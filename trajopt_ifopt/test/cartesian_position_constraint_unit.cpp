﻿/**
 * @file cartesian_position_constraint_unit.h
 * @brief The cartesian position constraint unit tests
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
#include <ctime>
#include <gtest/gtest.h>
#include <console_bridge/console.h>
#include <ifopt/problem.h>
#include <tesseract_common/resource_locator.h>
#include <tesseract_kinematics/core/joint_group.h>
#include <tesseract_environment/environment.h>
#include <tesseract_environment/utils.h>
#include <tesseract_common/types.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt_common/utils.hpp>
#include <trajopt_ifopt/constraints/cartesian_position_constraint.h>
#include <trajopt_ifopt/variable_sets/joint_position_variable.h>
#include <trajopt_ifopt/utils/numeric_differentiation.h>

using namespace trajopt_ifopt;
using namespace std;
using namespace trajopt_common;
using namespace tesseract_environment;
using namespace tesseract_kinematics;
using namespace tesseract_collision;
using namespace tesseract_scene_graph;
using namespace tesseract_geometry;
using namespace tesseract_common;

class CartesianPositionConstraintUnit : public testing::TestWithParam<const char*>
{
public:
  Environment::Ptr env = std::make_shared<Environment>();
  ifopt::Problem nlp;

  tesseract_kinematics::JointGroup::ConstPtr kin_group;
  CartPosConstraint::Ptr constraint;

  Eigen::Index n_dof{ -1 };

  void SetUp() override
  {
    // Initialize Tesseract
    const std::filesystem::path urdf_file(std::string(TRAJOPT_DATA_DIR) + "/arm_around_table.urdf");
    const std::filesystem::path srdf_file(std::string(TRAJOPT_DATA_DIR) + "/pr2.srdf");
    auto locator = std::make_shared<tesseract_common::GeneralResourceLocator>();
    auto env = std::make_shared<Environment>();
    const bool status = env->init(urdf_file, srdf_file, locator);
    EXPECT_TRUE(status);

    // Extract necessary kinematic information
    kin_group = env->getJointGroup("right_arm");
    n_dof = kin_group->numJoints();

    auto pos = Eigen::VectorXd::Ones(kin_group->numJoints());
    auto var0 = std::make_shared<trajopt_ifopt::JointPosition>(pos, kin_group->getJointNames(), "Joint_Position_0");
    nlp.AddVariableSet(var0);

    // 4) Add constraints
    const CartPosInfo cart_info(kin_group, "r_gripper_tool_frame", "base_footprint");
    constraint = std::make_shared<trajopt_ifopt::CartPosConstraint>(cart_info, var0);
    nlp.AddConstraintSet(constraint);
  }
};

/** @brief Checks that the GetValue function is correct */
TEST_F(CartesianPositionConstraintUnit, GetValue)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CartesianPositionConstraintUnit, GetValue");

  // Run FK to get target pose
  Eigen::VectorXd joint_position = Eigen::VectorXd::Ones(n_dof);
  const Eigen::Isometry3d target_pose = kin_group->calcFwdKin(joint_position).at("r_gripper_tool_frame");
  constraint->SetTargetPose(target_pose);

  // Set the joints to the joint position that should satisfy it
  nlp.SetVariables(joint_position.data());

  // Given a joint position at the target, the error should be 0
  {
    auto error = constraint->CalcValues(joint_position);
    EXPECT_LT(error.maxCoeff(), 1e-3);
    EXPECT_GT(error.minCoeff(), -1e-3);
  }
  {
    auto error = constraint->GetValues();
    EXPECT_LT(error.maxCoeff(), 1e-3);
    EXPECT_GT(error.minCoeff(), -1e-3);
  }

  // Given a joint position a small distance away, check the error for translation
  {
    Eigen::Isometry3d target_pose_mod = target_pose;
    target_pose_mod.translate(Eigen::Vector3d(0.1, 0.0, 0.0));
    constraint->SetTargetPose(target_pose_mod);
    auto error = constraint->CalcValues(joint_position);
    EXPECT_NEAR(error[0], -0.1, 1e-3);
  }
  {
    Eigen::Isometry3d target_pose_mod = target_pose;
    target_pose_mod.translate(Eigen::Vector3d(0.0, 0.2, 0.0));
    constraint->SetTargetPose(target_pose_mod);
    auto error = constraint->CalcValues(joint_position);
    EXPECT_NEAR(error[1], -0.2, 1e-3);
  }
  {
    Eigen::Isometry3d target_pose_mod = target_pose;
    target_pose_mod.translate(Eigen::Vector3d(0.0, 0.0, -0.3));
    constraint->SetTargetPose(target_pose_mod);
    auto error = constraint->CalcValues(joint_position);
    EXPECT_NEAR(error[2], 0.3, 1e-3);
  }

  // TODO: Check error for orientation
}

