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
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/compute_axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/geometry/storage/gzf_storage.h"
#include "intrinsic/geometry/storage/in_memory_storage.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/scene/sdf/scene_object_from_sdf.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/sim_spec_from_sdf.h"
#include "intrinsic/scene/util/object_user_data.h"
#include "intrinsic/scene/util/scene_object_gzf.h"
#include "intrinsic/scene/validate/large_mesh.h"
#include "intrinsic/scene/validate/scene_object_validate_geo.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

ABSL_FLAG(std::string, input_sdf_file, "",
          "The input sdf file to read the scene object from.");
ABSL_FLAG(std::string, input_file_descriptor_sets, "",
          "Comma-separated list of FileDescriptorSet binary proto files.");
ABSL_FLAG(std::string, scene_object_name, "",
          "The name of the generated scene object.");
ABSL_FLAG(std::string, output_scene_object_pbtxt_file,
          "/tmp/scene_object.pbtxt",
          "The output file to write the converted scene object textproto to.");
ABSL_FLAG(std::string, output_scene_object_gzf_file, "/tmp/scene_object.gzf",
          "The output file to write the converted scene object gzf to.");
ABSL_FLAG(double, large_mesh_checks_max_mesh_diagonal, 100,
          "The maximum allowable size for the diagonal of any single mesh. "
          "The conversion will stop with an error if any mesh has a bounding "
          "box that exceeds this value. Setting to a value < 0 disables these "
          "checks completely.");
ABSL_FLAG(bool, skip_validate_referenced_geos, false,
          "If true, skips the ValidateReferencedGeos check.");

namespace intrinsic {
namespace scene_object {

namespace {

absl::Status ValidateGeneratedSceneObject(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const GeometryDeserializer& geolib,
    const google::protobuf::FileDescriptorSet& fds) {
  INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object))
      << "Generated scene object is invalid.";

  if (!absl::GetFlag(FLAGS_skip_validate_referenced_geos)) {
    INTR_RETURN_IF_ERROR(ValidateReferencedGeos(scene_object, geolib))
        << "Referenced geometries in the scene object are invalid.";
  }

  INTR_RETURN_IF_ERROR(SceneObjectUserDataCanBeParsed(scene_object, fds))
      << "Failed to parse user_data in scene object. Remember to provide the "
         "protos used in user_data via `deps` attribute of the build rule.";

  return absl::OkStatus();
}

// GeometryLibrary that has a MapGeometryLibrary and optionally takes a second
// GeometryLibrary.
class WriteOnlyMapAndOtherGeometryLibrary : public GeometryLibrary,
                                            public GeometrySerializer,
                                            public GeometryDeserializer {
 public:
  explicit WriteOnlyMapAndOtherGeometryLibrary(
      std::unique_ptr<GeometryLibrary> other_library = nullptr)
      : map_library_(GetMapGeometryLibrary()),
        other_library_(std::move(other_library)) {}

  GeometrySerializer& Serializer() override { return *this; }
  const GeometryDeserializer& Deserializer() const override { return *this; }


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override {
    return map_library_->Deserializer().GetGeometry(geo_storage_refs, options);
  }


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override {
    return map_library_->Deserializer().GetGeometry(geo_storage_refs,
                                                    material_properties);
  }


  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override {
    if (other_library_) {
      INTR_RETURN_IF_ERROR(
          other_library_->Serializer().SaveGeometry(geometry).status());
    }
    return map_library_->Serializer().SaveGeometry(geometry);
  }


  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry) override {
    if (other_library_) {
      INTR_RETURN_IF_ERROR(
          other_library_->Serializer().SaveGeometryV1(geometry).status());
    }
    return map_library_->Serializer().SaveGeometryV1(geometry);
  }

  MapGeometryLibrary& MapLibrary() { return *map_library_; }

 private:
  std::unique_ptr<MapGeometryLibrary> map_library_;
  std::unique_ptr<GeometryLibrary> other_library_;
};

absl::StatusOr<google::protobuf::FileDescriptorSet>
GetDescriptorSetFromFlags() {
  google::protobuf::FileDescriptorSet fds;
  if (const std::string fdset_args =
          absl::GetFlag(FLAGS_input_file_descriptor_sets);
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
  return fds;
}

}  // namespace

// This will load a world into memory from some format (supported by world_load)
// and it will output the world into a pbtxt file and optionally a gzf file.
absl::Status MainImpl() {
  const std::string input_sdf_file = absl::GetFlag(FLAGS_input_sdf_file);
  const std::string output_scene_object_pbtxt_file =
      absl::GetFlag(FLAGS_output_scene_object_pbtxt_file);
  const std::string output_scene_object_gzf_file =
      absl::GetFlag(FLAGS_output_scene_object_gzf_file);

  QCHECK(!output_scene_object_pbtxt_file.empty())
      << "--output_scene_object_pbtxt_file must be set.";
  QCHECK(!input_sdf_file.empty()) << "--input_sdf_file must be set.";

  std::unique_ptr<GZFile> output_gzfile;

  // We need to use multiple GeometryLibraries so that we can later inspect the
  // geometry map for the large mesh checks, while also writing to the gzf file.
  std::unique_ptr<GeometryLibrary> gzf_serializer;
  if (!output_scene_object_gzf_file.empty()) {
    INTR_ASSIGN_OR_RETURN(output_gzfile,
                          GZFile::Create(output_scene_object_gzf_file));
    gzf_serializer = GetGzfGeometryLibrary(*output_gzfile);
  }

  INTR_ASSIGN_OR_RETURN(const auto fds, GetDescriptorSetFromFlags());

  SceneObjectFromSdfOptions options = {
      .user_data_fds = fds,
  };

  auto geometry_serializer =
      std::make_unique<WriteOnlyMapAndOtherGeometryLibrary>(
          std::move(gzf_serializer));
  INTR_ASSIGN_OR_RETURN(
      auto scene_object,
      SceneObjectFromSdfFile(input_sdf_file, sdf::SdfPathResolver,
                             *geometry_serializer, options));

  // If we have a name, set it.
  if (!absl::GetFlag(FLAGS_scene_object_name).empty()) {
    // Set the name of the scene object.
    scene_object.set_name(absl::GetFlag(FLAGS_scene_object_name));
  }

  // Perform checks for large meshes if enabled.
  const double max_mesh_diagonal =
      absl::GetFlag(FLAGS_large_mesh_checks_max_mesh_diagonal);
  if (max_mesh_diagonal > 0) {
    INTR_RETURN_IF_ERROR(CheckForLargeMeshes(
        geometry_serializer->MapLibrary().GetMaps().geometry_map,
        max_mesh_diagonal));
  }

  INTR_RETURN_IF_ERROR(ValidateGeneratedSceneObject(
      scene_object, geometry_serializer->Deserializer(), fds));

  LOG(INFO) << "Writing scene object in textproto format to: "
            << output_scene_object_pbtxt_file;
  INTR_RETURN_IF_ERROR(file::SetTextProto(output_scene_object_pbtxt_file,
                                          scene_object, file::Defaults()));

  if (!output_scene_object_gzf_file.empty()) {
    LOG(INFO) << "Writing scene object in gzf format to: "
              << output_scene_object_gzf_file;
    INTR_RETURN_IF_ERROR(AddSceneObjectToGzf(scene_object, *output_gzfile));
    INTR_RETURN_IF_ERROR(output_gzfile->Flush());
  }
  return absl::OkStatus();
}

}  // namespace scene_object
}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  // We change the stderr log threshold to minimize log spam in our build tools.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning);
  QCHECK_OK(::intrinsic::scene_object::MainImpl());
  return 0;
}
