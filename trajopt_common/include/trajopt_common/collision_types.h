/**
 * @file collision_types.h
 * @brief Contains data structures used by collision constraints
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
#ifndef TRAJOPT_COMMON_COLLISION_TYPES_H
#define TRAJOPT_COMMON_COLLISION_TYPES_H

#include <trajopt_common/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <Eigen/Eigen>
#include <array>
#include <memory>
#include <functional>
#include <tesseract_collision/core/types.h>
#include <tesseract_common/types.h>
#include <tesseract_common/eigen_types.h>
TRAJOPT_IGNORE_WARNINGS_POP

namespace trajopt_common
{
using GetStateFn = std::function<tesseract_common::TransformMap(const Eigen::Ref<const Eigen::VectorXd>& joint_values)>;

/** @brief Stores information about how the margins allowed between collision objects */
struct CollisionCoeffData
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  using Ptr = std::shared_ptr<CollisionCoeffData>;
  using ConstPtr = std::shared_ptr<const CollisionCoeffData>;

  CollisionCoeffData(double default_collision_coeff = 1);

  /**
   * @brief Set the coefficient for a given contact pair
   *
   * The order of the object names does not matter, that is handled internal to
   * the class.
   *
   * @param obj1 The first object name. Order doesn't matter
   * @param obj2 The Second object name. Order doesn't matter
   * @param Coeff Coefficient
   */
  void setCollisionCoeff(const std::string& obj1, const std::string& obj2, double collision_coeff);

  /**
   * @brief Get the pairs collision coefficient
   *
   * If a collision coeff for the request pair does not exist it returns the default collision coeff
   *
   * @param obj1 The first object name
   * @param obj2 The second object name
   * @return Coefficient
   */
  double getCollisionCoeff(const std::string& obj1, const std::string& obj2) const;

  /**
   * @brief Get the pairs with zero coeff
   * @return A vector of pairs with zero coeff
   */
  const std::set<tesseract_common::LinkNamesPair>& getPairsWithZeroCoeff() const;

private:
  /// Stores the collision coefficient used if no pair-specific one is set
  double default_collision_coeff_;

  /// A map of link pair names to contact distance
  std::unordered_map<tesseract_common::LinkNamesPair, double, tesseract_common::PairHash> lookup_table_;

  /// Pairs containing zero coeff
  std::set<tesseract_common::LinkNamesPair> zero_coeff_;

  friend class boost::serialization::access;
  friend struct tesseract_common::Serialization;
  template <class Archive>
  void serialize(Archive&, const unsigned int);  // NOLINT
};

/**
 * @brief Config settings for collision terms.
 */
struct TrajOptCollisionConfig
{
  using Ptr = std::shared_ptr<TrajOptCollisionConfig>;
  using ConstPtr = std::shared_ptr<const TrajOptCollisionConfig>;

  TrajOptCollisionConfig() = default;
  TrajOptCollisionConfig(
      double margin,
      double coeff,
      tesseract_collision::ContactRequest request = tesseract_collision::ContactRequest(),
      tesseract_collision::CollisionEvaluatorType type = tesseract_collision::CollisionEvaluatorType::DISCRETE,
      double longest_valid_segment_length = 0.005,
      tesseract_collision::CollisionCheckProgramType check_program_mode =
          tesseract_collision::CollisionCheckProgramType::ALL);

  /** @brief If true, a collision will be added to the problem. Default: true*/
  bool enabled = true;

  /** @brief The contact manager config */
  tesseract_collision::ContactManagerConfig contact_manager_config;

  /** @brief The contact check config */
  tesseract_collision::CollisionCheckConfig collision_check_config;

  /** @brief The collision coeff/weight */
  CollisionCoeffData collision_coeff_data;

  /** @brief Additional collision margin that is added for the collision check but is not used when calculating the
   * error */
  double collision_margin_buffer{ 0.01 };

  /**
   * @brief This define the max number of link pairs to be considered.
   * It still finds all contacts but sorts based on the worst uses those up to the max_num_cnt.
   * @note This is only used by trajopt_ifopt because the constraints size must be fixed.
   */
  int max_num_cnt{ 3 };

protected:
  friend class boost::serialization::access;
  friend struct tesseract_common::Serialization;
  template <class Archive>
  void serialize(Archive&, const unsigned int);  // NOLINT
};

/** @brief A data structure to contain a links gradient results */
struct LinkGradientResults
{
  /** @brief Indicates if gradient results are available */
  bool has_gradient{ false };

  /** @brief Gradient Results */
  Eigen::VectorXd gradient;

