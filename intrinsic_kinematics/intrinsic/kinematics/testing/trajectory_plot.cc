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

#include "intrinsic/kinematics/testing/trajectory_plot.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/eigen_file_export.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/testing/undeclared/undeclared_outputs.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/subprocess.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "rules_cc/cc/runfiles/runfiles.h"

namespace intrinsic::icon {
namespace {

using rules_cc::cc::runfiles::Runfiles;

constexpr absl::string_view kTrajectoryPlotterPath =
    "intrinsic_kinematics/intrinsic/kinematics/testing/"
    "trajectory_plotter";
constexpr absl::string_view kTimeseriesPlotterPath =
    "intrinsic_kinematics/intrinsic/kinematics/testing/timeseries_plot";
constexpr absl::string_view kXYPlotterPath =
    "intrinsic_kinematics/intrinsic/kinematics/testing/xy_plotter";

template <typename T>
void SaveToCsv(const std::vector<T>& data, absl::string_view path) {
  eigenmath::VectorToFile(
      eigenmath::EigenFileExport::CSVFormat(Eigen::FullPrecision), path, data,
      /*transpose_eigen_type=*/true);
}

JointTrajectoryForPlotting ToPlottingTrajectory(
    absl::Span<const CartStatePVA> traj, absl::Span<const double> timestamps,
    absl::string_view trajectory_name = "") {
  JointTrajectoryForPlotting plotting_traj;
  plotting_traj.name = std::string(trajectory_name);
  plotting_traj.positions.reserve(traj.size());
  plotting_traj.velocities.reserve(traj.size());
  plotting_traj.accelerations.reserve(traj.size());
  plotting_traj.timestamps.reserve(traj.size());
  for (size_t i = 0; i < traj.size(); ++i) {
    plotting_traj.positions.push_back(traj[i].pose.translation());
    plotting_traj.velocities.push_back(traj[i].velocity);
    plotting_traj.accelerations.push_back(traj[i].acceleration);
    plotting_traj.timestamps.push_back(eigenmath::VectorNd::Ones(1) *
                                       timestamps[i]);
  }
  return plotting_traj;
}

// Save a vectorized physical quantity `vectorized_physical_quantity` (of name
// `physical_quantity_name` and description `physical_quantity_description`) of
// a trajectory named `trajectory_name` to a CSV (Comma-Separated Values) file,
// from either within a test (`bazel test`) or within a run (`bazel run`).
// If called from within a test (`bazel test`), then the `testing_options` must
// be specified, otherwise (if called from within a run (`bazel run`) then) the
// `running_directory_path` must be specified (where the CSV file will be saved
// under the directory `running_directory_path`/`trajectory_name`/).
absl::StatusOr<std::string> SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
    absl::string_view trajectory_name, absl::string_view physical_quantity_name,
    absl::string_view physical_quantity_description,
    std::optional<testing::Options> testing_options,
    std::optional<std::string> running_directory_path,
    const std::vector<eigenmath::VectorNd>& vectorized_physical_quantity) {
  std::string filename = "";
  if (testing_options.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        filename,
        testing::PrepareUndeclaredOutput(
            absl::StrCat(trajectory_name, "/", physical_quantity_name, ".csv"),
            physical_quantity_description, "text/csv",
            testing_options.value()));
  } else {
    filename = absl::StrCat(running_directory_path.value(), "/",
                            physical_quantity_name, ".csv");
  }
  SaveToCsv<eigenmath::VectorNd>(vectorized_physical_quantity, filename);

  return filename;
}

