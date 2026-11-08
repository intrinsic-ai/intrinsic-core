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

#ifndef INTRINSIC_KINEMATICS_TESTING_TRAJECTORY_PLOT_H_
#define INTRINSIC_KINEMATICS_TESTING_TRAJECTORY_PLOT_H_

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/testing/undeclared/undeclared_outputs.h"

namespace intrinsic::icon {

// `TestingOptionsOrSaveDirectoryPath` is an `std::variant` containing necessary
// information to deduce the saving/logging/plotting directory path structure
// either in `bazel test` or in `bazel run` commands.
using TestingOptionsOrSaveDirectoryPath =
    std::variant<testing::Options, absl::string_view>;

constexpr testing::Options kHideCsvFile = {.hide_file = true};

// Options to customize the trajectory plots.
struct PlotOptions {
  // Marker type to use alongside the trajectory (i.e. "", "|", and "."). If
  // empty, no marker will be used.
  std::string marker_type = "";

  // If true, for multi-trajectory plots, all degrees of freedom within the same
  // trajectory share the same color, while different trajectories have
  // different colors.
  bool same_color_per_trajectory = false;
};

// Represents a joint trajectory with an associated name and optional start time
// offset, formatted for multi-trajectory plotting.
struct JointTrajectoryForPlotting {
  std::string name;
  std::vector<eigenmath::VectorNd> positions;
  std::vector<eigenmath::VectorNd> velocities;
  std::vector<eigenmath::VectorNd> accelerations;
  std::vector<eigenmath::VectorNd> jerks;
  std::vector<eigenmath::VectorNd> torques;
  std::vector<eigenmath::VectorNd> timestamps;
  // Offset added to timestamps when superimposing trajectories along a
  // timeline.
  absl::Duration time_offset = absl::ZeroDuration();
  // Optional duration within this window indicating when execution advances to
  // the next horizon iteration.
  std::optional<absl::Duration> advance_duration = std::nullopt;

  JointTrajectoryForPlotting() = default;

  JointTrajectoryForPlotting(
      absl::string_view trajectory_name, const JointTrajectoryPVA& traj,
      absl::Duration time_offset = absl::ZeroDuration(),
      std::optional<absl::Duration> advance_duration = std::nullopt);

  JointTrajectoryForPlotting(
      absl::string_view trajectory_name, const JointTrajectoryPVAJ& traj,
      absl::Duration time_offset = absl::ZeroDuration(),
      std::optional<absl::Duration> advance_duration = std::nullopt);

  JointTrajectoryForPlotting(
      absl::string_view trajectory_name, const JointTrajectoryPVAT& traj,
      absl::Duration time_offset = absl::ZeroDuration(),
      std::optional<absl::Duration> advance_duration = std::nullopt);

  JointTrajectoryForPlotting(
      absl::string_view trajectory_name, const JointTrajectoryPVAJT& traj,
      absl::Duration time_offset = absl::ZeroDuration(),
      std::optional<absl::Duration> advance_duration = std::nullopt);
};

// `CreateAndSavePlot` creates a Cartesian trajectory plot.
// `options_or_directory_path` needs to be supplied with either:
// (1) a `testing::Options` if executed in a `bazel test` command or in
//     a unit-test execution, or
// (2) an `absl::string_view` if executed in a `bazel run` command --with the
//     specified `absl::string_view` treated as the explicit directory path for
//     the saving/logging/plotting of the `bazel run` execution--.
// By default this function is considered to be called within a `bazel test`
// command or in a unit-test execution (i.e. case (1)), whereby the plot and CSV
// data files are attached to the test artifacts; however, if
// `options_or_directory_path` is specified as `kHideCsvFile`, then the csv
// files are not attached to the test artifacts. It stores the trajectory as csv
// files in the directory
// TEST_UNDECLARED_OUTPUTS_DIR/<test_id>/<trajectory_name>. It exists at test
// runtime, is uploaded to ObjFS when the test is finished and cleared when the
// test is rerun.
// The `plot_options` allow to specify customizing actions for the trajectory
// plots, such as using markers.
// Returns the directory where the files were saved in.
absl::StatusOr<std::string> CreateAndSavePlot(
    absl::Span<const CartStatePVA> traj, absl::Span<const double> t,
    absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());

// `CreateAndSavePlot` creates a joint trajectory plot.
// `options_or_directory_path` needs to be supplied with either:
// (1) a `testing::Options` if executed in a `bazel test` command or in
//     a unit-test execution, or
// (2) an `absl::string_view` if executed in a `bazel run` command --with the
//     specified `absl::string_view` treated as the explicit directory path for
//     the saving/logging/plotting of the `bazel run` execution--.
// By default this function is considered to be called within a `bazel test`
// command or in a unit-test execution (i.e. case (1)), whereby the plot and CSV
// data files are attached to the test artifacts; however, if
// `options_or_directory_path` is specified as `kHideCsvFile`, then the csv
// files are not attached to the test artifacts. It stores the trajectory as csv
// files in the directory
// TEST_UNDECLARED_OUTPUTS_DIR/<test_id>/<trajectory_name>. It exists at test
// runtime, is uploaded to ObjFS when the test is finished and cleared when the
// test is rerun.
// The `plot_options` allow to specify customizing actions for the trajectory
// plots, such as using markers.
// Returns the directory where the files were saved in.
absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVA& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());
// Same as above, but also plots the joint jerk.
absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVAJ& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());
// In addition to joint position, joint velocity and joint acceleration, it
// plots the corresponding joint torque.
absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVAT& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());
// Same as above, but also plots the joint jerk.
absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVAJT& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());

