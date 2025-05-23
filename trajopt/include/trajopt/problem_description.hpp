#pragma once
#include <trajopt_common/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <type_traits>
TRAJOPT_IGNORE_WARNINGS_POP

#include <tesseract_environment/fwd.h>
#include <tesseract_kinematics/core/fwd.h>
#include <tesseract_visualization/fwd.h>
#include <tesseract_collision/core/types.h>

#include <trajopt/typedefs.hpp>
#include <trajopt_common/collision_types.h>
#include <trajopt_common/utils.hpp>

#include <trajopt_sco/optimizers.hpp>
namespace sco
{
struct OptResults;
}  // namespace sco

namespace trajopt
{
using TrajOptRequest = Json::Value;
using TrajOptResponse = Json::Value;

struct ProblemConstructionInfo;

enum class TermType : char
{
  TT_INVALID = 0,     // 0000 0000
  TT_COST = 0x1,      // 0000 0001
  TT_CNT = 0x2,       // 0000 0010
  TT_USE_TIME = 0x4,  // 0000 0100
};

inline TermType operator|(TermType lhs, TermType rhs)
{
  using T = std::underlying_type_t<TermType>;
  return TermType(static_cast<T>(lhs) | static_cast<T>(rhs));
}

inline TermType operator&(TermType lhs, TermType rhs)
{
  using T = std::underlying_type_t<TermType>;
  return TermType(static_cast<T>(lhs) & static_cast<T>(rhs));
}

inline TermType operator^(TermType lhs, TermType rhs)
{
  using T = std::underlying_type_t<TermType>;
  return TermType(static_cast<T>(lhs) ^ static_cast<T>(rhs));
}

inline TermType operator~(TermType rhs)
{
  using T = std::underlying_type_t<TermType>;
  return TermType(~static_cast<T>(rhs));
}

#define DEFINE_CREATE(classname)                                                                                       \
  static TermInfo::Ptr create() { return std::make_shared<classname>(); }

/**
 * Holds all the data for a trajectory optimization problem
 * so you can modify it programmatically, e.g. add your own costs
 */
class TrajOptProb : public sco::OptProb
{
public:
  using Ptr = std::shared_ptr<TrajOptProb>;

  TrajOptProb();
  TrajOptProb(int n_steps, const ProblemConstructionInfo& pci);
  ~TrajOptProb() override = default;
  TrajOptProb(const TrajOptProb&) = delete;
  TrajOptProb& operator=(const TrajOptProb&) = delete;
  TrajOptProb(TrajOptProb&&) = default;
  TrajOptProb& operator=(TrajOptProb&&) = default;

  sco::VarVector GetVarRow(int i, int start_col, int num_col) { return m_traj_vars.rblock(i, start_col, num_col); }
  sco::VarVector GetVarRow(int i) { return m_traj_vars.row(i); }
  sco::Var& GetVar(int i, int j) { return m_traj_vars.at(i, j); }
  VarArray& GetVars() { return m_traj_vars; }
  /** @brief Returns the number of steps in the problem. This is the number of rows in the optimization matrix.*/
  int GetNumSteps() { return m_traj_vars.rows(); }
  /** @brief Returns the problem DOF. This is the number of columns in the optimization matrix.
   * Note that this is not necessarily the same as the kinematic DOF.*/
  int GetNumDOF() { return m_traj_vars.cols(); }
  std::shared_ptr<const tesseract_kinematics::JointGroup> GetKin() { return m_kin; }
  std::shared_ptr<const tesseract_environment::Environment> GetEnv() { return m_env; }
  void SetInitTraj(const TrajArray& x) { m_init_traj = x; }
  TrajArray GetInitTraj() { return m_init_traj; }
  friend TrajOptProb::Ptr ConstructProblem(const ProblemConstructionInfo&);
  /** @brief Returns TrajOptProb.has_time */
  bool GetHasTime() const { return has_time; }
  /** @brief Sets TrajOptProb.has_time  */
  void SetHasTime(bool tmp) { has_time = tmp; }

private:
  /** @brief If true, the last column in the optimization matrix will be 1/dt */
  bool has_time{ false };
  VarArray m_traj_vars;
  std::shared_ptr<const tesseract_kinematics::JointGroup> m_kin;
  std::shared_ptr<const tesseract_environment::Environment> m_env;
  TrajArray m_init_traj;
};

// void  SetupPlotting(TrajOptProb& prob, Optimizer& opt); TODO: Levi
// Fix

struct TrajOptResult
{
  using Ptr = std::shared_ptr<TrajOptResult>;

