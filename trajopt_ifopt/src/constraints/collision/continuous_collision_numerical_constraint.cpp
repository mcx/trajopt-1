/**
 * @file continuous_collision_numerical_constraint.cpp
 * @brief The continuous collision numerical constraint
 *
 * @author Levi Armstrong
 * @author Matthew Powelson
 * @date May 18, 2020
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2020, Southwest Research Institute
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

#include <trajopt_common/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <trajopt_common/collision_types.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt_ifopt/constraints/collision/continuous_collision_numerical_constraint.h>
#include <trajopt_ifopt/constraints/collision/continuous_collision_evaluators.h>
#include <trajopt_ifopt/variable_sets/joint_position_variable.h>
#include <trajopt_ifopt/constraints/collision/weighted_average_methods.h>

namespace trajopt_ifopt
{
ContinuousCollisionNumericalConstraint::ContinuousCollisionNumericalConstraint(
    std::shared_ptr<ContinuousCollisionEvaluator> collision_evaluator,
    std::array<std::shared_ptr<const JointPosition>, 2> position_vars,
    bool vars0_fixed,
    bool vars1_fixed,
    int max_num_cnt,
    bool fixed_sparsity,
    const std::string& name)
  : ifopt::ConstraintSet(max_num_cnt, name)
  , position_vars_(std::move(position_vars))
  , vars0_fixed_(vars0_fixed)
  , vars1_fixed_(vars1_fixed)
  , collision_evaluator_(std::move(collision_evaluator))
{
  if (position_vars_[0] == nullptr && position_vars_[1] == nullptr)
    throw std::runtime_error("position_vars contains a nullptr!");

  // Set n_dof_ for convenience
  n_dof_ = position_vars_[0]->GetRows();
  if (!(n_dof_ > 0))
    throw std::runtime_error("position_vars[0] is empty!");

  if (position_vars_[0]->GetRows() != position_vars_[1]->GetRows())
    throw std::runtime_error("position_vars are not the same size!");

  if (vars0_fixed_ && vars1_fixed_)
    throw std::runtime_error("position_vars are both fixed!");

  if (max_num_cnt < 1)
    throw std::runtime_error("max_num_cnt must be greater than zero!");

  bounds_ = std::vector<ifopt::Bounds>(static_cast<std::size_t>(max_num_cnt), ifopt::BoundSmallerZero);

  if (fixed_sparsity)
  {
    // Setting to zeros because snopt sparsity cannot change
    triplet_list_.reserve(static_cast<std::size_t>(bounds_.size()) *
                          static_cast<std::size_t>(position_vars_[0]->GetRows()));

    for (Eigen::Index i = 0; i < static_cast<Eigen::Index>(bounds_.size()); i++)  // NOLINT
      for (Eigen::Index j = 0; j < n_dof_; j++)
        triplet_list_.emplace_back(i, j, 0);
  }
}

Eigen::VectorXd ContinuousCollisionNumericalConstraint::GetValues() const
{
  // Get current joint values
  const Eigen::VectorXd joint_vals0 = this->GetVariables()->GetComponent(position_vars_[0]->GetName())->GetValues();
  const Eigen::VectorXd joint_vals1 = this->GetVariables()->GetComponent(position_vars_[1]->GetName())->GetValues();
  const double margin_buffer = collision_evaluator_->GetCollisionMarginBuffer();
  Eigen::VectorXd values = Eigen::VectorXd::Constant(static_cast<Eigen::Index>(bounds_.size()), -margin_buffer);

  auto collision_data =
      collision_evaluator_->CalcCollisionData(joint_vals0, joint_vals1, vars0_fixed_, vars1_fixed_, bounds_.size());

  if (collision_data->gradient_results_sets.empty())
    return values;

  const std::size_t cnt = std::min(bounds_.size(), collision_data->gradient_results_sets.size());

  if (!vars0_fixed_ && !vars1_fixed_)
  {
    for (std::size_t i = 0; i < cnt; ++i)
    {
      const trajopt_common::GradientResultsSet& r = collision_data->gradient_results_sets[i];
      values(static_cast<Eigen::Index>(i)) = r.coeff * r.getMaxError();
    }
  }
  else if (!vars0_fixed_)
  {
    for (std::size_t i = 0; i < cnt; ++i)
    {
      const trajopt_common::GradientResultsSet& r = collision_data->gradient_results_sets[i];
      values(static_cast<Eigen::Index>(i)) = r.coeff * r.getMaxErrorT0();
    }
  }
  else
  {
    for (std::size_t i = 0; i < cnt; ++i)
    {
      const trajopt_common::GradientResultsSet& r = collision_data->gradient_results_sets[i];
      values(static_cast<Eigen::Index>(i)) = r.coeff * r.getMaxErrorT1();
    }
  }

  return values;
}

// Set the limits on the constraint values
std::vector<ifopt::Bounds> ContinuousCollisionNumericalConstraint::GetBounds() const { return bounds_; }

void ContinuousCollisionNumericalConstraint::FillJacobianBlock(std::string var_set, Jacobian& jac_block) const
{
  // Only modify the jacobian if this constraint uses var_set
  if (var_set != position_vars_[0]->GetName() && var_set != position_vars_[1]->GetName())  // NOLINT
    return;

  // Setting to zeros because snopt sparsity cannot change
  if (!triplet_list_.empty())                                               // NOLINT
    jac_block.setFromTriplets(triplet_list_.begin(), triplet_list_.end());  // NOLINT

  const double margin_buffer = collision_evaluator_->GetCollisionMarginBuffer();

  // Calculate collisions
  Eigen::VectorXd joint_vals0 = this->GetVariables()->GetComponent(position_vars_[0]->GetName())->GetValues();
  const Eigen::VectorXd joint_vals1 = this->GetVariables()->GetComponent(position_vars_[1]->GetName())->GetValues();

  auto collision_data =
      collision_evaluator_->CalcCollisionData(joint_vals0, joint_vals1, vars0_fixed_, vars1_fixed_, bounds_.size());
  if (collision_data->gradient_results_sets.empty())
    return;

  const std::size_t cnt = std::min(bounds_.size(), collision_data->gradient_results_sets.size());

  Eigen::VectorXd jv = joint_vals0;
  const double delta = 1e-8;
  for (int j = 0; j < n_dof_; j++)
  {
    jv(j) = joint_vals0(j) + delta;
    auto collision_data_delta =
        collision_evaluator_->CalcCollisionData(jv, joint_vals1, vars0_fixed_, vars1_fixed_, bounds_.size());

    for (int i = 0; i < static_cast<int>(cnt); ++i)
    {
      const auto& baseline = collision_data->gradient_results_sets[static_cast<std::size_t>(i)];
      auto fn = [&baseline](const trajopt_common::GradientResultsSet& cr) {
        return (cr.key == baseline.key && cr.shape_key == baseline.shape_key);
      };
      auto it = std::find_if(
          collision_data_delta->gradient_results_sets.begin(), collision_data_delta->gradient_results_sets.end(), fn);
      if (it != collision_data_delta->gradient_results_sets.end())
      {
        double dist_delta{ 0 };
        if (!vars0_fixed_ && !vars1_fixed_)
          dist_delta = it->coeff * (it->getMaxError() - baseline.getMaxError());
        else if (!vars0_fixed_)
          dist_delta = it->coeff * (it->getMaxErrorT0() - baseline.getMaxErrorT0());
        else
          dist_delta = it->coeff * (it->getMaxErrorT1() - baseline.getMaxErrorT1());

        jac_block.coeffRef(i, j) = dist_delta / delta;
      }
      else
      {
        double dist_delta{ 0 };
        if (!vars0_fixed_ && !vars1_fixed_)
          dist_delta = baseline.coeff * ((-1.0 * margin_buffer) - baseline.getMaxError());
        else if (!vars0_fixed_)
          dist_delta = baseline.coeff * ((-1.0 * margin_buffer) - baseline.getMaxErrorT0());
        else
          dist_delta = baseline.coeff * ((-1.0 * margin_buffer) - baseline.getMaxErrorT1());

        jac_block.coeffRef(i, j) = dist_delta / delta;
      }
    }
    jv(j) = joint_vals0(j);
  }
}

void ContinuousCollisionNumericalConstraint::SetBounds(const std::vector<ifopt::Bounds>& bounds)
{
  assert(bounds.size() == 1);
  bounds_ = bounds;
}

std::shared_ptr<ContinuousCollisionEvaluator> ContinuousCollisionNumericalConstraint::GetCollisionEvaluator() const
{
  return collision_evaluator_;
}

}  // namespace trajopt_ifopt
