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

#include "intrinsic/assets/dependencies/utils.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "intrinsic/assets/data/proto/v1/data_assets.grpc.pb.h"
#include "intrinsic/assets/dependencies/platform.h"  
#include "intrinsic/assets/instances/connect/connect.h"
#include "intrinsic/assets/proto/field_metadata.pb.h"
#include "intrinsic/assets/proto/v1/dependency.pb.h"
#include "intrinsic/assets/proto/v1/grpc_connection.pb.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::assets::dependencies {

namespace {

using ::intrinsic_proto::assets::v1::GrpcConnection;
using ::intrinsic_proto::assets::v1::ResolvedDependency;

char kIngressAddress[] =
    "istio-ingressgateway.app-ingress.svc.cluster.local:80";

using ConnectionKey =
    std::pair<std::string, std::vector<std::pair<std::string, std::string>>>;

ConnectionKey MakeConnectionKey(const GrpcConnection& connection) {
  std::vector<std::pair<std::string, std::string>> headers;
  headers.reserve(connection.metadata_size());
  for (const auto& metadata : connection.metadata()) {
    headers.emplace_back(metadata.key(), metadata.value());
  }
  // Sort stably by header key so that headers with different keys are compared
  // order-independently, while preserving the relative order of multiple values
  // for the same key (which is significant in gRPC/HTTP2).
  std::stable_sort(
      headers.begin(), headers.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });
  return {connection.address(), std::move(headers)};
}

absl::StatusOr<const ResolvedDependency::Interface*> FindInterface(
    const ResolvedDependency& dep, absl::string_view iface) {
  const auto it = dep.interfaces().find(std::string(iface));
  if (it == dep.interfaces().end()) {
    std::string explanation;
    if (dep.interfaces().empty()) {
      explanation = "no interfaces provided";
    } else {
      std::vector<std::string> keys;
      keys.reserve(dep.interfaces().size());
      for (const auto& [key, _] : dep.interfaces()) {
        keys.push_back(key);
      }
      explanation = absl::StrCat("got interfaces: ", absl::StrJoin(keys, ", "));
    }
    return absl::NotFoundError(
        absl::StrCat("Interface not found in resolved dependency (want ", iface,
                     ", ", explanation, ")"));
  }
  return &it->second;
}

absl::StatusOr<GrpcConnection> GetGrpcConnection(const ResolvedDependency& dep,
                                                 absl::string_view iface) {
  INTR_ASSIGN_OR_RETURN(const auto* iface_proto, FindInterface(dep, iface));
  if (!iface_proto->has_grpc() || !iface_proto->grpc().has_connection()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Interface is not gRPC or no connection information is available: ",
        iface));
  }
  return iface_proto->grpc().connection();
}


GrpcConnection GetGrpcConnectionWithRuntimeFallback(
    const ResolvedDependency& dep, absl::string_view iface) {
  if (auto conn_or = GetGrpcConnection(dep, iface); conn_or.ok()) {
    return *std::move(conn_or);
  }
  GrpcConnection fallback;
  fallback.set_address(kIngressAddress);
  auto* metadata = fallback.add_metadata();
  metadata->set_key("x-resource-instance-name");
  metadata->set_value(platform::kRuntimeInstanceName);
  return fallback;
}


std::unique_ptr<intrinsic_proto::data::v1::DataAssets::StubInterface>
MakeDefaultDataAssetsClient() {
  return intrinsic_proto::data::v1::DataAssets::NewStub(::grpc::CreateChannel(
      kIngressAddress,
      grpc::InsecureChannelCredentials()));  // NOLINT(insecure)
}

}  // namespace