  std::vector<std::string> cost_names, cnt_names;
  DblVec cost_vals, cnt_viols;
  TrajArray traj;
  sco::OptStatus status;
  TrajOptResult(sco::OptResults& opt, TrajOptProb& prob);
};

struct BasicInfo
{
  /** @brief Number of time steps (rows) in the optimization matrix */
  int n_steps{ -1 };

  /** @brief The manipulator name */
  std::string manip;

  /**
   * @brief Timesteps at which to apply a fixed joint constraint
   * @details It binds the timestep to the value provided in the initial trajectory
   */
  IntVec fixed_timesteps;

  /**
   * @brief DOF(aka. Joint) to apply a fixed joint constraint for all timesteps
   * @details It binds the DOF(aka. Joint) to the value provided in the initial trajectory
   */
  IntVec fixed_dofs;

  /** @brief The convex solver to use */
  sco::ModelType convex_solver;

  /** @brief The convex solver configuration settings */
  sco::ModelConfig::Ptr convex_solver_config;

  /** @brief If true, the last column in the optimization matrix will be 1/dt */
  bool use_time = false;

  /** @brief The upper limit of 1/dt values allowed in the optimization*/
  double dt_upper_lim = 1.0;

  /** @brief The lower limit of 1/dt values allowed in the optimization*/
  double dt_lower_lim = 1.0;
};

/**
Initialization info read from json
*/
struct InitInfo
{
  /** @brief Methods of initializing the optimization matrix

    STATIONARY: Initializes all joint values to the initial value (the current value in the env
    pci.env->getCurrentJointValues)
    JOINT_INTERPOLATED: Linearly interpolates between initial value and the joint position specified in InitInfo.data
    GIVEN_TRAJ: Initializes the matrix to a given trajectory

    In all cases the dt column (if present) is appended the selected method is defined.
 */
  enum Type : std::uint8_t
  {
    STATIONARY,
    JOINT_INTERPOLATED,
    GIVEN_TRAJ,
  };
  /** @brief Specifies the type of initialization to use */
  Type type{ STATIONARY };
  /** @brief Data used during initialization. Use depends on the initialization selected. */
  TrajArray data;
  /** @brief Default value the final column of the optimization is initialized too if time is being used */
  double dt{ 1.0 };
};

struct MakesCost
{
};
struct MakesConstraint
{
};

/**
When cost or constraint element of JSON doc is read, one of these guys gets
constructed to hold the parameters.
Then it later gets converted to a Cost object by the hatch method
*/
struct TermInfo
{
  using Ptr = std::shared_ptr<TermInfo>;

  std::string name;
  TermType term_type{ TermType::TT_INVALID };
  TermType getSupportedTypes() const { return supported_term_types_; }
  virtual void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) = 0;
  virtual void hatch(TrajOptProb& prob) = 0;

  static TermInfo::Ptr fromName(const std::string& type);

  /**
   * Registers a user-defined TermInfo so you can use your own cost
   * see function RegisterMakers.cpp
   */
  using MakerFunc = std::shared_ptr<TermInfo> (*)();
  static void RegisterMaker(const std::string& type, MakerFunc);

  virtual ~TermInfo() = default;
  TermInfo(const TermInfo&) = default;
  TermInfo& operator=(const TermInfo&) = default;
  TermInfo(TermInfo&&) = default;
  TermInfo& operator=(TermInfo&&) = default;

protected:
  TermInfo(TermType supported_term_types) : supported_term_types_(supported_term_types) {}

private:
  static std::map<std::string, MakerFunc> name2maker;  // NOLINT
  TermType supported_term_types_;
};