absl::StatusOr<std::string> SaveTrajectory(
    const JointTrajectoryForPlotting& traj, absl::string_view trajectory_name,
    std::optional<testing::Options> testing_options,
    std::optional<std::string> running_directory_path) {
  INTR_ASSIGN_OR_RETURN(
      std::string position_file,
      SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
          trajectory_name, /*physical_quantity_name=*/"positions",
          /*physical_quantity_description=*/"Joint positions", testing_options,
          running_directory_path,
          /*vectorized_physical_quantity=*/traj.positions));

  INTR_ASSIGN_OR_RETURN(
      std::string velocities_file,
      SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
          trajectory_name, /*physical_quantity_name=*/"velocities",
          /*physical_quantity_description=*/"Joint velocities", testing_options,
          running_directory_path,
          /*vectorized_physical_quantity=*/traj.velocities));

  INTR_ASSIGN_OR_RETURN(
      std::string accelerations_file,
      SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
          trajectory_name, /*physical_quantity_name=*/"accelerations",
          /*physical_quantity_description=*/"Joint accelerations",
          testing_options, running_directory_path,
          /*vectorized_physical_quantity=*/traj.accelerations));

  if (!traj.jerks.empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::string jerks_file,
        SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
            trajectory_name, /*physical_quantity_name=*/"jerks",
            /*physical_quantity_description=*/"Joint jerks", testing_options,
            running_directory_path,
            /*vectorized_physical_quantity=*/traj.jerks));
  }

  if (!traj.torques.empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::string torques_file,
        SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
            trajectory_name, /*physical_quantity_name=*/"torques",
            /*physical_quantity_description=*/"Joint torques", testing_options,
            running_directory_path,
            /*vectorized_physical_quantity=*/traj.torques));
  }

  INTR_ASSIGN_OR_RETURN(
      std::string timestamps_file,
      SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
          trajectory_name, /*physical_quantity_name=*/"timestamps",
          /*physical_quantity_description=*/"Joint timestamps", testing_options,
          running_directory_path,
          /*vectorized_physical_quantity=*/traj.timestamps));

  if (traj.advance_duration.has_value()) {
    const double advance_timestamp_sec =
        absl::ToDoubleSeconds(traj.time_offset + traj.advance_duration.value());
    INTR_ASSIGN_OR_RETURN(
        std::string advance_file,
        SaveVectorizedPhysicalQuantityOfTrajectoryToCsv(
            trajectory_name,
            /*physical_quantity_name=*/"advance_timestamp",
            /*physical_quantity_description=*/"Advance timestamp",
            testing_options, running_directory_path,
            /*vectorized_physical_quantity=*/
            {eigenmath::VectorNd::Ones(1) * advance_timestamp_sec}));
  }
  return position_file;
}

absl::StatusOr<std::string> SaveTrajectory(
    const JointTrajectoryForPlotting& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  const bool holds_testing_options =
      std::holds_alternative<testing::Options>(options_or_directory_path);
  std::optional<testing::Options> testing_options = std::nullopt;
  std::optional<std::string> running_directory_path = std::nullopt;
  if (holds_testing_options) {
    testing_options = std::get<testing::Options>(options_or_directory_path);
  } else {
    running_directory_path = file::CleanPath(
        file::JoinPath(std::get<absl::string_view>(options_or_directory_path),
                       trajectory_name));
    INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(
        running_directory_path.value(), file::Defaults()));
  }

  INTR_ASSIGN_OR_RETURN(std::string position_file,
                        SaveTrajectory(traj, trajectory_name, testing_options,
                                       running_directory_path));

  return file::CleanPath(file::JoinPath(file::Dirname(position_file), ".."));
}

absl::StatusOr<std::string> SaveTrajectories(
    absl::Span<const JointTrajectoryForPlotting> trajectories,
    absl::string_view plot_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  const bool holds_testing_options =
      std::holds_alternative<testing::Options>(options_or_directory_path);
  std::optional<testing::Options> testing_options = std::nullopt;
  std::optional<std::string> running_directory_path = std::nullopt;
  if (holds_testing_options) {
    testing_options = std::get<testing::Options>(options_or_directory_path);
  } else {
    running_directory_path = file::CleanPath(file::JoinPath(
        std::get<absl::string_view>(options_or_directory_path), plot_name));
    INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(
        running_directory_path.value(), file::Defaults()));
  }

  std::string position_file;
  for (const auto& traj : trajectories) {
    const std::string traj_name = absl::StrCat(plot_name, "/", traj.name);
    std::optional<std::string> traj_running_dir = std::nullopt;
    if (!holds_testing_options) {
      traj_running_dir = file::CleanPath(
          file::JoinPath(running_directory_path.value(), traj.name));
      INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(traj_running_dir.value(),
                                                      file::Defaults()));
    }

    INTR_ASSIGN_OR_RETURN(
        position_file,
        SaveTrajectory(traj, traj_name, testing_options, traj_running_dir));
  }

  return file::CleanPath(
      file::JoinPath(file::Dirname(file::Dirname(position_file)), ".."));
}