/** @brief Checks that the FillJacobian function is correct */
TEST_F(CartesianPositionConstraintUnit, FillJacobian)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CartesianPositionConstraintUnit, FillJacobian");

  // Run FK to get target pose
  const Eigen::VectorXd joint_position = Eigen::VectorXd::Ones(n_dof);
  const Eigen::Isometry3d target_pose = kin_group->calcFwdKin(joint_position).at("r_gripper_tool_frame");
  constraint->SetTargetPose(target_pose);

  // Modify one joint at a time
  for (Eigen::Index i = 0; i < n_dof; i++)
  {
    // Set the joints
    Eigen::VectorXd joint_position_mod = joint_position;
    joint_position_mod[i] = 2.0;
    nlp.SetVariables(joint_position_mod.data());

    // Calculate jacobian numerically
    auto error_calculator = [&](const Eigen::Ref<const Eigen::VectorXd>& x) { return constraint->CalcValues(x); };
    const trajopt_ifopt::SparseMatrix num_jac_block =
        trajopt_ifopt::calcForwardNumJac(error_calculator, joint_position_mod, 1e-4);

    // Compare to constraint jacobian
    {
      trajopt_ifopt::SparseMatrix jac_block(num_jac_block.rows(), num_jac_block.cols());
      constraint->CalcJacobianBlock(joint_position_mod, jac_block);  // NOLINT
      EXPECT_TRUE(jac_block.isApprox(num_jac_block, 1e-3));
      //      std::cout << "Numeric:\n" << num_jac_block.toDense() << '\n';
      //      std::cout << "Analytic:\n" << jac_block.toDense() << '\n';
    }
    {
      trajopt_ifopt::SparseMatrix jac_block(num_jac_block.rows(), num_jac_block.cols());
      constraint->FillJacobianBlock("Joint_Position_0", jac_block);
      EXPECT_TRUE(jac_block.toDense().isApprox(num_jac_block.toDense(), 1e-3));
      //      std::cout << "Numeric:\n" << num_jac_block.toDense() << '\n';
      //      std::cout << "Analytic:\n" << jac_block.toDense() << '\n';
    }
  }
}

/**
 * @brief Checks that the Bounds are set correctly
 */
TEST_F(CartesianPositionConstraintUnit, GetSetBounds)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CartesianPositionConstraintUnit, GetSetBounds");

  // Check that setting bounds works
  {
    const Eigen::VectorXd pos = Eigen::VectorXd::Ones(kin_group->numJoints());
    auto var0 = std::make_shared<trajopt_ifopt::JointPosition>(pos, kin_group->getJointNames(), "Joint_Position_0");

    const CartPosInfo cart_info(kin_group, "r_gripper_tool_frame", "base_footprint");
    auto constraint_2 = std::make_shared<trajopt_ifopt::CartPosConstraint>(cart_info, var0);

    const ifopt::Bounds bounds(-0.1234, 0.5678);
    std::vector<ifopt::Bounds> bounds_vec = std::vector<ifopt::Bounds>(6, bounds);

    constraint_2->SetBounds(bounds_vec);
    std::vector<ifopt::Bounds> results_vec = constraint_2->GetBounds();
    for (std::size_t i = 0; i < bounds_vec.size(); i++)
    {
      EXPECT_EQ(bounds_vec[i].lower_, results_vec[i].lower_);
      EXPECT_EQ(bounds_vec[i].upper_, results_vec[i].upper_);
    }
  }
}

/**
 * @brief Checks that the constraint doesn't change the jacobian when it shouldn't
 */
TEST_F(CartesianPositionConstraintUnit, IgnoreVariables)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CartesianPositionConstraintUnit, IgnoreVariables");

  // Check that jacobian does not change for variables it shouldn't
  {
    ifopt::ConstraintSet::Jacobian jac_block_input;
    jac_block_input.resize(n_dof, n_dof);
    constraint->FillJacobianBlock("another_var", jac_block_input);
    EXPECT_EQ(jac_block_input.nonZeros(), 0);
  }

  // Check that it is fine with jac blocks the wrong size for this constraint
  {
    ifopt::ConstraintSet::Jacobian jac_block_input;
    jac_block_input.resize(3, 5);
    constraint->FillJacobianBlock("another_var2", jac_block_input);
    EXPECT_EQ(jac_block_input.nonZeros(), 0);
  }
}

////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