/**
This object holds all the data that's read from the JSON document
*/
struct ProblemConstructionInfo
{
public:
  BasicInfo basic_info;
  sco::BasicTrustRegionSQPParameters opt_info;
  std::vector<TermInfo::Ptr> cost_infos;
  std::vector<TermInfo::Ptr> cnt_infos;
  InitInfo init_info;

  std::shared_ptr<const tesseract_environment::Environment> env;
  std::shared_ptr<const tesseract_kinematics::JointGroup> kin;

  std::vector<sco::Optimizer::Callback> callbacks;

  ProblemConstructionInfo(std::shared_ptr<const tesseract_environment::Environment> env) : env(std::move(env)) {}

  void fromJson(const Json::Value& v);

private:
  void readBasicInfo(const Json::Value& v);
  void readOptInfo(const Json::Value& v);
  void readCosts(const Json::Value& v);
  void readConstraints(const Json::Value& v);
  void readInitInfo(const Json::Value& v);
};

/** @brief User defined error function that is set as a constraint or cost for each timestep.
 *
 * The error function is required, but the jacobian is optional (nullptr).
 *
 * Error Function:
 *   arg: VectorXd will be all of the joint values for one timestep.
 *   return: VectorXd of violations for each joint. Anything != 0 will be a violation
 *
 * Error Function Jacobian:
 *   arg: VectorXd will be all of the joint values for one timestep.
 *   return: Eigen::MatrixXd that represents the change in the error function with respect to joint values
 */
struct UserDefinedTermInfo : public TermInfo
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  /** @brief The name prefix shown in output */
  std::string name = "UserDefined";

  /** @brief Timesteps over which to apply term */
  int first_step{ -1 }, last_step{ -1 };

  /** @brief Indicated if a step is fixed and its variables cannot be changed */
  std::vector<int> fixed_steps;

  /** @brief The user defined error function */
  sco::VectorOfVector::func error_function;

  /** @brief The user defined jocobian function */
  sco::MatrixOfVector::func jacobian_function;

  /** @brief If added as a cost it will use this penalty type */
  sco::PenaltyType cost_penalty_type{ sco::PenaltyType::SQUARED };

  /** @brief If added as a constraint it will use this constraint type */
  sco::ConstraintType constraint_type{ sco::ConstraintType::EQ };

  /** @brief Coefficients for user defined error function */
  Eigen::VectorXd coeff;

  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(UserDefinedTermInfo)

  /** @brief Initialize term with it's supported types */
  UserDefinedTermInfo() : TermInfo{ TermType::TT_COST | TermType::TT_CNT } {}
};

/** @brief This is used when the goal frame is not fixed in space */
struct DynamicCartPoseTermInfo : public TermInfo
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  /** @brief Timestep at which to apply term */
  int timestep{ 0 };
  /** @brief Coefficients for position and rotation */
  Eigen::Vector3d pos_coeffs, rot_coeffs;
  /** @brief Link which should reach desired pos */
  std::string source_frame;
  /** @brief The link relative to which the target_tcp is defined */
  std::string target_frame;
  /** @brief Static transform applied to the link_ location */
  Eigen::Isometry3d source_frame_offset;
  /** @brief A Static transform to be applied to target_ location */
  Eigen::Isometry3d target_frame_offset;
  /** @brief Distance below waypoint that is allowed. Should be size = 6. First 3 elements are dx, dy, dz. The last 3
   * elements are angle axis error allowed (Eigen::AngleAxisd.axis() * Eigen::AngleAxisd.angle()) */
  Eigen::VectorXd lower_tolerance;
  /** @brief Distance above waypoint that is allowed. Should be size = 6. First 3 elements are dx, dy, dz. The last 3
   * elements are angle axis error allowed (Eigen::AngleAxisd.axis() * Eigen::AngleAxisd.angle())*/
  Eigen::VectorXd upper_tolerance;

  /** @brief Error function for calculating the error in the position given the source and target positions
   * this defaults to tesseract_common::calcTransformError if unset*/
  std::function<Eigen::VectorXd(const Eigen::Isometry3d&, const Eigen::Isometry3d&)> error_function = nullptr;

  DynamicCartPoseTermInfo();

  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(DynamicCartPoseTermInfo)
};

