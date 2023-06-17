/**
 * @file continuous_collision_evaluators.h
 * @brief Contains continuous evaluators for the collision constraint
 *
 * @author Levi Armstrong
 * @author Matthew Powelson
 * @date Nov 24, 2020
 * @version TODO
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <trajopt_ifopt/constraints/collision/continuous_collision_evaluators.h>
#include <trajopt_ifopt/constraints/collision/collision_utils.h>

namespace trajopt_ifopt
{
LVSContinuousCollisionEvaluator::LVSContinuousCollisionEvaluator(
    std::shared_ptr<CollisionCache> collision_cache,
    tesseract_kinematics::JointGroup::ConstPtr manip,
    tesseract_environment::Environment::ConstPtr env,
    trajopt_ifopt::TrajOptCollisionConfig::ConstPtr collision_config,
    bool dynamic_environment)
  : collision_cache_(std::move(collision_cache))
  , manip_(std::move(manip))
  , env_(std::move(env))
  , collision_config_(std::move(collision_config))
  , dynamic_environment_(dynamic_environment)
{
  manip_active_link_names_ = manip_->getActiveLinkNames();

  // If the environment is not expected to change, then the cloned state solver may be used each time.
  if (dynamic_environment_)
  {
    get_state_fn_ = [&](const Eigen::Ref<const Eigen::VectorXd>& joint_values) {
      return env_->getState(manip_->getJointNames(), joint_values).link_transforms;
    };
    env_active_link_names_ = env_->getActiveLinkNames();

    std::sort(manip_active_link_names_.begin(), manip_active_link_names_.end());
    std::sort(env_active_link_names_.begin(), env_active_link_names_.end());
    std::set_difference(env_active_link_names_.begin(),
                        env_active_link_names_.end(),
                        manip_active_link_names_.begin(),
                        manip_active_link_names_.end(),
                        std::inserter(diff_active_link_names_, diff_active_link_names_.begin()));
  }
  else
  {
    get_state_fn_ = [&](const Eigen::Ref<const Eigen::VectorXd>& joint_values) {
      return manip_->calcFwdKin(joint_values);
    };
    env_active_link_names_ = manip_->getActiveLinkNames();
  }

  contact_manager_ = env_->getContinuousContactManager();
  contact_manager_->setActiveCollisionObjects(manip_active_link_names_);
  contact_manager_->setCollisionMarginData(collision_config_->contact_manager_config.margin_data);
  // Increase the default by the buffer
  contact_manager_->setDefaultCollisionMarginData(
      collision_config_->contact_manager_config.margin_data.getMaxCollisionMargin() +
      collision_config_->collision_margin_buffer);
}

CollisionCacheData::ConstPtr
LVSContinuousCollisionEvaluator::CalcCollisionData(const Eigen::Ref<const Eigen::VectorXd>& dof_vals0,
                                                   const Eigen::Ref<const Eigen::VectorXd>& dof_vals1,
                                                   const std::array<bool, 2>& position_vars_fixed,
                                                   std::size_t bounds_size)
{
  size_t key = getHash(*collision_config_, dof_vals0, dof_vals1);
  auto* it = collision_cache_->get(key);
  if (it != nullptr)
  {
    CONSOLE_BRIDGE_logDebug("Using cached collision check");
    return *it;
  }

  auto data = std::make_shared<CollisionCacheData>();
  CalcCollisionsHelper(dof_vals0, dof_vals1, data->contact_results_map);

  for (const auto& pair : data->contact_results_map)
  {
    using ShapeGrsType = std::map<std::pair<std::size_t, std::size_t>, GradientResultsSet>;
    ShapeGrsType shape_grs;
    const double coeff =
        collision_config_->collision_coeff_data.getPairCollisionCoeff(pair.first.first, pair.first.second);
    for (const tesseract_collision::ContactResult& dist_result : pair.second)
    {
      const std::size_t shape_hash0 = cantorHash(dist_result.shape_id[0], dist_result.subshape_id[0]);
      const std::size_t shape_hash1 = cantorHash(dist_result.shape_id[1], dist_result.subshape_id[1]);
      auto shape_key = std::make_pair(shape_hash0, shape_hash1);
      auto it = shape_grs.find(shape_key);
      if (it == shape_grs.end())
      {
        GradientResultsSet grs;
        grs.key = pair.first;
        grs.shape_key = shape_key;
        grs.coeff = coeff;
        grs.is_continuous = true;
        grs.results.reserve(pair.second.size());
        grs.add(CalcGradientData(dof_vals0, dof_vals1, dist_result));
        shape_grs[shape_key] = grs;
      }
      else
      {
        it->second.add(CalcGradientData(dof_vals0, dof_vals1, dist_result));
      }
    }

    // This is not as efficient as it could be. Need to update Tesseract to store per subhshape key
    const std::size_t new_size = data->gradient_results_sets.size() + shape_grs.size();
    data->gradient_results_sets.reserve(new_size);

    std::transform(shape_grs.begin(),
                   shape_grs.end(),
                   std::back_inserter(data->gradient_results_sets),
                   std::bind(&ShapeGrsType::value_type::second, std::placeholders::_1));  // NOLINT
  }

  if (data->gradient_results_sets.size() > bounds_size)
  {
    if (!position_vars_fixed[0] && !position_vars_fixed[1])
    {
      std::sort(data->gradient_results_sets.begin(),
                data->gradient_results_sets.end(),
                [](const GradientResultsSet& a, const GradientResultsSet& b) {
                  return a.getMaxErrorWithBuffer() > b.getMaxErrorWithBuffer();
                });
    }
    else if (!position_vars_fixed[0])
    {
      std::sort(data->gradient_results_sets.begin(),
                data->gradient_results_sets.end(),
                [](const GradientResultsSet& a, const GradientResultsSet& b) {
                  return a.getMaxErrorWithBufferT0() > b.getMaxErrorWithBufferT0();
                });
    }
    else
    {
      std::sort(data->gradient_results_sets.begin(),
                data->gradient_results_sets.end(),
                [](const GradientResultsSet& a, const GradientResultsSet& b) {
                  return a.getMaxErrorWithBufferT1() > b.getMaxErrorWithBufferT1();
                });
    }
  }

  collision_cache_->put(key, data);
  return data;
}

void LVSContinuousCollisionEvaluator::CalcCollisionsHelper(const Eigen::Ref<const Eigen::VectorXd>& dof_vals0,
                                                           const Eigen::Ref<const Eigen::VectorXd>& dof_vals1,
                                                           tesseract_collision::ContactResultMap& dist_results)
{
  // The first step is to see if the distance between two states is larger than the longest valid segment. If larger
  // the collision checking is broken up into multiple casted collision checks such that each check is less then
  // the longest valid segment length.
  double dist = (dof_vals1 - dof_vals0).norm();

  // If not empty then there are links that are not part of the kinematics object that can move (dynamic environment)
  if (!diff_active_link_names_.empty())
  {
    tesseract_common::TransformMap state = get_state_fn_(dof_vals0);
    for (const auto& link_name : diff_active_link_names_)
      contact_manager_->setCollisionObjectsTransform(link_name, state[link_name]);
  }

  // Create filter
  // Don't include contacts at the fixed state
  // Don't include contacts with zero coeffs
  const auto& zero_coeff_pairs = collision_config_->collision_coeff_data.getPairsWithZeroCoeff();
  auto filter = [this, &zero_coeff_pairs](tesseract_collision::ContactResultMap::PairType& pair) {
    // Remove pairs with zero coeffs
    if (std::find(zero_coeff_pairs.begin(), zero_coeff_pairs.end(), pair.first) != zero_coeff_pairs.end())
    {
      pair.second.clear();
      return;
    }

    // Contains the contact distance threshold and coefficient for the given link pair
    double dist = collision_config_->contact_manager_config.margin_data.getPairCollisionMargin(pair.first.first,
                                                                                               pair.first.second);
    double coeff = collision_config_->collision_coeff_data.getPairCollisionCoeff(pair.first.first, pair.first.second);
    const Eigen::Vector3d data = { dist, collision_config_->collision_margin_buffer, coeff };
    removeInvalidContactResults(pair.second, data); /** @todo Should this be removed? levi */
  };

  if (dist > collision_config_->longest_valid_segment_length)
  {
    // Calculate the number state to interpolate
    long cnt = static_cast<long>(std::ceil(dist / collision_config_->longest_valid_segment_length)) + 1;

    // Create interpolated trajectory between two states that satisfies the longest valid segment length.
    tesseract_common::TrajArray subtraj(cnt, dof_vals0.size());
    for (long i = 0; i < dof_vals0.size(); ++i)
      subtraj.col(i) = Eigen::VectorXd::LinSpaced(cnt, dof_vals0(i), dof_vals1(i));

    // Perform casted collision checking for sub trajectory and store results in contacts_vector
    tesseract_collision::ContactResultMap contacts{ dist_results };
    int last_state_idx{ static_cast<int>(subtraj.rows()) - 1 };
    double dt = 1.0 / double(last_state_idx);
    for (int i = 0; i < subtraj.rows() - 1; ++i)
    {
      tesseract_common::TransformMap state0 = get_state_fn_(subtraj.row(i));
      tesseract_common::TransformMap state1 = get_state_fn_(subtraj.row(i + 1));

      for (const auto& link_name : manip_active_link_names_)
        contact_manager_->setCollisionObjectsTransform(link_name, state0[link_name], state1[link_name]);

      contact_manager_->contactTest(contacts, collision_config_->contact_request);
      if (!contacts.empty())
      {
        dist_results.addInterpolatedCollisionResults(
            contacts, i, last_state_idx, manip_active_link_names_, dt, false, filter);
      }
      contacts.clear();
    }
  }
  else
  {
    tesseract_common::TransformMap state0 = get_state_fn_(dof_vals0);
    tesseract_common::TransformMap state1 = get_state_fn_(dof_vals1);
    for (const auto& link_name : manip_active_link_names_)
      contact_manager_->setCollisionObjectsTransform(link_name, state0[link_name], state1[link_name]);

    contact_manager_->contactTest(dist_results, collision_config_->contact_request);

    dist_results.filter(filter);
  }
}