  /** @brief The minimum translation vector to move link out of collision*/
  Eigen::VectorXd translation_vector;

  /** @brief The robot jocobian at the contact point */
  Eigen::MatrixXd jacobian;

  /** @brief Gradient Scale */
  double scale{ 1.0 };

  /** @brief The continuous collision type */
  tesseract_collision::ContinuousCollisionType cc_type{ tesseract_collision::ContinuousCollisionType::CCType_None };
};

/** @brief A data structure to contain a link pair gradient results */
struct GradientResults
{
  /**
   * @brief The gradient results data for LinkA and LinkB
   * @details This is used by both discrete and continuous collision checking.
   * In the case of continuous collision checking this is the gradient at timestep0
   */
  std::array<LinkGradientResults, 2> gradients;

  /**
   * @brief The gradient results data for LinkA and LinkB
   * @details In the case of continuous collision checking, this is used to store the gradient at timestep1.
   */
  std::array<LinkGradientResults, 2> cc_gradients;

  /** @brief The error (margin - dist_result.distance) */
  double error{ 0 };

  /** @brief The error (margin + margin_buffer - dist_result.distance) */
  double error_with_buffer{ 0 };
};

struct LinkMaxError
{
  /** @brief Indicate if T0 or T1 has error */
  std::array<bool, 2> has_error{ false, false };

  /**
   * @brief The max error in the gradient_results
   * @details
   * [0] is the link max error excluding any values at T1
   * [1] is the link max error excluding any values at T0
   */
  std::array<double, 2> error{ std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };

  /**
   * @brief The max error with buffer in the gradient_results
   * @details
   * [0] is the link max error excluding any values at T1
   * [1] is the link max error excluding any values at T0
   */
  std::array<double, 2> error_with_buffer{ std::numeric_limits<double>::lowest(),
                                           std::numeric_limits<double>::lowest() };

  /**
   * @brief Get the max error including both T1 and T0 data
   * @return The max error including T1 and T0 data
   */
  double getMaxError() const;

  /**
   * @brief Get the max error with buffer including both T1 and T0 data
   * @return The max error with buffer including T1 and T0 data
   */
  double getMaxErrorWithBuffer() const;
};

/** @brief A set of gradient results */
struct GradientResultsSet
{
  GradientResultsSet() = default;
  GradientResultsSet(std::size_t reserve) { results.reserve(reserve); }

  /** @brief The map key from contact results map */
  std::pair<std::string, std::string> key;

  /** @brief For the link pair this is the subshap pair key */
  std::pair<std::size_t, std::size_t> shape_key;

  /** @brief The pair coeff */
  double coeff{ 1 };

  /**
   * @brief Indicate if this set is from a continuous contact checker
   * @details If false, the data is from a discrete contact checker
   */
  bool is_continuous{ false };

  /**
   * @brief The max errors
   * @details
   *   [0] Errors related to LinkA
   *   [1] Errors related to LinkB
   */
  std::array<LinkMaxError, 2> max_error;

  /** @brief The stored gradient results for this set */
  std::vector<GradientResults> results;

  /**
   * @brief Add bradient results to the set
   * @brief This updates max error data
   * @param gradient_result The gradient results to add
   */
  void add(const GradientResults& gradient_result);

  /** @brief Get the max error including T0 and T1 */
  double getMaxError() const;

  /** @brief Get the max error including excluding errors at T1 */
  double getMaxErrorT0() const;

  /** @brief Get the max error including excluding errors at T0 */
  double getMaxErrorT1() const;

  /** @brief Get the max error with buffer including T0 and T1 */
  double getMaxErrorWithBuffer() const;

  /** @brief Get the max error with buffer excluding errors at T1 */
  double getMaxErrorWithBufferT0() const;

  /** @brief Get the max error with buffer excluding errors at T0 */
  double getMaxErrorWithBufferT1() const;
};

/** @brief The data structure used to cache collision results data for discrete collision evaluator */
struct CollisionCacheData
{
  using Ptr = std::shared_ptr<CollisionCacheData>;
  using ConstPtr = std::shared_ptr<const CollisionCacheData>;

  tesseract_collision::ContactResultMap contact_results_map;
  std::vector<GradientResultsSet> gradient_results_sets;
};

}  // namespace trajopt_common

BOOST_CLASS_EXPORT_KEY(trajopt_common::CollisionCoeffData)
BOOST_CLASS_EXPORT_KEY(trajopt_common::TrajOptCollisionConfig)

#endif  // TRAJOPT_COMMON_COLLISION_TYPES_H
