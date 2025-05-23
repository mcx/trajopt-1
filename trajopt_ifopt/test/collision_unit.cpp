/**
 * @file collision_unit.cpp
 * @brief The collision constraint unit test
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
#include <tesseract_common/resource_locator.h>
#include <tesseract_kinematics/core/joint_group.h>
#include <tesseract_environment/environment.h>
#include <tesseract_environment/utils.h>
#include <ifopt/problem.h>
#include <ifopt/ipopt_solver.h>
#include <trajopt_common/collision_types.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt_ifopt/constraints/collision/discrete_collision_constraint.h>
#include <trajopt_ifopt/constraints/collision/discrete_collision_evaluators.h>
#include <trajopt_ifopt/variable_sets/joint_position_variable.h>

using namespace trajopt_ifopt;
using namespace tesseract_environment;
using namespace tesseract_kinematics;
using namespace tesseract_collision;
using namespace tesseract_scene_graph;
using namespace tesseract_geometry;
using namespace tesseract_common;

class CollisionUnit : public testing::TestWithParam<const char*>
{
public:
  Environment::Ptr env = std::make_shared<Environment>(); /**< Tesseract */
  trajopt_ifopt::DiscreteCollisionEvaluator::Ptr collision_evaluator;
  ifopt::Problem nlp;
  std::shared_ptr<trajopt_ifopt::DiscreteCollisionConstraint> constraint;

  void SetUp() override
  {
    const std::filesystem::path urdf_file(std::string(TRAJOPT_DATA_DIR) + "/boxbot.urdf");
    const std::filesystem::path srdf_file(std::string(TRAJOPT_DATA_DIR) + "/boxbot.srdf");

    const ResourceLocator::Ptr locator = std::make_shared<tesseract_common::GeneralResourceLocator>();
    EXPECT_TRUE(env->init(urdf_file, srdf_file, locator));

    // Set up collision evaluator
    const tesseract_kinematics::JointGroup::ConstPtr kin = env->getJointGroup("manipulator");
    trajopt_common::TrajOptCollisionConfig config(0.1, 1);
    auto collision_cache = std::make_shared<trajopt_ifopt::CollisionCache>(100);

    collision_evaluator =
        std::make_shared<trajopt_ifopt::SingleTimestepCollisionEvaluator>(collision_cache, kin, env, config);

    // 3) Add Variables
    Eigen::VectorXd pos(2);
    pos << -1.9, 0;
    auto var0 = std::make_shared<trajopt_ifopt::JointPosition>(pos, kin->getJointNames(), "Joint_Position_0");
    nlp.AddVariableSet(var0);

    constraint = std::make_shared<trajopt_ifopt::DiscreteCollisionConstraint>(collision_evaluator, var0, 1);
    nlp.AddConstraintSet(constraint);
  }
};

/**
 * @brief Tests that GetValues and FillJacobianBlock return the correct values
 */
TEST_F(CollisionUnit, GetValueFillJacobian)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CollisionUnit, GetValueFillJacobian");

  // Not in collision. Outside buffer
  {
    Eigen::VectorXd pos(2);
    // Small offset in y is to avoid numeric instabilites with perfectly aligning edges
    pos << -1.9, 0.01;
    nlp.SetVariables(pos.data());
    Eigen::VectorXd values = constraint->GetValues();
    EXPECT_NEAR(values[0], -1 * collision_evaluator->GetCollisionMarginBuffer(), 1e-6);

    ifopt::ConstraintSet::Jacobian jac_block;
    jac_block.resize(1, 2);
    constraint->FillJacobianBlock("Joint_Position_0", jac_block);
    const double dx = jac_block.coeff(0, 0);
    const double dy = jac_block.coeff(0, 1);
    EXPECT_NEAR(dx, 0.0, 1e-6);
    EXPECT_NEAR(dy, 0.0, 1e-6);
  }

  // Not in collision. Within buffer
  {
    Eigen::VectorXd pos(2);
    pos << -1.0, 0.01;
    nlp.SetVariables(pos.data());
    Eigen::VectorXd values = constraint->GetValues();
    EXPECT_NEAR(values[0], 0.1, 1e-6);

    ifopt::ConstraintSet::Jacobian jac_block;
    jac_block.resize(1, 2);
    constraint->FillJacobianBlock("Joint_Position_0", jac_block);
    const double dx = jac_block.coeff(0, 0);
    const double dy = jac_block.coeff(0, 1);
    EXPECT_NEAR(dx, 1.0, 1e-6);
    EXPECT_NEAR(dy, 0.0, 1e-6);
  }
  {
    Eigen::VectorXd pos(2);
    pos << 0.01, 1.0;
    nlp.SetVariables(pos.data());
    Eigen::VectorXd values = constraint->GetValues();
    EXPECT_NEAR(values[0], 0.1, 1e-6);

    ifopt::ConstraintSet::Jacobian jac_block;
    jac_block.resize(1, 2);
    constraint->FillJacobianBlock("Joint_Position_0", jac_block);
    const double dx = jac_block.coeff(0, 0);
    const double dy = jac_block.coeff(0, 1);
    EXPECT_NEAR(dx, 0.0, 1e-6);
    EXPECT_NEAR(dy, -1.0, 1e-6);
  }

  // In collision
  {
    Eigen::VectorXd pos(2);
    pos << -0.9, 0.01;
    nlp.SetVariables(pos.data());
    Eigen::VectorXd values = constraint->GetValues();
    EXPECT_NEAR(values[0], 0.2, 1e-6);

    ifopt::ConstraintSet::Jacobian jac_block;
    jac_block.resize(1, 2);
    constraint->FillJacobianBlock("Joint_Position_0", jac_block);
    const double dx = jac_block.coeff(0, 0);
    const double dy = jac_block.coeff(0, 1);
    EXPECT_NEAR(dx, 1.0, 1e-6);
    EXPECT_NEAR(dy, 0.0, 1e-6);
  }
}

/**
 * @brief Checks that the Bounds are set correctly
 */
TEST_F(CollisionUnit, GetSetBounds)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CollisionUnit, GetSetBounds");

  // Check that setting bounds works
  {
    Eigen::VectorXd pos(2);
    pos << -1.9, 0;
    const std::vector<std::string> joint_names(2, "names");
    auto var0 = std::make_shared<trajopt_ifopt::JointPosition>(pos, joint_names, "Joint_Position_0");

    auto constraint_2 = std::make_shared<trajopt_ifopt::DiscreteCollisionConstraint>(collision_evaluator, var0, 3);
    const ifopt::Bounds bounds(-0.1234, 0.5678);
    std::vector<ifopt::Bounds> bounds_vec = std::vector<ifopt::Bounds>(1, bounds);
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
TEST_F(CollisionUnit, IgnoreVariables)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("CollisionUnit, IgnoreVariables");

  // Check that jacobian does not change for variables it shouldn't
  {
    ifopt::ConstraintSet::Jacobian jac_block_input;
    jac_block_input.resize(1, 2);
    constraint->FillJacobianBlock("another_var", jac_block_input);
    EXPECT_EQ(jac_block_input.nonZeros(), 0);
  }
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