// `CreateAndSavePlot` creates a superimposed plot from multiple joint
// trajectories. It stores each trajectory in a separate subdirectory under
// TEST_UNDECLARED_OUTPUTS_DIR/<test_id>/<plot_name>/<trajectory_name>/ and
// generates a single superimposed plot.
// Returns the directory where the files were saved in.
absl::StatusOr<std::string> CreateAndSavePlot(
    absl::Span<const JointTrajectoryForPlotting> trajectories,
    absl::string_view plot_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());

// `CreateAndSavePlot` creates a plot from a float vector.
// `options_or_directory_path` needs to be supplied with either:
// (1) a `testing::Options` if executed in a `bazel test` command or in
//     a unit-test execution, or
// (2) an `absl::string_view` if executed in a `bazel run` command --with the
//     specified `absl::string_view` treated as the explicit directory path for
//     the saving/logging/plotting of the `bazel run` execution--.
// By default this function is considered to be called within a `bazel test`
// command or in a unit-test execution (i.e. case (1)), whereby the plot and CSV
// data files are attached to the test artifacts; however, if
// `options_or_directory_path` is specified as `kHideCsvFile`, then the csv
// files are not attached to the test artifacts. It stores the trajectory as csv
// files in the directory
// TEST_UNDECLARED_OUTPUTS_DIR/<test_id>/<name>. It exists at test
// runtime, is uploaded to ObjFS when the test is finished and cleared when the
// test is rerun.
// The values map can contain different data series which
// are all plotted as independent subplots. The subplots are ordered
// alphabetically by the key.
// Returns the directory where the files were saved in.
absl::StatusOr<std::string> CreateAndSavePlot(
    const absl::btree_map<std::string, std::vector<double>>& values,
    absl::string_view name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options());

// Variants of the line styles that can be selected for each curve in a plot.
// These line styles are based on matplotlib.
enum class LineStyle : char { kSolid, kDashed, kDashDot, kDotted };

// `ToString` maps the `LineStyle` enum to the string representation of the line
// style that will be passed to matplotlib's `plot` API. Please refer to
// https://matplotlib.org/stable/api/_as_gen/matplotlib.lines.Line2D.html#matplotlib.lines.Line2D.set_linestyle
// for the documentation.
inline absl::string_view ToString(LineStyle line_style) {
  switch (line_style) {
    case LineStyle::kSolid:
      return "-";  // solid line
    case LineStyle::kDashed:
      return "--";  // dashed line
    case LineStyle::kDashDot:
      return "-.";  // dash-dotted line
    case LineStyle::kDotted:
      return ":";  // dotted line
    default:
      return "None";  // draw nothing
  }
}

struct XYPathAndLineStyle {
  std::string path_name;

  std::vector<eigenmath::Vector2d> xy_path;

  LineStyle line_style;  // Line style to use for plotting the `xy_path`.
};

// Returns a circle path for plotting with a number of points. The circle is
// defined by the `center` and `radius`. The path is defined as a vector of 2D
// points in the form (x, y), which will be used to create an x-y plot of the
// circle. The number of points in the path is determined by the formula
// `reference_num_points` * `radius` / `reference_radius`, rounded up to the
// nearest integer. The purpose of exposing the `reference_radius` and
// `reference_num_points` is to allow for an intuitive parameterization of the
// circle data points (in the form of a path) that will render nicely when
// plotted, i.e. not too dense and not too sparse. In essence, this
// parameterization asks the user the question: "if `reference_radius` maps to
// `reference_num_points`, what should `radius` map to, in terms of the number
// of data points to be plotted?"
//
// `radius`, `reference_radius`, and `reference_num_points`
// must be strictly positive, otherwise the function will return a
// `kInvalidArgument` error.
absl::StatusOr<std::vector<eigenmath::Vector2d>> GetCirclePathForPlotting(
    const eigenmath::Vector2d& center, double radius,
    double reference_radius = 0.25, size_t reference_num_points = 50);

// `CreateAndSaveXYPlot` creates an x-y plot of one or several 2D paths
// (overlaid together), each stored in a vector of 2D points in the form (x, y)
// in `xy_paths_and_line_styles` alongside with its corresponding line style and
// `path_name` (to be used as the legend label).
// `options_or_directory_path` needs to be supplied with either:
// (1) a `testing::Options` if executed in a `bazel test` command or in
//     a unit-test execution, or
// (2) an `absl::string_view` if executed in a `bazel run` command --with the
//     specified `absl::string_view` treated as the explicit directory path for
//     the saving/logging/plotting of the `bazel run` execution--.
// By default this function is considered to be called within a `bazel test`
// command or in a unit-test execution (i.e. case (1)), whereby the plot and CSV
// data files are attached to the test artifacts; however, if
// `options_or_directory_path` is specified as `kHideCsvFile`, then the csv
// files are not attached to the test artifacts. It stores the trajectory as csv
// files in the directory
// TEST_UNDECLARED_OUTPUTS_DIR/<test_id>/<plot_name>. It exists at test
// runtime, is uploaded to ObjFS when the test is finished and cleared when the
// test is rerun.
// The `plot_options` allow to specify customizing actions for the trajectory
// plots, such as using markers.
// Returns the directory where the files were saved in.
absl::StatusOr<std::string> CreateAndSaveXYPlot(
    absl::Span<const XYPathAndLineStyle> xy_paths_and_line_styles,
    absl::string_view plot_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path =
        testing::Options(),
    const PlotOptions& plot_options = PlotOptions());

}  // namespace intrinsic::icon

#endif  // INTRINSIC_KINEMATICS_TESTING_TRAJECTORY_PLOT_H_