namespace internal {

absl::Status ConnectManyImpl(
    const ResolvedDependency& dep, absl::Span<const absl::string_view> ifaces,
    const ::grpc::ChannelArguments& channel_args,
    absl::Span<std::shared_ptr<grpc::Channel>> channels) {
  std::vector<GrpcConnection> connections;
  connections.reserve(ifaces.size());
  for (absl::string_view iface : ifaces) {
    INTR_ASSIGN_OR_RETURN(GrpcConnection connection,
                          GetGrpcConnection(dep, iface));
    connections.push_back(std::move(connection));
  }

  absl::flat_hash_map<ConnectionKey, std::shared_ptr<grpc::Channel>>
      channels_by_key;
  for (size_t i = 0; i < ifaces.size(); ++i) {
    LOG(INFO) << "Connecting to interface \"" << ifaces[i] << "\"";
    const GrpcConnection& connection = connections[i];
    ConnectionKey key = MakeConnectionKey(connection);
    auto it = channels_by_key.find(key);
    if (it == channels_by_key.end()) {
      INTR_ASSIGN_OR_RETURN(auto channel,
                            ::intrinsic::assets::instances::connect::Connect(
                                connection, channel_args));
      it = channels_by_key.emplace(std::move(key), std::move(channel)).first;
    }
    channels[i] = it->second;
  }
  return absl::OkStatus();
}


absl::Status ConnectManyWithRuntimeAssetFallbackForAssetMigrationOnlyImpl(
    const ResolvedDependency& dep, absl::Span<const absl::string_view> ifaces,
    const ::grpc::ChannelArguments& channel_args,
    absl::Span<std::shared_ptr<grpc::Channel>> channels) {
  absl::flat_hash_map<ConnectionKey, std::shared_ptr<grpc::Channel>>
      channels_by_key;
  for (size_t i = 0; i < ifaces.size(); ++i) {
    LOG(INFO) << "Connecting to interface \"" << ifaces[i] << "\"";
    GrpcConnection connection =
        GetGrpcConnectionWithRuntimeFallback(dep, ifaces[i]);
    ConnectionKey key = MakeConnectionKey(connection);
    auto it = channels_by_key.find(key);
    if (it == channels_by_key.end()) {
      INTR_ASSIGN_OR_RETURN(auto channel,
                            ::intrinsic::assets::instances::connect::Connect(
                                connection, channel_args));
      it = channels_by_key.emplace(std::move(key), std::move(channel)).first;
    }
    channels[i] = it->second;
  }
  return absl::OkStatus();
}


}  // namespace internal

absl::StatusOr<std::shared_ptr<grpc::Channel>> Connect(
    const ResolvedDependency& dep, absl::string_view iface,
    const ::grpc::ChannelArguments& channel_args) {
  INTR_ASSIGN_OR_RETURN(auto channels, ConnectMany(dep, {iface}, channel_args));
  return std::move(channels[0]);
}


absl::StatusOr<std::shared_ptr<grpc::Channel>>
ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(
    const ResolvedDependency& dep, absl::string_view iface,
    const ::grpc::ChannelArguments& channel_args) {
  INTR_ASSIGN_OR_RETURN(
      auto channels, ConnectManyWithRuntimeAssetFallbackForAssetMigrationOnly(
                         dep, {iface}, channel_args));
  return std::move(channels[0]);
}


absl::StatusOr<google::protobuf::Any> GetDataPayload(
    const ResolvedDependency& dep, absl::string_view iface,
    intrinsic_proto::data::v1::DataAssets::StubInterface* data_assets_client) {
  INTR_ASSIGN_OR_RETURN(const auto* iface_proto, FindInterface(dep, iface));
  if (!iface_proto->has_data()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Interface is not data or no data dependency information "
                     "is available: ",
                     iface));
  }

  std::unique_ptr<intrinsic_proto::data::v1::DataAssets::StubInterface>
      default_data_assets_client;
  if (data_assets_client == nullptr) {
    default_data_assets_client = MakeDefaultDataAssetsClient();
    data_assets_client = default_data_assets_client.get();
  }

  // Get the DataAsset proto from the DataAssets service.
  intrinsic_proto::data::v1::GetDataAssetRequest request;
  *request.mutable_id() = iface_proto->data().id();
  intrinsic_proto::data::v1::DataAsset da;
  grpc::ClientContext context;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(data_assets_client->GetDataAsset(&context, request, &da)));

  return da.data();
}

bool RequiresDependencyAnnotationCheck(
    const ResolvedDepsIntrospectionOptions& options) {
  return options.check_dependency_annotation || options.check_skill_annotations;
}

