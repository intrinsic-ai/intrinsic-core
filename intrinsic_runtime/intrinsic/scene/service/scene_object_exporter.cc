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

#include "intrinsic/scene/service/scene_object_exporter.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/struct.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/scene/mesh/scene_object_to_glb.h"
#include "intrinsic/scene/proto/v1/export.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/sdf/scene_object_to_sdf.h"
#include "intrinsic/scene/sdf/scene_object_to_zipped_sdf.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {

using ::intrinsic::scene_object::SceneObjectToGlb;
using ::intrinsic::scene_object::SceneObjectToZippedSdf;
using ::intrinsic::scene_object::ValidateSceneObject;
using ::intrinsic::sdf::SceneObjectToSdf;
using ::intrinsic::sdf::SceneObjectToSdfOptions;
using ::intrinsic_proto::scene_object::v1::ExportRequest;
using ::intrinsic_proto::scene_object::v1::ExportResponse;
using ::intrinsic_proto::scene_object::v1::SceneObject;

}  // namespace

absl::StatusOr<ExportResponse> ExportSceneObject(
    const ExportRequest& request, const GeometryLibrary& geometry_library) {
  switch (request.format()) {
    case ExportRequest::EXPORT_FORMAT_SDF:
    case ExportRequest::EXPORT_FORMAT_GLB:
      break;
    case ExportRequest::EXPORT_FORMAT_UNSPECIFIED:
    default:
      return absl::UnimplementedError(
          "Only SDF and GLB formats are currently supported.");
  }

  switch (request.input_case()) {
    case ExportRequest::kSceneObject:
      break;
    case ExportRequest::INPUT_NOT_SET:
    default:
      return absl::InvalidArgumentError(
          "Request does not contain SceneObject.");
  }

  const SceneObject& scene_object = request.scene_object().proto();
  INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));

  if (request.format() == ExportRequest::EXPORT_FORMAT_GLB) {
    ExportResponse response;
    INTR_ASSIGN_OR_RETURN(*response.mutable_glb(),
                          SceneObjectToGlb(scene_object, geometry_library));
    return response;
  }

  google::protobuf::FileDescriptorSet fds;
  if (request.include_user_data()) {
    // Some SceneObject assets may not have fds set correctly. So always merge
    // fds for `Struct` (a known type) to prevent avoidable serialization
    // errors.
    fds = request.fds();
    MergeFileDescriptorSet<google::protobuf::Struct>(fds);
  }
  SceneObjectToSdfOptions sdf_options = {
      .geometry_deserializer = &geometry_library.Deserializer(),
      .serialize_user_data = request.include_user_data(),
      .user_data_fds = fds,
  };

  if (request.include_geometries()) {
    ExportResponse response;
    INTR_ASSIGN_OR_RETURN(*response.mutable_zip_archive(),
                          SceneObjectToZippedSdf(scene_object, sdf_options));
    return response;
  }

  INTR_ASSIGN_OR_RETURN(std::string sdf_content,
                        SceneObjectToSdf(scene_object, sdf_options));
  ExportResponse response;
  response.set_sdf(std::move(sdf_content));
  return response;
}

}  // namespace intrinsic
