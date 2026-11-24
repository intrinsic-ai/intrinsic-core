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

#include "intrinsic/world/update_world.h"

#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/print_world.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/world_updates.pb.h"
#include "intrinsic/world/service/objects/object_world_updates_utils.h"
#include "intrinsic/world/world.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

ABSL_FLAG(std::string, world_gzf_filename, "",
          "Path from which to read the world. In intrinsic process format");
ABSL_FLAG(std::string, input_updates_proto_filenames, "",
          "Paths to (comma separated) WorldUpdates proto files describing how "
          "this world needs to be updated. The updates are applied in the "
          "order specified.");
ABSL_FLAG(std::string, output_world_filename, "",
          "Saves the serialized world to this file.");
ABSL_FLAG(bool, skip_kinematic_checks, false,
          "If set, skips the kinematic sanity checks on the generated world.");

namespace intrinsic {
namespace {

absl::Status ApplyUpdatesProtoFile(
    absl::string_view input_updates_proto_filename, World& world) {
  const auto world_updates =
      file::GetTextProto<intrinsic_proto::world::WorldUpdates>(
          input_updates_proto_filename, file::Defaults());
  if (world_updates.ok()) {
    return UpdateWorld(*world_updates, &world);
  }

  const auto object_world_updates =
      file::GetTextProto<intrinsic_proto::world::ObjectWorldUpdates>(
          input_updates_proto_filename, file::Defaults());
  if (object_world_updates.ok()) {
    for (const auto& entity_update : object_world_updates->entity_updates()) {
      INTR_RETURN_IF_ERROR(UpdateWorld(entity_update, &world));
    }

    if (!object_world_updates->updates().empty()) {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<object_world::ObjectWorld> object_world,
          object_world::ObjectWorld::CreateView(world),
          _ << "\n\nIf you have specified multiple updates proto files also "
               "check the order of updates. Files are processed in the given "
               "order and, for each file, WorldUpdate's are applied before "
               "ObjectWorldUpdate's. WorldUpdate's for making the world "
               "object-view compatible need to be applied before the first "
               "ObjectWorldUpdate.");
      INTR_RETURN_IF_ERROR(object_world::HandleObjectWorldUpdates(
          *object_world, *object_world_updates));
    }
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      "The file contents could neither be parsed as a text proto of type "
      "intrinsic_proto.world.WorldUpdates nor "
      "intrinsic_proto.world.ObjectWorldUpdates");
}

absl::Status MainImpl() {
  const std::string output_world_filename =
      absl::GetFlag(FLAGS_output_world_filename);
  if (output_world_filename.empty()) {
    return absl::InvalidArgumentError("--output_world_filename not specified");
  }

  const std::string world_gzf_filename =
      absl::GetFlag(FLAGS_world_gzf_filename);
  if (world_gzf_filename.empty()) {
    return absl::InvalidArgumentError("--world_gzf_filename not specified");
  }

  LOG(INFO) << "Loading the world from gzfile: " << world_gzf_filename;
  INTR_ASSIGN_OR_RETURN(auto gz_file, GZFile::Open(world_gzf_filename));
  INTR_ASSIGN_OR_RETURN(World world, World::FromFile(*gz_file));

  if (ABSL_VLOG_IS_ON(1)) {
    PrintAspectsTo(world, &std::cout);
  }

  const std::string input_updates_proto_filenames =
      absl::GetFlag(FLAGS_input_updates_proto_filenames);
  if (!input_updates_proto_filenames.empty()) {
    for (const auto input_updates_proto_filename :
         absl::StrSplit(input_updates_proto_filenames, ',')) {
      INTR_RETURN_IF_ERROR(
          ApplyUpdatesProtoFile(input_updates_proto_filename, world))
          << "Error found while applying updates in "
          << input_updates_proto_filename;
    }
  } else {
    return absl::InvalidArgumentError(
        "--input_updates_proto_filenames not specified");
  }

  if (!absl::GetFlag(FLAGS_skip_kinematic_checks)) {
    INTR_RETURN_IF_ERROR(CheckKinematics(world));
  }

  if (ABSL_VLOG_IS_ON(1)) {
    PrintAspectsTo(world, &std::cout);
  }

  // Saving the world
  LOG(INFO) << "Saving serialized world to " << output_world_filename;

  INTR_ASSIGN_OR_RETURN(auto gzfile, GZFile::Create(output_world_filename));
  INTR_RETURN_IF_ERROR(
      world.ToFile(gzfile.get(), GetDummyGeometryLibrary()->Deserializer()));
  INTR_RETURN_IF_ERROR(gzfile->Flush());

  return absl::OkStatus();
}

}  // namespace
}  // namespace intrinsic

int main(int argc, char* argv[]) {
  InitIntrinsic(argv[0], argc, argv);
  QCHECK_OK(intrinsic::MainImpl());
}