GradientResults
LVSContinuousCollisionEvaluator::CalcGradientData(const Eigen::Ref<const Eigen::VectorXd>& dof_vals0,
                                                  const Eigen::Ref<const Eigen::VectorXd>& dof_vals1,
                                                  const tesseract_collision::ContactResult& contact_results)
{
  // Contains the contact distance threshold and coefficient for the given link pair
  double margin = collision_config_->contact_manager_config.margin_data.getPairCollisionMargin(
      contact_results.link_names[0], contact_results.link_names[1]);

  return getGradient(dof_vals0, dof_vals1, contact_results, margin, collision_config_->collision_margin_buffer, manip_);
}

const trajopt_ifopt::TrajOptCollisionConfig& LVSContinuousCollisionEvaluator::GetCollisionConfig() const
{
  return *collision_config_;
}

//////////////////////////////////////////

LVSDiscreteCollisionEvaluator::LVSDiscreteCollisionEvaluator(
    std::shared_ptr<CollisionCache> collision_cache,
    tesseract_kinematics::JointGroup::ConstPtr manip,
    tesseract_environment::Environment::ConstPtr env,
    trajopt_ifopt::TrajOptCollisionConfig::ConstPtr collision_config,
    bool dynamic_environment)
  : collision_cache_(std::move(collision_cache))
  , manip_(std::move(manip))
  , env_(std::move(env))
  , collision_config_(std::move(collision_config))
  , dynamic_environment_(dynamic_environment)
{
  manip_active_link_names_ = manip_->getActiveLinkNames();

  // If the environment is not expected to change, then the cloned state solver may be used each time.
  if (dynamic_environment_)
  {
    get_state_fn_ = [&](const Eigen::Ref<const Eigen::VectorXd>& joint_values) {
      return env_->getState(manip_->getJointNames(), joint_values).link_transforms;
    };
    env_active_link_names_ = env_->getActiveLinkNames();

    std::sort(manip_active_link_names_.begin(), manip_active_link_names_.end());
    std::sort(env_active_link_names_.begin(), env_active_link_names_.end());
    std::set_difference(env_active_link_names_.begin(),
                        env_active_link_names_.end(),
                        manip_active_link_names_.begin(),
                        manip_active_link_names_.end(),
                        std::inserter(diff_active_link_names_, diff_active_link_names_.begin()));
  }
  else
  {
    get_state_fn_ = [&](const Eigen::Ref<const Eigen::VectorXd>& joint_values) {
      return manip_->calcFwdKin(joint_values);
    };
    env_active_link_names_ = manip_->getActiveLinkNames();
  }

  contact_manager_ = env_->getDiscreteContactManager();
  contact_manager_->setActiveCollisionObjects(manip_active_link_names_);
  contact_manager_->setCollisionMarginData(collision_config_->contact_manager_config.margin_data);
  // Increase the default by the buffer
  contact_manager_->setDefaultCollisionMarginData(
      collision_config_->contact_manager_config.margin_data.getMaxCollisionMargin() +
      collision_config_->collision_margin_buffer);
}

