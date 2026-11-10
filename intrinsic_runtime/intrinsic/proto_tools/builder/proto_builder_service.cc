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

#include "intrinsic_runtime/intrinsic/proto_tools/builder/proto_builder_service.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/util/message_differencer.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic_runtime/intrinsic/proto_tools/util/descriptor_pool_loader.h"
#include "intrinsic/executive/proto/proto_builder.pb.h"
#include "intrinsic/util/log_lines.h"
#include "intrinsic/util/proto/compile_utils.h"
#include "intrinsic/util/proto/descriptor_pools.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic::executive {

namespace {

constexpr std::string_view kUnversioned = "unversioned";

absl::Status ResolveImports(
    const google::protobuf::DescriptorProto& descriptor,
    const google::protobuf::DescriptorPool& descriptor_pool,
    absl::flat_hash_set<std::string>& imports) {
  for (const google::protobuf::FieldDescriptorProto& field :
       descriptor.field()) {
    if (field.type() == google::protobuf::FieldDescriptorProto::TYPE_MESSAGE) {
      absl::string_view full_name = field.type_name();
      if (absl::StartsWith(full_name, ".")) {
        full_name = full_name.substr(1);
      }
      const google::protobuf::Descriptor* type_descriptor =
          descriptor_pool.FindMessageTypeByName(full_name);
      if (type_descriptor == nullptr) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Cannot find field type %s", field.type_name()));
      }
      if (imports.contains(type_descriptor->file()->name())) continue;
      imports.emplace(type_descriptor->file()->name());
      google::protobuf::DescriptorProto type_descriptor_proto;
      type_descriptor->CopyTo(&type_descriptor_proto);
      INTR_RETURN_IF_ERROR(
          ResolveImports(type_descriptor_proto, descriptor_pool, imports));
    }
  }
  for (const google::protobuf::DescriptorProto& nested_type :
       descriptor.nested_type()) {
    INTR_RETURN_IF_ERROR(ResolveImports(nested_type, descriptor_pool, imports));
  }
  return absl::OkStatus();
}

}  // namespace

ProtoBuilderService::ProtoBuilderService() {
  static constexpr std::string_view kWellKnownTypesFileDescriptorSetFile =
      "intrinsic_runtime/intrinsic/proto_tools/builder/"
      "well_known_types_transitive_set_sci.proto.bin";

  // Load the well-known types file descriptor set from a file which has
  // file descriptors **with** source code info so that file descriptor sets
  // served to users can include comments for well-known types.
  absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>> loaded_pool =
      LoadedDescriptorPool::LoadFromRunfiles(
          kWellKnownTypesFileDescriptorSetFile);
  CHECK_OK(loaded_pool);
  well_known_types_loaded_pool_ = std::move(*loaded_pool);

  // Manually register types from the well-known type file descriptor set that
  // should be exposed as well-known types. The containing proto files must have
  // been added to //intrinsic/executive/tools:well_known_types.
  RegisterWellKnownType("google.protobuf.Any");
  RegisterWellKnownType("google.protobuf.BoolValue");
  RegisterWellKnownType("google.protobuf.BytesValue");
  RegisterWellKnownType("google.protobuf.DoubleValue");
  RegisterWellKnownType("google.protobuf.Duration");
  RegisterWellKnownType("google.protobuf.Empty");
  RegisterWellKnownType("google.protobuf.FloatValue");
  RegisterWellKnownType("google.protobuf.Int32Value");
  RegisterWellKnownType("google.protobuf.Int64Value");
  RegisterWellKnownType("google.protobuf.StringValue");
  RegisterWellKnownType("google.protobuf.Timestamp");
  RegisterWellKnownType("google.protobuf.UInt32Value");
  RegisterWellKnownType("google.protobuf.UInt64Value");
  RegisterWellKnownType("intrinsic_proto.Accel", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Array", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Point", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Pose", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Quaternion", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Twist", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Vector2", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Vector3", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.icon.CartesianLimits", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.icon.JointVec", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.perception.v1.CameraSetting", "v1");
  RegisterWellKnownType({{"intrinsic_proto.perception.v1.CaptureData", "v1"}});
  RegisterWellKnownType(
      {{"intrinsic_proto.perception.v1.PoseEstimateInRoot", "v1"}});
  RegisterWellKnownType(
      {{"intrinsic_proto.perception.v1.PoseEstimatorId", "v1"}});
  RegisterWellKnownType("intrinsic_proto.world.FrameReferenceByName",
                        kUnversioned);
  RegisterWellKnownType("intrinsic_proto.world.ObjectReference", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.world.ObjectReferenceByName",
                        kUnversioned);
  RegisterWellKnownType("intrinsic_proto.world.ObjectWorldUpdates",
                        kUnversioned);
  RegisterWellKnownType("intrinsic_proto.world.TransformNodeReference",
                        kUnversioned);
  RegisterWellKnownType("intrinsic_proto.world.TransformNodeReferenceByName",
                        kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Affine3d", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.Matrixd", kUnversioned);
  RegisterWellKnownType("intrinsic_proto.assets.Id", kUnversioned);

  // Files required in the well-known types file descriptor set which don't
  // themselves define a registered well-known type or are a dependency of a
  // registered well-known type.
  std::vector<std::string> additional_required_files = {
      "intrinsic/assets/proto/field_metadata.proto",
      "intrinsic/skills/proto/skill_parameter_metadata.proto",
      "intrinsic/assets/proto/v1/resolved_dependency.proto",
  };

  // Guard against having "unused" files in the well-known types file descriptor
  // set. E.g., an unrelated proto could have been included accidentally, or a
  // proto file could have been included in the file descriptor set without
  // registering a type from that file as a well-known type.
  CheckWellKnownTypesContainOnlyRequiredFiles(additional_required_files);
}

