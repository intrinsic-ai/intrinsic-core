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

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/log_severity.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/geometry/storage/gzf_storage.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/scene/usd/scene_object_from_usd.h"
#include "intrinsic/scene/usd/utils.h"
#include "intrinsic/scene/util/object_user_data.h"
#include "intrinsic/scene/util/scene_object_gzf.h"
#include "intrinsic/scene/validate/scene_object_validate_geo.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

// `usd_file_converter` is an executable for converting a USD file to our
// SceneObject format. It outputs the contents as pbtxt and gzf files.
//
// Usage:
// usd_file_converter --input_usd_file some_file.usd --scene_object_name
// my_scene --output_scene_object_pbtxt_file output.pbtxt
// --output_scene_object_gzf_file output.gzf
//
ABSL_FLAG(std::string, input_usd_file, "",
          "The input USD file to read the scene object from (may be a .usd, "
          ".usda, .usdc, or .usdz file).");
ABSL_FLAG(std::string, scene_object_name, "",
          "The name of the generated scene object.");
ABSL_FLAG(std::string, output_scene_object_pbtxt_file,
          "/tmp/scene_object.pbtxt",
          "The output file to write the converted scene object textproto to.");
ABSL_FLAG(std::string, output_scene_object_gzf_file, "/tmp/scene_object.gzf",
          "The output file to write the converted scene object gzf to.");

namespace intrinsic {
namespace scene_object {

void MainImpl() {
  const std::string input_usd_file = absl::GetFlag(FLAGS_input_usd_file);
  const std::string output_scene_object_pbtxt_file =
      absl::GetFlag(FLAGS_output_scene_object_pbtxt_file);
  const std::string output_scene_object_gzf_file =
      absl::GetFlag(FLAGS_output_scene_object_gzf_file);

  QCHECK(!output_scene_object_pbtxt_file.empty())
      << "--output_scene_object_pbtxt_file must be set.";
  QCHECK(!input_usd_file.empty()) << "--input_usd_file must be set.";

  // Initialize the OpenUSD library. Must be done before any USD functions are
  // called.
  QCHECK_OK(usd::InitializeOpenUsdLibrary());

  std::unique_ptr<GZFile> output_gzfile;
  std::unique_ptr<GeometryLibrary> gzf_serializer;
  if (!output_scene_object_gzf_file.empty()) {
    ASSIGN_OR_DIE(output_gzfile, GZFile::Create(output_scene_object_gzf_file));
    gzf_serializer = GetGzfGeometryLibrary(*output_gzfile);
  }

  ASSIGN_OR_DIE(auto scene_object,
                usd::SceneObjectFromUsdFile(input_usd_file,
                                            gzf_serializer->Serializer()));

  // If we have a name, set it.
  if (!absl::GetFlag(FLAGS_scene_object_name).empty()) {
    scene_object.set_name(absl::GetFlag(FLAGS_scene_object_name));
  }

  QCHECK_OK(scene_object::ValidateSceneObject(scene_object))
      << "Generated scene object is invalid.";

  QCHECK_OK(
      ValidateReferencedGeos(scene_object, gzf_serializer->Deserializer()))
      << "Referenced geometries in the scene object are invalid.";

  LOG(INFO) << "Writing scene object in textproto format to: "
            << output_scene_object_pbtxt_file;
  QCHECK_OK(file::SetTextProto(output_scene_object_pbtxt_file, scene_object,
                               file::Defaults()));

  if (!output_scene_object_gzf_file.empty()) {
    LOG(INFO) << "Writing scene object in gzf format to: "
              << output_scene_object_gzf_file;
    CHECK_OK(AddSceneObjectToGzf(scene_object, *output_gzfile));
    CHECK_OK(output_gzfile->Flush());
  }
}

}  // namespace scene_object
}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  // We change the stderr log threshold to minimize log spam in our build tools.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning);
  ::intrinsic::scene_object::MainImpl();
  return 0;
}
