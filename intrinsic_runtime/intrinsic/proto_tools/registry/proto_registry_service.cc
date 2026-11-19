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

#include "intrinsic_runtime/intrinsic/proto_tools/registry/proto_registry_service.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/descriptor.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic_runtime/intrinsic/proto_tools/registry/file_descriptor_set_name.h"
#include "intrinsic_runtime/intrinsic/proto_tools/registry/resolver.h"
#include "intrinsic/proto_tools/proto/proto_registry.pb.h"
#include "intrinsic/proto_tools/proto/proto_registry_internal.pb.h"
#include "intrinsic/util/proto/parsed_type_url.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/status/status_specs.h"

namespace intrinsic::proto_registry {

using intrinsic_proto::proto_registry::DetermineCompatibilityRequest;
using intrinsic_proto::proto_registry::DetermineCompatibilityResponse;
using intrinsic_proto::proto_registry::internal::FileDescriptorSetNameProto;

ProtoRegistryService::ProtoRegistryService(
    std::shared_ptr<ProtoResolverMap> resolver)
    : resolver_(std::move(resolver)) {}

absl::StatusOr<std::unique_ptr<ProtoRegistryService>>
ProtoRegistryService::CreateService(
    std::shared_ptr<ProtoResolverMap> resolver) {
  LOG(INFO) << "Creating service";
  return absl::WrapUnique(new ProtoRegistryService(std::move(resolver)));
}

void ProtoRegistryService::Shutdown() { quit_ = true; }

// Assumes that exactly one of 'requested_type_url' and 'requested_name' and
// 'requested_asset_id' is non-empty.
absl::StatusOr<
    std::pair<FileDescriptorSetName, google::protobuf::FileDescriptorSet>>
ProtoRegistryService::GetFileDescriptorSet(
    std::string_view requested_type_url, std::string_view requested_name,
    std::string_view requested_asset_id) {
  FileDescriptorSetName name;
  ParsedUrl parsed_url;

  if (!requested_type_url.empty()) {
    INTR_ASSIGN_OR_RETURN(parsed_url, ParseTypeUrl(requested_type_url));
    INTR_ASSIGN_OR_RETURN(name,
                          FileDescriptorSetNameForTypeUrl(requested_type_url));
  } else if (!requested_asset_id.empty()) {
    std::string type_url_prefix = GenerateIntrinsicTypeUrl(
        kIntrinsicTypeUrlAreaAssets, requested_asset_id);
    INTR_ASSIGN_OR_RETURN(parsed_url, ParseTypeUrlPrefix(type_url_prefix));
    INTR_ASSIGN_OR_RETURN(
        name, FileDescriptorSetNameForTypeUrlPrefix(type_url_prefix));
  } else {  // requested_name is set
    name = FileDescriptorSetName(requested_name);
    FileDescriptorSetNameProto unobfuscated_name;
    INTR_ASSIGN_OR_RETURN(unobfuscated_name,
                          UnobfuscateFileDescriptorSetName(name));
    if (!unobfuscated_name.type_url_prefix().empty()) {
      INTR_ASSIGN_OR_RETURN(
          parsed_url, ParseTypeUrlPrefix(unobfuscated_name.type_url_prefix()));
    } else {
      INTR_ASSIGN_OR_RETURN(parsed_url,
                            ParseTypeUrl(unobfuscated_name.type_url()));
    }
  }

  // parsed_url is a full type URL, except for the cases when it comes
  // from the requested_asset_id branch. Then it's a type URL prefix without a
  // message name.
  google::protobuf::FileDescriptorSet fds;
  INTR_ASSIGN_OR_RETURN(fds, resolver_->Resolve(parsed_url));

  return std::make_pair(name, std::move(fds));
}

grpc::Status ProtoRegistryService::GetNamedFileDescriptorSet(
    grpc::ServerContext* context,
    const intrinsic_proto::proto_registry::GetNamedFileDescriptorSetRequest*
        request,
    intrinsic_proto::proto_registry::NamedFileDescriptorSet* response) {
  if (quit_) {
    return ToGrpcStatus(absl::UnavailableError(
        "The proto-registry is currently shutting down"));
  }

  if (request->identifier_type_case() ==
      intrinsic_proto::proto_registry::GetNamedFileDescriptorSetRequest::
          IDENTIFIER_TYPE_NOT_SET) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "oneof 'identifier_type' is not set - either 'type_url' or 'name' or "
        "'asset_id' must be provided"));
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      (auto [name, fds]),
      GetFileDescriptorSet(request->type_url(), request->name(),
                           request->asset_id()));

  response->set_name(name.value());
  *response->mutable_file_descriptor_set() = std::move(fds);

  return grpc::Status::OK;
}

grpc::Status ProtoRegistryService::DetermineCompatibility(
    grpc::ServerContext* context, const DetermineCompatibilityRequest* request,
    DetermineCompatibilityResponse* response) {
  if (quit_) {
    return ToGrpcStatus(absl::UnavailableError(
        "The proto-registry is currently shutting down"));
  }

  if (request->checks_size() == 0) {
    return ToGrpcStatus(absl::InvalidArgumentError("No checks requested"));
  }

  for (int i = 0; i < request->checks_size(); ++i) {
    const intrinsic_proto::proto_registry::DetermineCompatibilityRequest::Check&
        check = request->checks(i);

    *response->add_result_statuses() = CreateExtendedStatus(
        13003,  // Assignability check skipped
        absl::StrFormat(
            "%s%sThe check for the given instance or version of message '%s' "
            "was skipped. ProtoRegistry.DetermineCompatibility has been "
            "retired and does not perform any checks anymore.",
            check.description(), check.description().empty() ? "" : ": ",
            check.message_full_name_b()),
        {.related_to = check.related_to(),
         .severity = intrinsic_proto::status::ExtendedStatus::INFO});
  }

  return grpc::Status::OK;
}

}  // namespace intrinsic::proto_registry