void ProtoBuilderService::RegisterWellKnownType(
    std::vector<std::pair<std::string_view, std::string_view>>
        full_names_and_display_versions) {
  // Check input
  {
    absl::flat_hash_set<std::string_view> names;
    absl::flat_hash_set<std::string_view> display_versions;
    for (const auto& [full_name, display_version] :
         full_names_and_display_versions) {
      const google::protobuf::Descriptor* descriptor =
          well_known_types_loaded_pool_->descriptor_pool()
              ->FindMessageTypeByName(full_name);

      CHECK(descriptor) << "Type " << full_name
                        << " not found in well-known types pool. Add "
                           "the corresponding proto file to the well-known "
                           "types file descriptor set.";

      names.insert(descriptor->name());
      display_versions.insert(display_version);
    }

    CHECK(names.size() == 1)
        << "All descriptors for different versions of the same type must have "
           "the same message name (but the package can differ). Got: {"
        << absl::StrJoin(names, ", ") << "}";
    CHECK(display_versions.size() == 1 || !display_versions.contains(""))
        << "Version display names for " << *names.begin()
        << " must not be empty (type has multiple versions)";
    CHECK(display_versions.size() == full_names_and_display_versions.size())
        << "All version display names for " << *names.begin()
        << " must be different (type has multiple versions)";
  }

  intrinsic_proto::executive::TypeWithVersions type;

  for (const auto& [full_name, display_version] :
       full_names_and_display_versions) {
    // We verified above that this descriptor exists.
    const google::protobuf::Descriptor* descriptor =
        well_known_types_loaded_pool_->descriptor_pool()->FindMessageTypeByName(
            full_name);
    CHECK(descriptor);

    well_known_types_.push_back(descriptor);
    well_known_types_names_.emplace(descriptor->full_name());

    // We verified above that all versions of this type have the same name, so
    // this only has an effect in the first loop iteration.
    type.set_display_name(descriptor->name());

    intrinsic_proto::executive::TypeVersion* version = type.add_versions();
    version->set_message_full_name(descriptor->full_name());
    version->set_display_version(display_version);
    version->set_file(descriptor->file()->name());
  }

  CHECK(absl::c_find_if(
            well_known_types_with_versions_,
            [&type](const intrinsic_proto::executive::TypeWithVersions& other) {
              return other.display_name() == type.display_name();
            }) == well_known_types_with_versions_.end())
      << "Duplicate type display name " << type.display_name();

  well_known_types_with_versions_.push_back(type);
}