bool IsDependencyWithConditionsFound(
    const google::protobuf::Descriptor& descriptor,
    const ResolvedDepsIntrospectionOptions& options) {
  if (descriptor.full_name() == ResolvedDependency::descriptor()->full_name() &&
      !RequiresDependencyAnnotationCheck(options)) {
    return true;
  }

  if (options.treat_any_as_true &&
      descriptor.full_name() ==
          google::protobuf::Any::descriptor()->full_name()) {
    return true;
  }

  if (RequiresDependencyAnnotationCheck(options)) {
    for (int i = 0; i < descriptor.field_count(); ++i) {
      const google::protobuf::FieldDescriptor* field = descriptor.field(i);
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        continue;
      }
      const google::protobuf::FieldDescriptor* field_message_descriptor;
      if (field->is_map()) {
        if (field->message_type()->map_value() == nullptr ||
            field->message_type()->map_value()->cpp_type() !=
                google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
          continue;
        }
        field_message_descriptor = field->message_type()->map_value();
      } else {
        field_message_descriptor = field;
      }

      if (field_message_descriptor->message_type()->full_name() !=
          ResolvedDependency::descriptor()->full_name()) {
        continue;
      }
      if (!field->options().HasExtension(
              intrinsic_proto::assets::field_metadata)) {
        continue;
      }
      const intrinsic_proto::assets::FieldMetadata& field_metadata =
          field->options().GetExtension(
              intrinsic_proto::assets::field_metadata);
      // At this point, the dependency annotation check is complete. If the
      // Skill annotations check is not required, we can return true.
      if (!options.check_skill_annotations) {
        return true;
      }
      return field_metadata.dependency().has_skill_annotations();
    }
  }
  return false;
}

void WalkProtoMessageDescriptors(
    const google::protobuf::Descriptor& descriptor,
    std::function<bool(const google::protobuf::Descriptor&)> function,
    absl::flat_hash_set<const google::protobuf::Descriptor*>& visited) {
  visited.insert(&descriptor);
  bool should_enter = function(descriptor);
  if (!should_enter) {
    return;
  }

  for (int i = 0; i < descriptor.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor.field(i);
    // Skip if not a message type.
    if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      continue;
    }
    // Skip already visited messages.
    if (visited.contains(field->message_type())) {
      continue;
    }

    if (field->is_map()) {
      const google::protobuf::FieldDescriptor* value_field =
          field->message_type()->map_value();
      if (value_field == nullptr ||
          value_field->cpp_type() !=
              google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        continue;
      }
      if (visited.contains(value_field->message_type())) {
        continue;
      }
      WalkProtoMessageDescriptors(*value_field->message_type(), function,
                                  visited);
    } else {
      WalkProtoMessageDescriptors(*field->message_type(), function, visited);
    }
  }
}

bool HasResolvedDependency(const google::protobuf::Descriptor& descriptor,
                           const ResolvedDepsIntrospectionOptions& options) {
  bool has_resolved_dependency = false;
  absl::flat_hash_set<const google::protobuf::Descriptor*> visited;
  WalkProtoMessageDescriptors(
      descriptor,
      [&](const google::protobuf::Descriptor& descriptor) -> bool {
        if (IsDependencyWithConditionsFound(descriptor, options)) {
          // Stop the recursion if we already found a dependency.
          has_resolved_dependency = true;
          return false;
        }
        return true;
      },
      visited);
  return has_resolved_dependency;
}


std::vector<std::string> FallbackManifestKeys(
    const google::protobuf::Descriptor& descriptor) {
  std::vector<std::string> keys;
  absl::flat_hash_set<const google::protobuf::Descriptor*> visited;
  WalkProtoMessageDescriptors(
      descriptor,
      [&](const google::protobuf::Descriptor& desc) -> bool {
        for (int i = 0; i < desc.field_count(); ++i) {
          const google::protobuf::FieldDescriptor* field = desc.field(i);
          if (!field->options().HasExtension(
                  intrinsic_proto::assets::field_metadata)) {
            continue;
          }
          const intrinsic_proto::assets::FieldMetadata& field_metadata =
              field->options().GetExtension(
                  intrinsic_proto::assets::field_metadata);
          if (!field_metadata.has_dependency()) {
            continue;
          }
          const intrinsic_proto::assets::v1::Dependency& dep =
              field_metadata.dependency();
          if (dep.has_skill_annotations() &&
              dep.skill_annotations().has_fallback_manifest_dependency_key()) {
            const std::string& key =
                dep.skill_annotations().fallback_manifest_dependency_key();
            if (!key.empty()) {
              keys.push_back(key);
            }
          }
        }
        return true;
      },
      visited);
  return keys;
}


}  // namespace intrinsic::assets::dependencies