absl::StatusOr<std::string> SaveTrajectory(
    const JointTrajectoryPVA& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  return SaveTrajectory(JointTrajectoryForPlotting(trajectory_name, traj),
                        trajectory_name, options_or_directory_path);
}

absl::StatusOr<std::string> SaveTrajectory(
    const JointTrajectoryPVAJ& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  return SaveTrajectory(JointTrajectoryForPlotting(trajectory_name, traj),
                        trajectory_name, options_or_directory_path);
}

absl::StatusOr<std::string> SaveTrajectory(
    const JointTrajectoryPVAT& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  return SaveTrajectory(JointTrajectoryForPlotting(trajectory_name, traj),
                        trajectory_name, options_or_directory_path);
}

absl::StatusOr<std::string> SaveTrajectory(
    const JointTrajectoryPVAJT& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  return SaveTrajectory(JointTrajectoryForPlotting(trajectory_name, traj),
                        trajectory_name, options_or_directory_path);
}

absl::StatusOr<std::string> SaveTrajectory(
    absl::Span<const CartStatePVA> traj, absl::Span<const double> timestamps,
    absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  return SaveTrajectory(ToPlottingTrajectory(traj, timestamps, trajectory_name),
                        trajectory_name, options_or_directory_path);
}

// Prepare the directory structure and get the saving file path of a file with
// name `file_name`, description `file_description`, content type
// `file_content_type` (e.g. "image/png" or "text/csv"), and extension
// `file_extension` (e.g. ".png" or ".csv"), from either within a test
// (`bazel test`) or within a run (`bazel run`) as determined by the
// specification of `options_or_directory_path`.
// If called from within a test (`bazel test`), then the
// `options_or_directory_path` must specify a `testing::Options`, otherwise
// (if called from within a run (`bazel run`) then) the
// `options_or_directory_path` must specify an `absl::string_view` (where the
// file will be saved under the directory
// `options_or_directory_path`/`file_name`/).
absl::StatusOr<std::string> PrepareAndGetSavingFilepath(
    absl::string_view file_name, absl::string_view file_description,
    absl::string_view file_content_type, absl::string_view file_extension,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  std::string file_path = "";
  if (std::holds_alternative<testing::Options>(options_or_directory_path)) {
    INTR_ASSIGN_OR_RETURN(
        file_path, testing::PrepareUndeclaredOutput(
                       absl::StrCat(file_name, "/", file_name, file_extension),
                       file_description, file_content_type));
  } else {
    std::string running_directory_path = file::CleanPath(file::JoinPath(
        std::get<absl::string_view>(options_or_directory_path), file_name));
    INTR_RETURN_IF_ERROR(
        file::RecursivelyCreateDir(running_directory_path, file::Defaults()));
    file_path =
        absl::StrCat(running_directory_path, "/", file_name, file_extension);
  }
  return file_path;
}

// Adds the required runfiles environment variables to run a subprocess also
// build with Bazel.
absl::Status AugmentSubprocessWithRunfilesEnvVars(Subprocess& process) {
  std::string error = "";
  auto runfiles = std::unique_ptr<Runfiles>(
      Runfiles::Create(program_invocation_name, &error));
  if (runfiles == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failure to create runfiles: ", error));
  }
  for (const auto& env_var : runfiles->EnvVars()) {
    INTR_RETURN_IF_ERROR(process.SetEnv(env_var.first, env_var.second));
  }
  return absl::OkStatus();
}

