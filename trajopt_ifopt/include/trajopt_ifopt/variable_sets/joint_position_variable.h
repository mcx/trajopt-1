/**
 * @file joint_position_variable.h
 * @brief Contains the joint position variable
 *
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
#ifndef TRAJOPT_IFOPT_JOINT_POSITION_VARIABLE_H
#define TRAJOPT_IFOPT_JOINT_POSITION_VARIABLE_H

#include <trajopt_common/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <ifopt/variable_set.h>
#include <ifopt/bounds.h>
#include <Eigen/Core>
#include <tesseract_common/fwd.h>
TRAJOPT_IGNORE_WARNINGS_POP

namespace trajopt_ifopt
{
/** @brief Represents a single joint position in the optimization. Values are of dimension 1 x n_dof */
class JointPosition : public ifopt::VariableSet
{
public:
  using Ptr = std::shared_ptr<JointPosition>;
  using ConstPtr = std::shared_ptr<const JointPosition>;

  JointPosition(const Eigen::Ref<const Eigen::VectorXd>& init_value,
                std::vector<std::string> joint_names,
                const std::string& name = "Joint_Position");

  JointPosition(const Eigen::Ref<const Eigen::VectorXd>& init_value,
                std::vector<std::string> joint_names,
                const ifopt::Bounds& bounds,
                const std::string& name = "Joint_Position");

  JointPosition(const Eigen::Ref<const Eigen::VectorXd>& init_value,
                std::vector<std::string> joint_names,
                const tesseract_common::KinematicLimits& bounds,
                const std::string& name = "Joint_Position");

  /**
   * @brief Sets this variable to the given joint position
   * @param x Joint Position to which this variable will be set.
   */
  void SetVariables(const Eigen::VectorXd& x) override;

  /**
   * @brief Gets the joint position associated with this variable
   * @return This variable's joint position
   */
  Eigen::VectorXd GetValues() const override;

  /**
   * @brief Gets the bounds on this variable
   * @return Bounds on this variable
   */
  VecBound GetBounds() const override;

  /**
   * @brief Sets the bounds for the joints in this variable.
   * @param new_bounds New bounds for the joints
   */
  void SetBounds(VecBound& new_bounds);

  /**
   * @brief Sets the bounds for the joints in this variable from a MatrixX2d with the first column being lower bound and
   * second column being upper bound
   * @param bounds Columns 1/2 are lower/upper bounds. You probably will get this from forward_kinematics->getLimits()
   */
  void SetBounds(const Eigen::Ref<const Eigen::MatrixX2d>& bounds);

  /**
   * @brief Get the joint names associated with this variable set
   * @return The joint names
   */
  std::vector<std::string> GetJointNames() const;

private:
  VecBound bounds_;
  Eigen::VectorXd values_;
  std::vector<std::string> joint_names_;
};

}  // namespace trajopt_ifopt

#endif
