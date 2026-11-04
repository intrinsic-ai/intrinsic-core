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

#ifndef INTRINSIC_ASSETS_DEPENDENCIES_UTILS_H_
#define INTRINSIC_ASSETS_DEPENDENCIES_UTILS_H_

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/assets/data/proto/v1/data_assets.grpc.pb.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"

namespace intrinsic::assets::dependencies {

namespace internal {

absl::Status ConnectManyImpl(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    absl::Span<const absl::string_view> ifaces,
    const ::grpc::ChannelArguments& channel_args,
    absl::Span<std::shared_ptr<grpc::Channel>> channels);


absl::Status ConnectManyWithRuntimeAssetFallbackForAssetMigrationOnlyImpl(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    absl::Span<const absl::string_view> ifaces,
    const ::grpc::ChannelArguments& channel_args,
    absl::Span<std::shared_ptr<grpc::Channel>> channels);


}  // namespace internal

// LINT.IfChange(utils)

// Creates a gRPC channel for communicating with the provider of the specified
// interface.
//
// The channel will be configured with any needed metadata for communicating
// with the provider.
absl::StatusOr<std::shared_ptr<grpc::Channel>> Connect(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    absl::string_view iface,
    const ::grpc::ChannelArguments& channel_args = ::grpc::ChannelArguments());

// Creates gRPC channels for communicating with the providers of the specified
// interfaces, in the same order as `ifaces`.
//
// The channels will be configured with any needed metadata for communicating
// with the providers. Interfaces that share the exact same address and metadata
// headers will share the same underlying `grpc::Channel` instance.
template <size_t N>
absl::StatusOr<std::array<std::shared_ptr<grpc::Channel>, N>> ConnectMany(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    const absl::string_view (&ifaces)[N],
    const ::grpc::ChannelArguments& channel_args = ::grpc::ChannelArguments()) {
  std::array<std::shared_ptr<grpc::Channel>, N> channels;
  if (absl::Status s = internal::ConnectManyImpl(dep, ifaces, channel_args,
                                                 absl::MakeSpan(channels));
      !s.ok()) {
    return s;
  }
  return channels;
}


// Creates a gRPC channel for communicating with the provider of the specified
// interface.
//
// The channel will be configured with any needed metadata for communicating
// with the provider.
//
// If the interface or its connection details are missing from the resolved
// dependency, it falls back to using the runtime asset instance connection
// details.
absl::StatusOr<std::shared_ptr<grpc::Channel>>
ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    absl::string_view iface,
    const ::grpc::ChannelArguments& channel_args = ::grpc::ChannelArguments());

// Creates gRPC channels for communicating with the providers of the specified
// interfaces, in the same order as `ifaces`.
//
// The channels will be configured with any needed metadata for communicating
// with the providers. Interfaces that share the exact same address and metadata
// headers will share the same underlying `grpc::Channel` instance.
//
// For any interface where the interface or its connection details are missing
// from the resolved dependency, it falls back to using the runtime asset
// instance connection details.
template <size_t N>
absl::StatusOr<std::array<std::shared_ptr<grpc::Channel>, N>>
ConnectManyWithRuntimeAssetFallbackForAssetMigrationOnly(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    const absl::string_view (&ifaces)[N],
    const ::grpc::ChannelArguments& channel_args = ::grpc::ChannelArguments()) {
  std::array<std::shared_ptr<grpc::Channel>, N> channels;
  if (absl::Status s = internal::
          ConnectManyWithRuntimeAssetFallbackForAssetMigrationOnlyImpl(
              dep, ifaces, channel_args, absl::MakeSpan(channels));
      !s.ok()) {
    return s;
  }
  return channels;
}


// Retrieves the payload for the specified data interface.
absl::StatusOr<google::protobuf::Any> GetDataPayload(
    const intrinsic_proto::assets::v1::ResolvedDependency& dep,
    absl::string_view iface,
    intrinsic_proto::data::v1::DataAssets::StubInterface* data_assets_client =
        nullptr);

// Options for HasResolvedDependency.
struct ResolvedDepsIntrospectionOptions {
  bool check_dependency_annotation = false;
  bool check_skill_annotations = false;
  bool treat_any_as_true = false;
};

// Checks if the given proto has any ResolvedDependency fields.
//
// If additional introspection options are provided, the method returns true
// only if all of the options are satisfied.
bool HasResolvedDependency(const google::protobuf::Descriptor& descriptor,
                           const ResolvedDepsIntrospectionOptions& options);


// Returns all of the fallback_manifest_dependency_key values in the descriptor.
std::vector<std::string> FallbackManifestKeys(
    const google::protobuf::Descriptor& descriptor);


// LINT.ThenChange(
//  //intrinsic_sdk/intrinsic/assets/dependencies/utils.go:utils,
//  //intrinsic_sdk/intrinsic/assets/dependencies/utils.py:utils
// )

}  // namespace intrinsic::assets::dependencies

#endif  // INTRINSIC_ASSETS_DEPENDENCIES_UTILS_H_
