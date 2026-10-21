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

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/text_format.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/geometry/storage/gzf_storage.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/util/object_user_data.h"
#include "intrinsic/scene/util/scene_object_gzf.h"
#include "intrinsic/scene/util/scene_object_updates.h"
#include "intrinsic/scene/validate/scene_object_validate_geo.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/proto/dynamic_message_parser.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

ABSL_FLAG(std::string, input_scene_object_gzf_file, "",
          "Path from which to read the scene object in gzf format.");
ABSL_FLAG(std::string, input_scene_object_pbtxt_file, "",
          "Path from which to read the scene object in textproto format.");
ABSL_FLAG(
    std::string, input_updates_proto_filenames, "",
    "Paths to (comma separated) SceneObjectUpdates proto files describing how "
    "this scene object needs to be updated. The updates are applied in the "
    "order specified.");
ABSL_FLAG(std::string, output_scene_object_gzf_file, "",
          "Saves the serialized scene object to this file in gzf format.");
ABSL_FLAG(
    std::string, output_scene_object_pbtxt_file, "",
    "Saves the serialized scene object to this file in textproto format.");
ABSL_FLAG(std::string, file_descriptor_sets, "",
          "Comma-separated list of FileDescriptorSet binary proto files.");

namespace intrinsic {
namespace scene_object {
namespace {

using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectUpdates;

// A helper class to find message types and extensions from a DescriptorPool.
// This is needed for TextFormat::Printer to correctly print Any protos.
class PoolTypeFinder : public google::protobuf::TextFormat::Finder {
 public:
  explicit PoolTypeFinder(const google::protobuf::DescriptorPool* pool)
      : pool_(pool) {}

  const google::protobuf::Descriptor* FindAnyType(
      const google::protobuf::Message& /*message*/,
      const std::string& /*prefix*/, const std::string& name) const override {
    return pool_->FindMessageTypeByName(name);
  }

 private:
  const google::protobuf::DescriptorPool* pool_;
};

absl::StatusOr<SceneObject> ApplyUpdatesProtoFile(
    const SceneObject& scene_object,
    const absl::string_view input_updates_proto_filename,
    const google::protobuf::FileDescriptorSet& fds) {
  std::string proto_contents;
  INTR_RETURN_IF_ERROR(file::GetContents(input_updates_proto_filename,
                                         &proto_contents, file::Defaults()));

  INTR_ASSIGN_OR_RETURN(
      MessageAndDeps parsed_msg,
      DynamicMessageParser::ParseSingleTextProto(
          fds, "intrinsic_proto.scene_object.v1.SceneObjectUpdates",
          proto_contents),
      _ << "Failed to parse scene object updates proto file. Remember to "
           "provide the proto dependencies in the `deps` attribute of the "
           "build rule if you are using custom proto(s) to specify "
           "SceneObject user_data.");
  std::string serialized;
  if (!parsed_msg.message->SerializeToString(&serialized)) {
    return absl::InternalError("Failed to serialize message to string");
  }
  SceneObjectUpdates scene_object_updates;
  if (!scene_object_updates.ParseFromString(serialized)) {
    return absl::InternalError(
        "Failed to parse SceneObjectUpdates from string");
  }
  INTR_ASSIGN_OR_RETURN(
      const auto result,
      ProcessSceneObjectUpdates(scene_object, scene_object_updates));
  return result.result;
}

absl::Status MainImpl() {
  const std::string input_scene_object_pbtxt_file =
      absl::GetFlag(FLAGS_input_scene_object_pbtxt_file);
  const std::string input_scene_object_gzf_file =
      absl::GetFlag(FLAGS_input_scene_object_gzf_file);
  if (input_scene_object_pbtxt_file.empty() ==
      input_scene_object_gzf_file.empty()) {
    return absl::InvalidArgumentError(
        "exactly one of --input_scene_object_pbtxt_file or "
        "--input_scene_object_gzf_file must be specified.");
  }

  const std::string output_scene_object_pbtxt_file =
      absl::GetFlag(FLAGS_output_scene_object_pbtxt_file);
  const std::string output_scene_object_gzf_file =
      absl::GetFlag(FLAGS_output_scene_object_gzf_file);
  if (output_scene_object_pbtxt_file.empty() &&
      output_scene_object_gzf_file.empty()) {
    return absl::InvalidArgumentError(
        "--output_scene_object_pbtxt_file or --output_scene_object_gzf_file "
        "must be specified.");
  }

  google::protobuf::FileDescriptorSet fds =
      intrinsic::GenFileDescriptorSet<SceneObjectUpdates>();
  if (const std::string fdset_args = absl::GetFlag(FLAGS_file_descriptor_sets);
      !fdset_args.empty()) {
    const auto fdset_list = absl::StrSplit(fdset_args, ',');
    for (const auto& file_descriptor_sets_path : fdset_list) {
      INTR_ASSIGN_OR_RETURN(
          const auto file_descriptor_set,
          file::GetBinaryProto<google::protobuf::FileDescriptorSet>(
              file_descriptor_sets_path, file::Defaults()),
          _ << "Failed to parse file descriptor set");
      fds.MergeFrom(file_descriptor_set);
    }
  }

  SceneObject scene_object;
  std::unique_ptr<GZFile> input_gzfile;

  if (!input_scene_object_gzf_file.empty()) {
    LOG(INFO) << "Loading the scene object from: "
              << input_scene_object_gzf_file;
    INTR_ASSIGN_OR_RETURN(input_gzfile,
                          GZFile::Open(input_scene_object_gzf_file),
                          _ << "Failed to open input gzf file");
    INTR_ASSIGN_OR_RETURN(
        scene_object, GetSceneObjectFromGzf(*input_gzfile),
        _ << "Failed to get scene object from input gzf file");
  } else {  // input_scene_object_pbtxt_file is set
    LOG(INFO) << "Loading the scene object from: "
              << input_scene_object_pbtxt_file;
    INTR_ASSIGN_OR_RETURN(scene_object,
                          file::GetTextProto<SceneObject>(
                              input_scene_object_pbtxt_file, file::Defaults()),
                          _ << "Failed to parse input pbtxt file");
  }
  INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object))
      << "Provided scene object is invalid.";