namespace {

void AddFileAndTransitiveDependencies(
    const google::protobuf::FileDescriptor* file,
    absl::flat_hash_set<std::string>& files_set) {
  if (files_set.contains(file->name())) {
    return;
  }
  files_set.insert(std::string(file->name()));
  for (int i = 0; i < file->dependency_count(); ++i) {
    AddFileAndTransitiveDependencies(file->dependency(i), files_set);
  }
}

}  // namespace

void ProtoBuilderService::CheckWellKnownTypesContainOnlyRequiredFiles(
    const std::vector<std::string>& additional_required_files) const {
  absl::flat_hash_set<std::string> required_files;
  // For every registered well-known type we require its containing file and all
  // of its dependencies.
  for (const google::protobuf::Descriptor* descriptor : well_known_types_) {
    AddFileAndTransitiveDependencies(descriptor->file(), required_files);
  }
  // For every additionally required file we require the file itself and all of
  // its dependencies.
  for (const std::string& file_name : additional_required_files) {
    const google::protobuf::FileDescriptor* file =
        well_known_types_loaded_pool_->descriptor_pool()->FindFileByName(
            file_name);
    CHECK(file) << "File " << file_name
                << " not found in the well-known types file "
                   "descriptor set but it is required";
    AddFileAndTransitiveDependencies(file, required_files);
  }

  // All files present in the well-known types file descriptor set must be
  // required files.
  std::vector<std::string> file_names;
  CHECK(well_known_types_loaded_pool_->descriptor_db()->FindAllFileNames(
      &file_names));
  for (const std::string& file_name : file_names) {
    CHECK(required_files.contains(file_name))
        << "File " << file_name
        << " is in the well-known types file descriptor set but it is not "
           "required.";
  }
}

absl::Status ProtoBuilderService::CheckContainsOnlyWellKnownTypes(
    const google::protobuf::DescriptorProto& descriptor) {
  for (const google::protobuf::FieldDescriptorProto& field :
       descriptor.field()) {
    // This assumes that the descriptor is always a "top-level" descriptor
    // without nesting. In particular maps are not supported.
    if (field.type() == google::protobuf::FieldDescriptorProto::TYPE_MESSAGE) {
      absl::string_view full_name = field.type_name();
      if (absl::StartsWith(full_name, ".")) {
        full_name = full_name.substr(1);
      }
      if (!well_known_types_names_.contains(full_name)) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Message: '%s' has field '%s' with type '%s', which is "
            "not a well known type.",
            descriptor.name(), field.name(), full_name));
      }
    }
  }
  for (const google::protobuf::DescriptorProto& nested_type :
       descriptor.nested_type()) {
    INTR_RETURN_IF_ERROR(CheckContainsOnlyWellKnownTypes(nested_type));
  }
  return absl::OkStatus();
}