CollisionCacheData::ConstPtr
LVSDiscreteCollisionEvaluator::CalcCollisionData(const Eigen::Ref<const Eigen::VectorXd>& dof_vals0,
                                                 const Eigen::Ref<const Eigen::VectorXd>& dof_vals1,
                                                 const std::array<bool, 2>& position_vars_fixed,
                                                 std::size_t bounds_size)
{
  size_t key = getHash(*collision_config_, dof_vals0, dof_vals1);
  auto* it = collision_cache_->get(key);
  if (it != nullptr)
  {
    CONSOLE_BRIDGE_logDebug("Using cached collision check");
    return *it;
  }

  auto data = std::make_shared<CollisionCacheData>();
  CalcCollisionsHelper(dof_vals0, dof_vals1, data->contact_results_map);
  for (const auto& pair : data->contact_results_map)
  {
    using ShapeGrsType = std::map<std::pair<std::size_t, std::size_t>, GradientResultsSet>;
    ShapeGrsType shape_grs;
    const double coeff =
        collision_config_->collision_coeff_data.getPairCollisionCoeff(pair.first.first, pair.first.second);
    for (const tesseract_collision::ContactResult& dist_result : pair.second)
    {
      const std::size_t shape_hash0 = cantorHash(dist_result.shape_id[0], dist_result.subshape_id[0]);
      const std::size_t shape_hash1 = cantorHash(dist_result.shape_id[1], dist_result.subshape_id[1]);
      auto shape_key = std::make_pair(shape_hash0, shape_hash1);
      auto it = shape_grs.find(shape_key);
      if (it == shape_grs.end())
      {
        GradientResultsSet grs;
        grs.key = pair.first;
        grs.shape_key = shape_key;
        grs.coeff = coeff;
        grs.is_continuous = true;
        grs.results.reserve(pair.second.size());
        grs.add(CalcGradientData(dof_vals0, dof_vals1, dist_result));
        shape_grs[shape_key] = grs;
      }
      else
      {
        it->second.add(CalcGradientData(dof_vals0, dof_vals1, dist_result));
      }
    }

    // This is not as efficient as it could be. Need to update Tesseract to store per subhshape key
    const std::size_t new_size = data->gradient_results_sets.size() + shape_grs.size();
    data->gradient_results_sets.reserve(new_size);

    std::transform(shape_grs.begin(),
                   shape_grs.end(),
                   std::back_inserter(data->gradient_results_sets),
                   std::bind(&ShapeGrsType::value_type::second, std::placeholders::_1));  // NOLINT
  }

  if (data->gradient_results_sets.size() > bounds_size)
  {
    if (!position_vars_fixed[0] && !position_vars_fixed[1])
    {
      std::sort(data->gradient_results_sets.begin(),
                data->gradient_results_sets.end(),
                [](const GradientResultsSet& a, const GradientResultsSet& b) {
                  return a.getMaxErrorWithBuffer() > b.getMaxErrorWithBuffer();
                });
    }
    else if (!position_vars_fixed[0])
    {
      std::sort(data->gradient_results_sets.begin(),
                data->gradient_results_sets.end(),
                [](const GradientResultsSet& a, const GradientResultsSet& b) {
                  return a.getMaxErrorWithBufferT0() > b.getMaxErrorWithBufferT0();
                });
    }
    else
    {
      std::sort(data->gradient_results_sets.begin(),
                data->gradient_results_sets.end(),
                [](const GradientResultsSet& a, const GradientResultsSet& b) {
                  return a.getMaxErrorWithBufferT1() > b.getMaxErrorWithBufferT1();
                });
    }
  }

  collision_cache_->put(key, data);
  return data;
}