/** @brief This term is used when the goal frame is fixed in cartesian space

  Set term_type == TermType::TT_COST or TermType::TT_CNT for cost or constraint.
*/
struct CartPoseTermInfo : public TermInfo
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  /** @brief Timestep at which to apply term */
  int timestep{ 0 };
  Eigen::Vector3d pos_coeffs, rot_coeffs;

  /** @brief Link which should reach desired pos */
  std::string source_frame;
  /** @brief The link relative to which the target_tcp is defined */
  std::string target_frame;
  /** @brief Static transform applied to the link_ location */
  Eigen::Isometry3d source_frame_offset;
  /** @brief A Static transform to be applied to target_ location */
  Eigen::Isometry3d target_frame_offset;
  /** @brief Distance below waypoint that is allowed. Should be size = 6. First 3 elements are dx, dy, dz. The last 3
   * elements are angle axis error allowed (Eigen::AngleAxisd.axis() * Eigen::AngleAxisd.angle()) */
  Eigen::VectorXd lower_tolerance;
  /** @brief Distance above waypoint that is allowed. Should be size = 6. First 3 elements are dx, dy, dz. The last 3
   * elements are angle axis error allowed (Eigen::AngleAxisd.axis() * Eigen::AngleAxisd.angle())*/
  Eigen::VectorXd upper_tolerance;

  /** @brief Error function for calculating the error in the position given the source and target positions
   * this defaults to tesseract_common::calcTransformError if unset*/
  std::function<Eigen::VectorXd(const Eigen::Isometry3d&, const Eigen::Isometry3d&)> error_function = nullptr;

  CartPoseTermInfo();

  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(CartPoseTermInfo)
};

/**
 \brief Applies cost/constraint to the cartesian velocity of a link

 Constrains the change in position of the link in each timestep to be less than
 max_displacement
 */
struct CartVelTermInfo : public TermInfo
{
  /** @brief Timesteps over which to apply term */
  int first_step{ -1 }, last_step{ -1 };
  /** @brief Link to which the term is applied */
  std::string link;
  double max_displacement{ 0 };
  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(CartVelTermInfo)

  /** @brief Initialize term with it's supported types */
  CartVelTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT) {}
};

/**
  \brief Joint space position cost
    Position operates on a single point (unlike velocity, etc). This is b/c the primary usecase is joint-space
    position waypoints

  \f{align*}{
  \sum_i c_i (x_i - xtarg_i)^2
  \f}
  where \f$i\f$ indexes over dof and \f$c_i\f$ are coeffs
 */
struct JointPosTermInfo : public TermInfo
{
  /** @brief Vector of coefficients that scale the cost. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec coeffs;
  /** @brief Vector of position targets. This is a required value. Size should be the DOF of the system */
  DblVec targets;
  /** @brief Vector of position upper limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec upper_tols;
  /** @brief Vector of position lower limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec lower_tols;
  /** @brief First time step to which the term is applied. Default: 0 */
  int first_step = 0;
  /** @brief Last time step to which the term is applied. Default: prob.GetNumSteps() - 1*/
  int last_step = -1;
  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(JointPosTermInfo)

  /** @brief Initialize term with it's supported types */
  JointPosTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT | TermType::TT_USE_TIME) {}
};