  auto dummy_geolib = GetDummyGeometryLibrary();
  const GeometryDeserializer* geolib = &dummy_geolib->Deserializer();
  std::unique_ptr<GeometryLibrary> gzf_geolib;
  if (input_gzfile != nullptr) {
    gzf_geolib = GetReadOnlyGzfGeometryLibrary(*input_gzfile);
    geolib = &gzf_geolib->Deserializer();
  }

  // Process any specified updates.
  const std::string input_updates_proto_filenames =
      absl::GetFlag(FLAGS_input_updates_proto_filenames);
  for (const auto input_updates_proto_filename :
       absl::StrSplit(input_updates_proto_filenames, ',')) {
    if (input_updates_proto_filename.empty()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        scene_object,
        ApplyUpdatesProtoFile(scene_object, input_updates_proto_filename, fds),
        _ << "Error found while applying updates in "
          << input_updates_proto_filename);
  }

  // Validate the scene object again after applying updates.
  INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object))
      << "Scene object is invalid after applying updates.";
  INTR_RETURN_IF_ERROR(ValidateReferencedGeos(scene_object, *geolib))
      << "Referenced geometries in the scene object are invalid.";
  INTR_RETURN_IF_ERROR(SceneObjectUserDataCanBeParsed(scene_object, fds))
      << "Failed to parse user_data in scene object.";

  // Saving the scene object to the output file.
  if (!output_scene_object_pbtxt_file.empty()) {
    LOG(INFO) << "Saving serialized textproto scene object to "
              << output_scene_object_pbtxt_file;
    google::protobuf::SimpleDescriptorDatabase db;
    INTR_RETURN_IF_ERROR(intrinsic::PopulateDescriptorDatabase(&db, fds));
    google::protobuf::DescriptorPool pool(&db);
    PoolTypeFinder finder(&pool);
    google::protobuf::TextFormat::Printer printer;
    printer.SetFinder(&finder);
    printer.SetExpandAny(true);

    std::string pbtxt_contents;
    if (!printer.PrintToString(scene_object, &pbtxt_contents)) {
      return absl::InternalError("Failed to print scene object to text proto.");
    }
    INTR_RETURN_IF_ERROR(file::SetContents(output_scene_object_pbtxt_file,
                                           pbtxt_contents, file::Defaults()))
        << "Failed to write output pbtxt file";
  }

  if (!output_scene_object_gzf_file.empty()) {
    LOG(INFO) << "Saving serialized gzf scene object to "
              << output_scene_object_gzf_file;
    INTR_ASSIGN_OR_RETURN(auto output_gzfile,
                          GZFile::Create(output_scene_object_gzf_file),
                          _ << "Failed to open output gzf file");

    // Copy the input gzf file to the output gzf file, if one existed.
    if (input_gzfile != nullptr) {
      for (const auto& [id, key] : input_gzfile->GetAllChunkIds()) {
        const auto chunk = input_gzfile->GetChunkOrDie(id, key);
        INTR_RETURN_IF_ERROR(output_gzfile->SetChunk(id, key, chunk))
            << "Failed to set chunk in output gzf file";
      }
    }

    // Override the scene object in the output gzf file.
    INTR_RETURN_IF_ERROR(AddSceneObjectToGzf(scene_object, *output_gzfile))
        << "Failed to add scene object to output gzf file";
    INTR_RETURN_IF_ERROR(output_gzfile->Flush())
        << "Failed to flush output gzf file";
  }

  return absl::OkStatus();
}

}  // namespace
}  // namespace scene_object
}  // namespace intrinsic

int main(int argc, char* argv[]) {
  InitIntrinsic(argv[0], argc, argv);
  QCHECK_OK(intrinsic::scene_object::MainImpl());
}