namespace {

absl::StatusOr<std::string> DiffFileDescriptorProtos(
    const google::protobuf::FileDescriptorProto& file_descriptor_1,
    const google::protobuf::FileDescriptorProto& file_descriptor_2) {
  const google::protobuf::FieldDescriptor* source_code_info_field =
      google::protobuf::FileDescriptorProto::GetDescriptor()->FindFieldByName(
          "source_code_info");
  if (!source_code_info_field) {
    return absl::InternalError("Field 'source_code_info' not found");
  }
  const google::protobuf::FieldDescriptor* json_name_field =
      google::protobuf::FieldDescriptorProto::GetDescriptor()->FindFieldByName(
          "json_name");
  if (!json_name_field) {
    return absl::InternalError("Field 'json_name' not found");
  }

  google::protobuf::util::MessageDifferencer differencer;
  // Source code info does not have any effect on functionality.
  differencer.IgnoreField(source_code_info_field);
  // Compiled-in descriptors and descriptors loaded from a file descriptor set
  // with source code info differ in the json_name field. Ignoring the field
  // here allows users to use a compiled-in file descriptor, e.g. for
  // "wrappers.proto", in the user supplied dependencies (instead of getting the
  // file descriptor for the same file via ProtoBuilder.GetWellKnownTypes).
  differencer.IgnoreField(json_name_field);
  std::string diff;
  differencer.ReportDifferencesToString(&diff);
  bool equal = differencer.Compare(file_descriptor_1, file_descriptor_2);
  return equal ? "" : diff;
}

absl::Status CheckMergedUserAndWellKnownTypesDatabase(
    google::protobuf::DescriptorDatabase* merged_db,
    const google::protobuf::FileDescriptorSet user_fds,
    google::protobuf::DescriptorDatabase* well_known_types_db) {
  // Create a temporary pool only to be used as a helper in this function.
  DescriptorPoolErrorCollector error_collector;
  google::protobuf::DescriptorPool merged_pool(merged_db, &error_collector);

  for (const google::protobuf::FileDescriptorProto& user_file_proto :
       user_fds.file()) {
    // Check that the user dependency can be loaded. If it cannot, then it
    // probably means the user failed to supply a transitively closed dependency
    // set.
    const google::protobuf::FileDescriptor* user_file =
        merged_pool.FindFileByName(user_file_proto.name());
    if (user_file == nullptr) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Failed to verify dependency %s (not retrievable, maybe dependency "
          "set not transitively closed: %s)",
          user_file_proto.name(), error_collector.GetErrorsJoined()));
    }

    // Check if the user dependency also exists in the well-known types
    // supported by this service. If it does, check that they are the same,
    // otherwise reject.
    google::protobuf::FileDescriptorProto well_known_file_proto;
    if (well_known_types_db->FindFileByName(std::string(user_file->name()),
                                            &well_known_file_proto)) {
      INTR_ASSIGN_OR_RETURN(
          std::string diff,
          DiffFileDescriptorProtos(user_file_proto, well_known_file_proto));
      if (!diff.empty()) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Dependency file descriptor set contains "
            "well-known file '%s' in an incompatible version. Diff:\n%s",
            user_file->name(), diff));
      }
    }
  }
  INTR_RETURN_IF_ERROR(error_collector.GetStatus());
  return absl::OkStatus();
}

}  // namespace

grpc::Status ProtoBuilderService::Compile(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ProtoCompileRequest* request,
    intrinsic_proto::executive::ProtoCompileResponse* response) {
  LOG(INFO) << "Compiling proto " << request->proto_filename()
            << (request->has_dependencies()
                    ? " (user supplied custom dependencies)"
                    : "");
  LOG_LINES(INFO, request->proto_schema());

  // We only use the descriptor DB from `LoadedDescriptorPool`, but we use the
  // class for creation to have consistency. The performance overhead of the
  // extra pool seems acceptable for nicer readability.
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<LoadedDescriptorPool> user_pool,
      LoadedDescriptorPool::Create(request->dependencies()));

  auto merged_db = std::make_unique<google::protobuf::MergedDescriptorDatabase>(
      user_pool->descriptor_db(),
      well_known_types_loaded_pool_->descriptor_db());

  INTR_RETURN_IF_ERROR_GRPC(CheckMergedUserAndWellKnownTypesDatabase(
      merged_db.get(), request->dependencies(),
      well_known_types_loaded_pool_->descriptor_db()));

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->mutable_file_descriptor_set(),
      CompileSchema(request->proto_filename(), request->proto_schema(),
                    merged_db.get()),
      _.LogError());

  return grpc::Status::OK;
}

