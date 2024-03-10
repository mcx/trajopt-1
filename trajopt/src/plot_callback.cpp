#include <trajopt_common/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <functional>
#include <tesseract_common/joint_state.h>
#include <tesseract_state_solver/state_solver.h>
#include <tesseract_kinematics/core/joint_group.h>
#include <tesseract_environment/environment.h>
#include <tesseract_visualization/visualization.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt_sco/optimizers.hpp>
#include <trajopt_sco/modeling.hpp>

#include <trajopt/problem_description.hpp>
#include <trajopt/typedefs.hpp>
#include <trajopt/utils.hpp>

namespace trajopt
{
void PlotCosts(const std::shared_ptr<tesseract_visualization::Visualization>& plotter,
               const tesseract_scene_graph::StateSolver& state_solver,
               const std::vector<std::string>& joint_names,
               const std::vector<sco::Cost::Ptr>& costs,
               const std::vector<sco::Constraint::Ptr>& cnts,
               const VarArray& vars,
               const sco::OptResults& results)
{
  plotter->clear();

  for (const sco::Cost::Ptr& cost : costs)
  {
    if (auto* plt = dynamic_cast<Plotter*>(cost.get()))
    {
      plt->Plot(plotter, results.x);
    }
  }

  for (const sco::Constraint::Ptr& cnt : cnts)
  {
    if (auto* plt = dynamic_cast<Plotter*>(cnt.get()))
    {
      plt->Plot(plotter, results.x);
    }
  }

  auto traj = getTraj(results.x, vars);
  tesseract_common::JointTrajectory joint_trajectory;
  for (long i = 0; i < traj.rows(); ++i)
    joint_trajectory.states.emplace_back(joint_names, traj.row(i));

  plotter->plotTrajectory(joint_trajectory, state_solver);
  plotter->waitForInput();
}

sco::Optimizer::Callback PlotCallback(const std::shared_ptr<tesseract_visualization::Visualization>& plotter)
{
  return [plotter](sco::OptProb* prob, sco::OptResults& results) {
    auto& trajopt_prob = dynamic_cast<TrajOptProb&>(*prob);
    auto state_solver = trajopt_prob.GetEnv()->getStateSolver();
    PlotCosts(plotter,
              *state_solver,
              trajopt_prob.GetKin()->getJointNames(),
              std::ref(trajopt_prob.getCosts()),
              trajopt_prob.getConstraints(),
              std::ref(trajopt_prob.GetVars()),
              results);
  };
}

void PlotProb(const std::shared_ptr<tesseract_visualization::Visualization>& plotter,
              const tesseract_scene_graph::StateSolver& state_solver,
              const std::vector<std::string>& joint_names,
              sco::OptProb* prob,
              const sco::OptResults& results)
{
  plotter->clear();

  for (const sco::Cost::Ptr& cost : prob->getCosts())
  {
    if (auto* plt = dynamic_cast<Plotter*>(cost.get()))
    {
      plt->Plot(plotter, results.x);
    }
  }

  for (const sco::Constraint::Ptr& cnt : prob->getConstraints())
  {
    if (auto* plt = dynamic_cast<Plotter*>(cnt.get()))
    {
      plt->Plot(plotter, results.x);
    }
  }
  auto var_vec = prob->getVars();
  // This probably is/should be a utility somewhere
  VarArray var_array;
  var_array.m_data = var_vec;
  var_array.m_nCol = static_cast<int>(joint_names.size());
  var_array.m_nRow = static_cast<int>(var_vec.size()) / var_array.cols();

  auto traj = getTraj(results.x, var_array);
  tesseract_common::JointTrajectory joint_trajectory;
  for (long i = 0; i < traj.rows(); ++i)
    joint_trajectory.states.emplace_back(joint_names, traj.row(i));

  plotter->plotTrajectory(joint_trajectory, state_solver);
  plotter->waitForInput();
}

sco::Optimizer::Callback PlotProbCallback(const std::shared_ptr<tesseract_visualization::Visualization>& plotter,
                                          const tesseract_scene_graph::StateSolver& state_solver,
                                          const std::vector<std::string>& joint_names)
{
  return [plotter, &state_solver, &joint_names](sco::OptProb* opt_problem, sco::OptResults& opt_results) {
    PlotProb(plotter, state_solver, joint_names, opt_problem, opt_results);
  };
}

}  // namespace trajopt
