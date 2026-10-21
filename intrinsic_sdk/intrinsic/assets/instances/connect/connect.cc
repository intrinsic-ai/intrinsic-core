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

#include "intrinsic/assets/instances/connect/connect.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "intrinsic/assets/proto/v1/grpc_connection.pb.h"

namespace intrinsic::assets::instances::connect {

namespace {

// A gRPC MetadataCredentialsPlugin that attaches a fixed set of key-value
// metadata headers to every gRPC request made on a channel.
class HeaderMetadataPlugin : public grpc::MetadataCredentialsPlugin {
 public:
  explicit HeaderMetadataPlugin(
      std::vector<std::pair<std::string, std::string>> metadata)
      : metadata_(std::move(metadata)) {}

  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, std::string>* metadata) override {
    for (const auto& md : metadata_) {
      metadata->insert({md.first, md.second});
    }
    return grpc::Status::OK;
  }

 private:
  std::vector<std::pair<std::string, std::string>> metadata_;
};

}  // namespace

absl::StatusOr<std::shared_ptr<grpc::Channel>> Connect(
    const intrinsic_proto::assets::v1::GrpcConnection& connection,
    const ::grpc::ChannelArguments& channel_args) {
  std::vector<std::string> metadata_strs;
  metadata_strs.reserve(connection.metadata_size());
  for (const auto& metadata_proto : connection.metadata()) {
    metadata_strs.push_back(
        absl::StrCat(metadata_proto.key(), "=", metadata_proto.value()));
  }
  LOG(INFO) << "Connecting to gRPC address \"" << connection.address() << "\""
            << " with headers injected by MetadataCredentialsPlugin: ["
            << absl::StrJoin(metadata_strs, ", ") << "]";

  auto channel_creds = grpc::InsecureChannelCredentials();  // NOLINT(insecure)
  if (connection.metadata_size() > 0) {
    std::vector<std::pair<std::string, std::string>> metadata;
    metadata.reserve(connection.metadata_size());
    for (const auto& metadata_proto : connection.metadata()) {
      metadata.emplace_back(metadata_proto.key(), metadata_proto.value());
    }
    // Permitting GRPC_SECURITY_NONE is safe because this plugin only attaches
    // non-sensitive routing metadata headers rather than auth credentials.
    auto call_creds = grpc::experimental::MetadataCredentialsFromPlugin(
        std::make_unique<HeaderMetadataPlugin>(std::move(metadata)),
        /*min_security_level=*/GRPC_SECURITY_NONE);
    auto composite_creds =
        grpc::CompositeChannelCredentials(channel_creds, call_creds);
    return ::grpc::CreateCustomChannel(connection.address(), composite_creds,
                                       channel_args);
  }

  return ::grpc::CreateCustomChannel(
      connection.address(),
      grpc::InsecureChannelCredentials(),  // NOLINT(insecure)
      channel_args);
}

}  // namespace intrinsic::assets::instances::connect