absl::Status CreatePlot(
    absl::string_view data_dir, absl::string_view trajectory_name,
    absl::string_view type, absl::string_view marker_type,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  Subprocess process;

  std::string binary =
      PathResolver::ResolveRunfilesPathForTest(kTrajectoryPlotterPath);

  INTR_ASSIGN_OR_RETURN(
      std::string filepath,
      PrepareAndGetSavingFilepath(
          /*file_name=*/trajectory_name,
          /*file_description=*/absl::StrCat(trajectory_name, " plot"),
          /*file_content_type=*/"image/png",
          /*file_extension=*/".png", options_or_directory_path));

  std::vector<std::string> args = {
      binary,
      "--data_path",
      std::string(data_dir),
      "--fig_output_path",
      filepath,
      "--trajectory_name",
      std::string(trajectory_name),
      "--trajectory_type",
      std::string(type),
      "--marker_type",
      std::string(marker_type),
  };
  process.SetStderrFd(STDERR_FILENO);
  process.SetStdoutFd(STDOUT_FILENO);
  INTR_RETURN_IF_ERROR(AugmentSubprocessWithRunfilesEnvVars(process));
  process.SetProgram(binary, args);
  LOG(INFO) << "Started trajectory plotter.";
  INTR_RETURN_IF_ERROR(process.Start());
  INTR_RETURN_IF_ERROR(process.Wait());
  if (process.exit_code() != 0) {
    return absl::InternalError("Failed to plot trajectory.");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> SaveTimeseries(
    const absl::btree_map<std::string, std::vector<double>>& values,
    absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  INTR_ASSIGN_OR_RETURN(
      std::string filepath,
      PrepareAndGetSavingFilepath(
          /*file_name=*/trajectory_name,
          /*file_description=*/"Timeseries data",
          /*file_content_type=*/"text/csv",
          /*file_extension=*/".csv", options_or_directory_path));

  std::ofstream file((std::string(filepath)));

  size_t num_points = 0;
  int column_index = 0;
  for (const auto& [name, data] : values) {
    if (column_index == 0) {
      num_points = data.size();
    } else if (num_points != data.size()) {
      return absl::InvalidArgumentError(
          "All data series must have the same length.");
    }
    file << name;
    if (column_index < values.size() - 1) {
      file << ",";
    }
    ++column_index;
  }
  file << "\n";

  for (size_t i = 0; i < num_points; i++) {
    column_index = 0;
    for (const auto& [name, data] : values) {
      file << data.at(i);
      if (column_index < values.size() - 1) {
        file << ",";
      }
      ++column_index;
    }
    file << "\n";
  }
  file.close();

  return filepath;
}

absl::StatusOr<std::string> CreateTimeSeriesPlot(
    absl::string_view data_file, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  Subprocess process;
  std::string binary =
      PathResolver::ResolveRunfilesPathForTest(kTimeseriesPlotterPath);

  INTR_ASSIGN_OR_RETURN(
      std::string filepath,
      PrepareAndGetSavingFilepath(
          /*file_name=*/trajectory_name,
          /*file_description=*/
          absl::StrCat(trajectory_name, " timeseries plot"),
          /*file_content_type=*/"image/png",
          /*file_extension=*/".png", options_or_directory_path));

  std::vector<std::string> args = {
      binary,
      "--csv_file_path",
      std::string(data_file),
      "--fig_output_path",
      std::string(filepath),
  };
  process.SetStderrFd(STDERR_FILENO);
  process.SetStdoutFd(STDOUT_FILENO);
  INTR_RETURN_IF_ERROR(AugmentSubprocessWithRunfilesEnvVars(process));
  process.SetProgram(binary, args);
  INTR_RETURN_IF_ERROR(process.Start());
  INTR_RETURN_IF_ERROR(process.Wait());
  if (process.exit_code() != 0) {
    return absl::InternalError("Failed to plot trajectory.");
  }

  return filepath;
}

absl::StatusOr<std::string> SaveXYPaths(
    absl::Span<const XYPathAndLineStyle> xy_paths_and_line_styles,
    absl::string_view plot_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  const bool holds_testing_options =
      std::holds_alternative<testing::Options>(options_or_directory_path);
  std::optional<testing::Options> testing_options = std::nullopt;
  std::optional<std::string> running_directory_path = std::nullopt;
  if (holds_testing_options) {
    testing_options = std::get<testing::Options>(options_or_directory_path);
  } else {
    running_directory_path = file::CleanPath(file::JoinPath(
        std::get<absl::string_view>(options_or_directory_path), plot_name));
    INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(
        running_directory_path.value(), file::Defaults()));
  }

  std::string position_file = "";
  for (const XYPathAndLineStyle& xy_path_and_line_style :
       xy_paths_and_line_styles) {
    if (holds_testing_options) {
      INTR_ASSIGN_OR_RETURN(
          position_file,
          testing::PrepareUndeclaredOutput(
              absl::StrCat(plot_name, "/", xy_path_and_line_style.path_name,
                           ".csv"),
              absl::StrCat("X-Y path ", xy_path_and_line_style.path_name),
              "text/csv", testing_options.value()));
    } else {
      position_file = absl::StrCat(running_directory_path.value(), "/",
                                   xy_path_and_line_style.path_name, ".csv");
    }
    SaveToCsv<eigenmath::Vector2d>(xy_path_and_line_style.xy_path,
                                   position_file);
  }

  return file::CleanPath(file::JoinPath(file::Dirname(position_file), ".."));
}

absl::StatusOr<std::string> CreateXYPlot(
    absl::string_view data_dir, absl::string_view plot_name,
    absl::string_view path_names, absl::string_view line_styles,
    absl::string_view marker_type,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  Subprocess process;

  std::string binary = PathResolver::ResolveRunfilesPathForTest(kXYPlotterPath);

  INTR_ASSIGN_OR_RETURN(
      std::string filepath,
      PrepareAndGetSavingFilepath(
          /*file_name=*/plot_name,
          /*file_description=*/
          absl::StrCat(plot_name, " X-Y plot"),
          /*file_content_type=*/"image/png",
          /*file_extension=*/".png", options_or_directory_path));

  std::vector<std::string> args = {
      binary,
      "--data_path",
      std::string(data_dir),
      "--fig_output_path",
      filepath,
      "--plot_name",
      std::string(plot_name),
      "--path_names",
      std::string(path_names),
      "--line_styles",
      std::string(line_styles),
      "--marker_type",
      std::string(marker_type),
  };
  process.SetStderrFd(STDERR_FILENO);
  process.SetStdoutFd(STDOUT_FILENO);
  INTR_RETURN_IF_ERROR(AugmentSubprocessWithRunfilesEnvVars(process));
  process.SetProgram(binary, args);
  LOG(INFO) << "Started X-Y plotter.";
  INTR_RETURN_IF_ERROR(process.Start());
  INTR_RETURN_IF_ERROR(process.Wait());
  if (process.exit_code() != 0) {
    return absl::InternalError("Failed to plot X-Y paths.");
  }

  return filepath;
}

absl::Status CreateMultiTrajectoryPlot(
    absl::string_view data_dir, absl::string_view plot_name,
    absl::Span<const std::string> trajectory_names,
    const PlotOptions& plot_options,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  Subprocess process;

  std::string binary =
      PathResolver::ResolveRunfilesPathForTest(kTrajectoryPlotterPath);

  INTR_ASSIGN_OR_RETURN(
      std::string filepath,
      PrepareAndGetSavingFilepath(
          /*file_name=*/plot_name,
          /*file_description=*/absl::StrCat(plot_name, " plot"),
          /*file_content_type=*/"image/png",
          /*file_extension=*/".png", options_or_directory_path));

  std::string trajectory_names_str = absl::StrJoin(trajectory_names, ",");

  std::vector<std::string> args = {
      binary,
      "--data_path",
      std::string(data_dir),
      "--fig_output_path",
      filepath,
      "--plot_name",
      std::string(plot_name),
      "--trajectory_names",
      trajectory_names_str,
      "--trajectory_type",
      "joint",
      "--marker_type",
      plot_options.marker_type,
  };
  if (plot_options.same_color_per_trajectory) {
    args.push_back("--same_color_per_trajectory=true");
  }
  process.SetStderrFd(STDERR_FILENO);
  process.SetStdoutFd(STDOUT_FILENO);
  INTR_RETURN_IF_ERROR(AugmentSubprocessWithRunfilesEnvVars(process));
  process.SetProgram(binary, args);
  LOG(INFO) << "Started multi-trajectory plotter.";
  INTR_RETURN_IF_ERROR(process.Start());
  INTR_RETURN_IF_ERROR(process.Wait());
  if (process.exit_code() != 0) {
    return absl::InternalError("Failed to plot trajectories.");
  }
  return absl::OkStatus();
}

template <typename TrajT>
void PopulateTrajectoryBase(absl::string_view trajectory_name,
                            const TrajT& traj, absl::Duration time_offset,
                            std::optional<absl::Duration> advance_duration,
                            JointTrajectoryForPlotting* out) {
  out->name = std::string(trajectory_name);
  out->time_offset = time_offset;
  out->advance_duration = advance_duration;
  out->positions.reserve(traj.size());
  out->velocities.reserve(traj.size());
  out->accelerations.reserve(traj.size());
  out->timestamps.reserve(traj.size());
  for (int i = 0; i < traj.size(); ++i) {
    out->positions.push_back(traj.data().at(i).position);
    out->velocities.push_back(traj.data().at(i).velocity);
    out->accelerations.push_back(traj.data().at(i).acceleration);
    out->timestamps.push_back(
        eigenmath::VectorNd::Ones(1) *
        absl::ToDoubleSeconds(traj.time_stamps()[i] + time_offset));
  }
}

}  // namespace