grpc::Status ProtoBuilderService::Compose(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ProtoComposeRequest* request,
    intrinsic_proto::executive::ProtoComposeResponse* response) {
  LOG(INFO) << "Composing descriptor proto " << request->proto_filename();
  absl::flat_hash_set<std::string> message_names;
  for (const auto& desc : request->input_descriptor()) {
    if (message_names.contains(desc.name())) {
      return ToGrpcStatus(absl::InvalidArgumentError(
          absl::StrFormat("Messages must be unique, but message %s has been "
                          "found multiple times.",
                          desc.name())));
    }
    message_names.insert(desc.name());
    LOG_LINES(INFO, absl::StrCat(desc));
  }

  // request->proto_filename must not collide with a file name in the generated
  // pool
  const google::protobuf::FileDescriptor* request_file =
      well_known_types_loaded_pool_->descriptor_pool()->FindFileByName(
          request->proto_filename());
  if (request_file != nullptr) {
    return ToGrpcStatus(absl::InvalidArgumentError(absl::StrFormat(
        "The proto_filename '%s' is already in use by a built-in proto file. "
        "Please use another proto_filename.",
        request->proto_filename())));
  }

  for (const google::protobuf::DescriptorProto& input_descriptor :
       request->input_descriptor()) {
    INTR_RETURN_IF_ERROR_GRPC(
        CheckContainsOnlyWellKnownTypes(input_descriptor));
  }

  // Build a FileDescriptorProto by adding the given input descriptors.
  // In addition check that every input_descriptor can be imported and record
  // its imports as dependencies on the FileDescriptorProto
  google::protobuf::FileDescriptorProto file_descriptor_proto;
  file_descriptor_proto.set_syntax("proto3");
  file_descriptor_proto.set_name(request->proto_filename());
  file_descriptor_proto.set_package(request->proto_package());
  absl::flat_hash_set<std::string> imports;
  for (const google::protobuf::DescriptorProto& input_descriptor :
       request->input_descriptor()) {
    INTR_RETURN_IF_ERROR_GRPC(
        ResolveImports(input_descriptor,
                       *well_known_types_loaded_pool_->descriptor_pool(),
                       imports))
        .LogError();
    *file_descriptor_proto.add_message_type() = input_descriptor;
  }
  absl::c_move(imports, google::protobuf::RepeatedFieldBackInserter(
                            file_descriptor_proto.mutable_dependency()));
  google::protobuf::SimpleDescriptorDatabase descriptor_db;
  if (!descriptor_db.Add(file_descriptor_proto)) {
    return ToGrpcStatus(absl::InvalidArgumentError(absl::StrFormat(
        "Failed to add file descriptor '%s'", request->proto_filename())));
  }

  google::protobuf::MergedDescriptorDatabase merged_db(
      &descriptor_db, well_known_types_loaded_pool_->descriptor_db());
  DescriptorPoolErrorCollector error_collector;
  google::protobuf::DescriptorPool descriptor_pool(&merged_db,
                                                   &error_collector);

  // Get a FileDescriptor object from the FileDescriptorProto built above to
  // gather recursive dependencies
  const google::protobuf::FileDescriptor* file_descriptor =
      descriptor_pool.FindFileByName(request->proto_filename());
  if (file_descriptor == nullptr) {
    absl::Status status = absl::InvalidArgumentError(absl::StrFormat(
        "Failed to import proto '%s': %s", request->proto_filename(),
        error_collector.GetErrorsJoined()));
    LOG(ERROR) << status;
    return ToGrpcStatus(status);
  }

  // Record the FileDescriptor and its dependencies to built the transitively
  // closed descriptor set.
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->mutable_file_descriptor_set(),
      CreateFileDescriptorSetFromFileDescriptors({file_descriptor}),
      _.LogError());

  return grpc::Status::OK;
}

grpc::Status ProtoBuilderService::GetWellKnownTypes(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetWellKnownTypesRequest* request,
    intrinsic_proto::executive::GetWellKnownTypesResponse* response) {
  for (const google::protobuf::Descriptor* descriptor : well_known_types_) {
    response->add_type_names(descriptor->full_name());
  }

  response->mutable_types_with_versions()->Add(
      well_known_types_with_versions_.begin(),
      well_known_types_with_versions_.end());

  *response->mutable_file_descriptor_set() =
      well_known_types_loaded_pool_->file_descriptor_set();
  return grpc::Status::OK;
}

}  // namespace intrinsic::executive