/**
@brief Used to apply cost/constraint to joint-space velocity

Term is applied to every step between first_step and last_step. It applies two limits, upper_limits/lower_limits,
to the joint velocity subject to the following cases.

* term_type = TermType::TT_COST
** upper_limit = lower_limit = 0 - Cost is applied with a SQUARED error scaled for each joint by coeffs
** upper_limit != lower_limit - 2 hinge costs are applied scaled for each joint by coeffs. If velocity < upper_limit and
velocity > lower_limit, no penalty.

* term_type = TermType::TT_CNT
** upper_limit = lower_limit = 0 - Equality constraint is applied
** upper_limit != lower_limit - 2 Inequality constraints are applied. These are both satisfied when velocity <
upper_limit and velocity > lower_limit

Note: coeffs, upper_limits, and lower_limits are optional. If a term is not given it will default to 0 for all joints.
If one value is given, this will be broadcast to all joints.

Note: Velocity is calculated numerically using forward finite difference

\f{align*}{
  cost = \sum_{t=0}^{T-2} \sum_j c_j (x_{t+1,j} - x_{t,j})^2
\f}
where j indexes over DOF, and \f$c_j\f$ are the coeffs.
*/
struct JointVelTermInfo : public TermInfo
{
  /** @brief Vector of coefficients that scale the cost. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec coeffs;
  /** @brief Vector of velocity targets. This is a required value. Size should be the DOF of the system */
  DblVec targets;
  /** @brief Vector of velocity upper limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec upper_tols;
  /** @brief Vector of velocity lower limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec lower_tols;
  /** @brief First time step to which the term is applied. Default: 0*/
  int first_step = 0;
  /** @brief Last time step to which the term is applied. Default: prob.GetNumSteps() - 1*/
  int last_step = -1;
  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(JointVelTermInfo)

  /** @brief Initialize term with it's supported types */
  JointVelTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT | TermType::TT_USE_TIME) {}
};

/**
@brief Used to apply cost/constraint to joint-space acceleration

Term is applied to every step between first_step and last_step. It applies two limits, upper_limits/lower_limits,
to the joint velocity subject to the following cases.

* term_type = TermType::TT_COST
** upper_limit = lower_limit = 0 - Cost is applied with a SQUARED error scaled for each joint by coeffs
** upper_limit != lower_limit - 2 hinge costs are applied scaled for each joint by coeffs. If acceleration < upper_limit
and acceleration > lower_limit, no penalty.

* term_type = TermType::TT_CNT
** upper_limit = lower_limit = 0 - Equality constraint is applied
** upper_limit != lower_limit - 2 Inequality constraints are applied. These are both satisfied when acceleration <
upper_limit and acceleration > lower_limit

Note: coeffs, upper_limits, and lower_limits are optional. If a term is not given it will default to 0 for all joints.
If one value is given, this will be broadcast to all joints.

Note: Acceleration is calculated numerically using central finite difference
*/
struct JointAccTermInfo : public TermInfo
{
  /** @brief Vector of coefficients that scale the cost. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec coeffs;
  /** @brief Vector of accel targets. This is a required value. Size should be the DOF of the system */
  DblVec targets;
  /** @brief Vector of accel upper limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec upper_tols;
  /** @brief Vector of accel lower limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec lower_tols;
  /** @brief First time step to which the term is applied. Default: 0 */
  int first_step = 0;
  /** @brief Last time step to which the term is applied. Default: prob.GetNumSteps() - 1 */
  int last_step = -1;
  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(JointAccTermInfo)

  /** @brief Initialize term with it's supported types */
  JointAccTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT) {}
};

/**
@brief Used to apply cost/constraint to joint-space jerk

Term is applied to every step between first_step and last_step. It applies two limits, upper_limits/lower_limits,
to the joint velocity subject to the following cases.

* term_type = TermType::TT_COST
** upper_limit = lower_limit = 0 - Cost is applied with a SQUARED error scaled for each joint by coeffs
** upper_limit != lower_limit - 2 hinge costs are applied scaled for each joint by coeffs. If jerk < upper_limit and
jerk > lower_limit, no penalty.

* term_type = TermType::TT_CNT
** upper_limit = lower_limit = 0 - Equality constraint is applied
** upper_limit != lower_limit - 2 Inequality constraints are applied. These are both satisfied when jerk <
upper_limit and jerk > lower_limit

Note: coeffs, upper_limits, and lower_limits are optional. If a term is not given it will default to 0 for all joints.
If one value is given, this will be broadcast to all joints.

Note: Jerk is calculated numerically using central finite difference
*/
struct JointJerkTermInfo : public TermInfo
{
  /** @brief Vector of coefficients that scale the cost. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec coeffs;
  /** @brief Vector of jerk targets. This is a required value. Size should be the DOF of the system */
  DblVec targets;
  /** @brief Vector of jerk upper limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec upper_tols;
  /** @brief Vector of jerk lower limits. Size should be the DOF of the system. Default: vector of 0's*/
  DblVec lower_tols;
  /** @brief First time step to which the term is applied. Default: 0 */
  int first_step = 0;
  /** @brief Last time step to which the term is applied. Default: prob.GetNumSteps() - 1 */
  int last_step = -1;
  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(JointJerkTermInfo)