void LVSDiscreteCollisionEvaluator::CalcCollisionsHelper(const Eigen::Ref<const Eigen::VectorXd>& dof_vals0,
                                                         const Eigen::Ref<const Eigen::VectorXd>& dof_vals1,
                                                         tesseract_collision::ContactResultMap& dist_results)
{
  // If not empty then there are links that are not part of the kinematics object that can move (dynamic environment)
  if (!diff_active_link_names_.empty())
  {
    tesseract_common::TransformMap state = get_state_fn_(dof_vals0);
    for (const auto& link_name : diff_active_link_names_)
      contact_manager_->setCollisionObjectsTransform(link_name, state[link_name]);
  }

  // Create filter
  // Don't include contacts at the fixed state
  // Don't include contacts with zero coeffs
  const auto& zero_coeff_pairs = collision_config_->collision_coeff_data.getPairsWithZeroCoeff();
  auto filter = [this, &zero_coeff_pairs](tesseract_collision::ContactResultMap::PairType& pair) {
    // Remove pairs with zero coeffs
    if (std::find(zero_coeff_pairs.begin(), zero_coeff_pairs.end(), pair.first) != zero_coeff_pairs.end())
    {
      pair.second.clear();
      return;
    }

    // Contains the contact distance threshold and coefficient for the given link pair
    double dist = collision_config_->contact_manager_config.margin_data.getPairCollisionMargin(pair.first.first,
                                                                                               pair.first.second);
    double coeff = collision_config_->collision_coeff_data.getPairCollisionCoeff(pair.first.first, pair.first.second);
    const Eigen::Vector3d data = { dist, collision_config_->collision_margin_buffer, coeff };

    // Don't include contacts at the fixed state
    removeInvalidContactResults(pair.second, data);
  };

  // The first step is to see if the distance between two states is larger than the longest valid segment. If larger
  // the collision checking is broken up into multiple casted collision checks such that each check is less then
  // the longest valid segment length.
  double dist = (dof_vals1 - dof_vals0).norm();
  long cnt = 2;
  if (dist > collision_config_->longest_valid_segment_length)
  {
    // Calculate the number state to interpolate
    cnt = static_cast<long>(std::ceil(dist / collision_config_->longest_valid_segment_length)) + 1;
  }

  // Create interpolated trajectory between two states that satisfies the longest valid segment length.
  tesseract_common::TrajArray subtraj(cnt, dof_vals0.size());
  for (long i = 0; i < dof_vals0.size(); ++i)
    subtraj.col(i) = Eigen::VectorXd::LinSpaced(cnt, dof_vals0(i), dof_vals1(i));

  // Perform casted collision checking for sub trajectory and store results in contacts_vector
  tesseract_collision::ContactResultMap contacts{ dist_results };
  int last_state_idx{ static_cast<int>(subtraj.rows()) - 1 };
  double dt = 1.0 / double(last_state_idx);
  for (int i = 0; i < subtraj.rows(); ++i)
  {
    tesseract_common::TransformMap state0 = get_state_fn_(subtraj.row(i));

    for (const auto& link_name : manip_active_link_names_)
      contact_manager_->setCollisionObjectsTransform(link_name, state0[link_name]);

    contact_manager_->contactTest(contacts, collision_config_->contact_request);

    if (!contacts.empty())
    {
      dist_results.addInterpolatedCollisionResults(
          contacts, i, last_state_idx, manip_active_link_names_, dt, true, filter);
    }
    contacts.clear();
  }
}

GradientResults
LVSDiscreteCollisionEvaluator::CalcGradientData(const Eigen::Ref<const Eigen::VectorXd>& dof_vals0,
                                                const Eigen::Ref<const Eigen::VectorXd>& dof_vals1,
                                                const tesseract_collision::ContactResult& contact_results)
{
  // Contains the contact distance threshold and coefficient for the given link pair
  double margin = collision_config_->contact_manager_config.margin_data.getPairCollisionMargin(
      contact_results.link_names[0], contact_results.link_names[1]);

  return getGradient(dof_vals0, dof_vals1, contact_results, margin, collision_config_->collision_margin_buffer, manip_);
}

const trajopt_ifopt::TrajOptCollisionConfig& LVSDiscreteCollisionEvaluator::GetCollisionConfig() const
{
  return *collision_config_;
}

}  // namespace trajopt_ifopt