JointTrajectoryForPlotting::JointTrajectoryForPlotting(
    absl::string_view trajectory_name, const JointTrajectoryPVA& traj,
    absl::Duration time_offset,
    std::optional<absl::Duration> advance_duration) {
  PopulateTrajectoryBase(trajectory_name, traj, time_offset, advance_duration,
                         this);
}

JointTrajectoryForPlotting::JointTrajectoryForPlotting(
    absl::string_view trajectory_name, const JointTrajectoryPVAJ& traj,
    absl::Duration time_offset,
    std::optional<absl::Duration> advance_duration) {
  PopulateTrajectoryBase(trajectory_name, traj, time_offset, advance_duration,
                         this);
  jerks.reserve(traj.size());
  for (int i = 0; i < traj.size(); ++i) {
    jerks.push_back(traj.data().at(i).jerk);
  }
}

JointTrajectoryForPlotting::JointTrajectoryForPlotting(
    absl::string_view trajectory_name, const JointTrajectoryPVAT& traj,
    absl::Duration time_offset,
    std::optional<absl::Duration> advance_duration) {
  PopulateTrajectoryBase(trajectory_name, traj, time_offset, advance_duration,
                         this);
  torques.reserve(traj.size());
  for (int i = 0; i < traj.size(); ++i) {
    torques.push_back(traj.data().at(i).torque);
  }
}