  /** @brief Initialize term with it's supported types */
  JointJerkTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT) {}
};

/**
\brief %Collision penalty

Distrete-time penalty:
\f{align*}{
  cost = \sum_{t=0}^{T-1} \sum_{A, B} | distpen_t - sd(A,B) |^+
\f}

Continuous-time penalty: same, except you consider swept-out shaps of robot
links. Currently self-collisions are not included.

*/
struct CollisionTermInfo : public TermInfo
{
  /** @brief first_step and last_step are inclusive */
  int first_step{ -1 }, last_step{ -1 };

  /** @brief Indicated if a step is fixed and its variables cannot be changed */
  std::vector<int> fixed_steps;

  /** @brief The collision checking configuration */
  trajopt_common::TrajOptCollisionConfig config;

  /** @brief Used to add term to pci from json */
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  /** @brief Converts term info into cost/constraint and adds it to trajopt problem */
  void hatch(TrajOptProb& prob) override;
  DEFINE_CREATE(CollisionTermInfo)

  CollisionTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT) {}
};

/**
 * @brief Applies a penalty to the total time taken by the trajectory
 */
struct TotalTimeTermInfo : public TermInfo
{
  /** @brief Scales this term. */
  double coeff = 1.0;
  /** @brief If non zero, penalty type will be a hinge on values greater than this limit relative to the target. */
  double limit = 0.0;

  void hatch(TrajOptProb& prob) override;
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  DEFINE_CREATE(TotalTimeTermInfo)

  TotalTimeTermInfo() : TermInfo(TermType::TT_COST | TermType::TT_CNT | TermType::TT_USE_TIME) {}
};

/**
 * @brief Applies a cost to avoid kinematic singularities
 */
struct AvoidSingularityTermInfo : public TermInfo
{
  /** @brief The forward kinematics solver used to calculate the jacobian for which to do singularity avoidance */
  std::shared_ptr<const tesseract_kinematics::JointGroup> subset_kin_;
  /** @brief Damping factor used to prevent numerical instability in the singularity avoidance cost as the smallest
   * singular value approaches zero */
  double lambda{ 0.1 };
  /** @brief The robot link with which to calculate the robot jacobian (required because of kinematic trees) */
  std::string link;
  int first_step{ -1 };
  int last_step{ -1 };
  DblVec coeffs;

  void hatch(TrajOptProb& prob) override;
  void fromJson(ProblemConstructionInfo& pci, const Json::Value& v) override;
  DEFINE_CREATE(AvoidSingularityTermInfo)

  AvoidSingularityTermInfo(std::shared_ptr<const tesseract_kinematics::JointGroup> subset_kin = nullptr,
                           double lambda_ = 0.1)
    : TermInfo(TermType::TT_COST | TermType::TT_CNT), subset_kin_(std::move(subset_kin)), lambda(lambda_)
  {
  }
};

TrajOptProb::Ptr ConstructProblem(const ProblemConstructionInfo&);
TrajOptProb::Ptr ConstructProblem(const Json::Value&,
                                  const std::shared_ptr<const tesseract_environment::Environment>& env);
TrajOptResult::Ptr OptimizeProblem(const TrajOptProb::Ptr&,
                                   const std::shared_ptr<tesseract_visualization::Visualization>& plotter = nullptr);

}  // namespace trajopt
