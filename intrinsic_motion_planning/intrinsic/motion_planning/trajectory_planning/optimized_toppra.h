// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_OPTIMIZED_TOPPRA_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_OPTIMIZED_TOPPRA_H_

#include <optional>
#include <vector>

#include "Eigen/Core"
#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_options.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_solver_commons.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.h"

namespace intrinsic::topp {

// Structure to store internal TOPP-RA data. Primarily used for debugging and
// inspecting intermediate linear programming steps.
struct ToppRAStorage {
  // Number of iterations required by the solver to construct a feasible
  // solution that strictly satisfies the limits.
  int num_iterations;

  // Squared path velocities (the reachable phase-space profile) constructed
  // during the forward pass.
  std::vector<double> squared_path_velocities;

  // Precomputed coefficients and Jacobians for Cartesian constraints.
  // These are constructed only if Cartesian velocity or Cartesian acceleration
  // constraints are explicitly enabled in the optimization options.
  std::optional<CartesianConstraintsDotProducts>
      cartesian_constraints_dot_products = std::nullopt;
};

// Implementation of Time-Optimal Path Parameterization based on Reachability
// Analysis (TOPP-RA).
//
// This class takes a predefined geometric path (represented as discrete
// samples) and computes a continuous time parameterization that minimizes the
// total traversal time while strictly adhering to system limits (joint
// velocity/acceleration/torque, and Cartesian velocity/acceleration).
//
// Reference: "A New Approach to Time-Optimal Path Parameterization Based on
// Reachability Analysis", Hung Pham and Quang-Cuong Pham, IEEE TRO, 2018.
class ToppRA {
 public:
  using RowMatrixXd = eigenmath::Matrix<double, Eigen::Dynamic, Eigen::Dynamic,
                                        Eigen::RowMajor>;

  // Constructs the TOPP-RA solver.
  //
  // Parameters:
  //  * `chain`: The kinematic chain used to compute Jacobians for Cartesian
  //    constraints. It must outlive the solver.
  //  * `dynamics`: The rigid body dynamics interface. Required only if dynamic
  //    (torque) constraints are activated in the options. May be nullptr
  //    otherwise. It must outlive the solver.
  //  * `options`: Configuration options defining the active kinematic, dynamic,
  //    and Cartesian constraints for the solver. Per-frame Cartesian velocity
  //    limits (`PathSample::frame_cartesian_limits`) are only applied when
  //    `activate_cartesian_velocity_constraints` is enabled.
  explicit ToppRA(
      const kinematics::Chain* chain ABSL_ATTRIBUTE_LIFETIME_BOUND = nullptr,
      icon::RigidBodyInterface* dynamics ABSL_ATTRIBUTE_LIFETIME_BOUND =
          nullptr,
      const OptimizationOptions& options = DefaultToppOptions())
      : chain_(chain), dynamics_(dynamics), options_(options) {}

  // Solves the time-optimal path parameterization problem for the given path.
  //
  // This method applies an iterative approach: it scales limits down slightly
  // upon constraint violations and re-solves (up to a maximum number of
  // attempts). This ensures the resulting trajectory strictly satisfies the
  // given limits despite the numerical discretization errors inherent to the
  // TOPP-RA algorithm.
  //
  // Parameters:
  //  * path_samples: A span of consecutive path states containing positions,
  //    derivatives w.r.t the path parameter (`qp`, `qpp`), and applicable
  //    limits at each discrete point. Note that additional monitored frame
  //    limits (`PathSample::frame_cartesian_limits`) are only applied for
  //    Cartesian velocity, and only when
  //    `activate_cartesian_velocity_constraints` is enabled in `options`.
  //
  // Returns:
  //  * A `ToppTrajectoryResult` containing the time-parameterized joint
  //    trajectory and the continuous squared path velocity profile. Returns
  //    an error status if the path cannot be parameterized under the given
  //    limits or if inputs are inconsistent.
  absl::StatusOr<ToppTrajectoryResult> Solve(
      absl::Span<const PathSample> path_samples);

  // Returns intermediate internal computation data from the most recent run.
  const ToppRAStorage& GetInternalData() const { return data_; }

 private:
  // Implements the core TOPP-RA algorithm to solve for the velocities,
  // accelerations, and times of discrete points on an underlying continuous
  // path, such that the time required to traverse the path is minimized while
  // respecting all velocity and acceleration limits.
  //
  // This implementation uses interpolated acceleration constraints as described
  // in Appendix D of the original paper to significantly reduce acceleration
  // limit violations between waypoints.
  //
  // Definitions:
  //  * s(t): A twice-differentiable function that maps times to phases. The
  //    TOPP-RA algorithm outputs a discrete approximation of this mapping.
  //  * q(s): A twice-differentiable function that maps phases to path
  //    positions, where positions can have any number of dimensions.
  //
  // Parameters:
  //  * path_samples [INPUT]: Complete definition of the time-optimal path
  //    planning problem, including path values, derivatives up to third order
  //    with respect to the path variable, joint/Cartesian limits (where
  //    `frame_cartesian_limits` are only applied when Cartesian velocity
  //    constraints are enabled in `options_`), and tip target transformations
  //    used for Cartesian constraints.
  //
  // Returns `true` if a feasible reachability profile is found, `false`
  // otherwise. Note: This method computes accelerations over grid point
  // intervals, so there is one fewer acceleration row than there are path
  // samples.
  absl::StatusOr<bool> TimeParametrizePath(
      absl::Span<const PathSample> path_samples);

 private:
  // The robot's kinematic chain used to compute Jacobians at different poses.
  const kinematics::Chain* chain_ = nullptr;
  // The robot's rigid body dynamics interface used to formulate constraints
  // based on the equations of motion.
  icon::RigidBodyInterface* dynamics_;
  // Parameterization of the solver solution type.
  OptimizationOptions options_;
  // Storage for intermediate quantities useful for debugging.
  ToppRAStorage data_;
};

}  // namespace intrinsic::topp

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_OPTIMIZED_TOPPRA_H_