JointTrajectoryForPlotting::JointTrajectoryForPlotting(
    absl::string_view trajectory_name, const JointTrajectoryPVAJT& traj,
    absl::Duration time_offset,
    std::optional<absl::Duration> advance_duration) {
  PopulateTrajectoryBase(trajectory_name, traj, time_offset, advance_duration,
                         this);
  jerks.reserve(traj.size());
  torques.reserve(traj.size());
  for (int i = 0; i < traj.size(); ++i) {
    jerks.push_back(traj.data().at(i).jerk);
    torques.push_back(traj.data().at(i).torque);
  }
}

absl::StatusOr<std::string> CreateAndSavePlot(
    absl::Span<const JointTrajectoryForPlotting> trajectories,
    absl::string_view plot_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  if (trajectories.empty()) {
    return absl::InvalidArgumentError(
        "Cannot plot empty list of trajectories.");
  }
  INTR_ASSIGN_OR_RETURN(
      const std::string data_dir,
      SaveTrajectories(trajectories, plot_name, options_or_directory_path));

  std::vector<std::string> names;
  names.reserve(trajectories.size());
  for (const auto& traj : trajectories) {
    names.push_back(traj.name);
  }

  INTR_RETURN_IF_ERROR(CreateMultiTrajectoryPlot(
      data_dir, plot_name, names, plot_options, options_or_directory_path));
  return data_dir;
}

absl::StatusOr<std::string> CreateAndSavePlot(
    absl::Span<const CartStatePVA> traj, absl::Span<const double> timestamps,
    absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  INTR_ASSIGN_OR_RETURN(std::string data_dir,
                        SaveTrajectory(traj, timestamps, trajectory_name,
                                       options_or_directory_path));
  INTR_RETURN_IF_ERROR(CreatePlot(data_dir, trajectory_name, "cart",
                                  plot_options.marker_type,
                                  options_or_directory_path));
  return data_dir;
}

absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVA& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  INTR_ASSIGN_OR_RETURN(
      const std::string data_dir,
      SaveTrajectory(traj, trajectory_name, options_or_directory_path));
  INTR_RETURN_IF_ERROR(CreatePlot(data_dir, trajectory_name, "joint",
                                  plot_options.marker_type,
                                  options_or_directory_path));
  return data_dir;
}

absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVAJ& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  INTR_ASSIGN_OR_RETURN(
      const std::string data_dir,
      SaveTrajectory(traj, trajectory_name, options_or_directory_path));
  INTR_RETURN_IF_ERROR(CreatePlot(data_dir, trajectory_name, "joint",
                                  plot_options.marker_type,
                                  options_or_directory_path));
  return data_dir;
}

absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVAT& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  INTR_ASSIGN_OR_RETURN(
      const std::string data_dir,
      SaveTrajectory(traj, trajectory_name, options_or_directory_path));
  INTR_RETURN_IF_ERROR(CreatePlot(data_dir, trajectory_name, "joint",
                                  plot_options.marker_type,
                                  options_or_directory_path));
  return data_dir;
}

absl::StatusOr<std::string> CreateAndSavePlot(
    const JointTrajectoryPVAJT& traj, absl::string_view trajectory_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  INTR_ASSIGN_OR_RETURN(
      const std::string data_dir,
      SaveTrajectory(traj, trajectory_name, options_or_directory_path));
  INTR_RETURN_IF_ERROR(CreatePlot(data_dir, trajectory_name, "joint",
                                  plot_options.marker_type,
                                  options_or_directory_path));
  return data_dir;
}

absl::StatusOr<std::string> CreateAndSavePlot(
    const absl::btree_map<std::string, std::vector<double>>& values,
    absl::string_view name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path) {
  INTR_ASSIGN_OR_RETURN(
      const std::string data_file,
      SaveTimeseries(values, name, options_or_directory_path));
  INTR_ASSIGN_OR_RETURN(
      const std::string image_file,
      CreateTimeSeriesPlot(data_file, name, options_or_directory_path));
  return image_file;
}

absl::StatusOr<std::vector<eigenmath::Vector2d>> GetCirclePathForPlotting(
    const eigenmath::Vector2d& center, const double radius,
    const double reference_radius, const size_t reference_num_points) {
  if (radius <= 0.0) {
    return absl::InvalidArgumentError("The radius must be strictly positive.");
  }
  if (reference_radius <= 0.0) {
    return absl::InvalidArgumentError(
        "The reference radius must be strictly positive.");
  }
  if (reference_num_points <= 0) {
    return absl::InvalidArgumentError(
        "The reference number of points must be strictly positive.");
  }
  const size_t num_points = static_cast<size_t>(
      std::ceil(reference_num_points * radius / reference_radius));
  constexpr double kOmega = 2.0 * M_PI;
  std::vector<eigenmath::Vector2d> circle_path(num_points + 1);
  for (int i = 0; i < num_points + 1; ++i) {
    circle_path[i](0) = center(0) + (radius * sin(kOmega * i / num_points));
    circle_path[i](1) = center(1) + (radius * cos(kOmega * i / num_points));
  }
  return circle_path;
}

absl::StatusOr<std::string> CreateAndSaveXYPlot(
    absl::Span<const XYPathAndLineStyle> xy_paths_and_line_styles,
    absl::string_view plot_name,
    const TestingOptionsOrSaveDirectoryPath& options_or_directory_path,
    const PlotOptions& plot_options) {
  INTR_ASSIGN_OR_RETURN(const std::string data_dir,
                        SaveXYPaths(xy_paths_and_line_styles, plot_name,
                                    options_or_directory_path));
  std::string path_names = absl::StrJoin(
      xy_paths_and_line_styles, ",",
      [](std::string* out, const XYPathAndLineStyle& xy_path_and_line_style) {
        absl::StrAppend(out, xy_path_and_line_style.path_name);
      });
  std::string line_styles = absl::StrJoin(
      xy_paths_and_line_styles, ",",
      [](std::string* out, const XYPathAndLineStyle& xy_path_and_line_style) {
        absl::StrAppend(out, ToString(xy_path_and_line_style.line_style));
      });
  INTR_ASSIGN_OR_RETURN(
      const std::string image_file,
      CreateXYPlot(data_dir, plot_name, path_names, line_styles,
                   plot_options.marker_type, options_or_directory_path));
  return image_file;
}

}  // namespace intrinsic::icon
