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

#include "intrinsic/util/grpc/channel.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/auth_context.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/config.h"
#include "grpcpp/support/string_ref.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/frontend/cloud/api/v1/solutiondiscovery_api.grpc.pb.h"
#include "intrinsic/frontend/cloud/api/v1/solutiondiscovery_api.pb.h"
#include "intrinsic/kubernetes/acl/cc/cookie_names.h"
#include "intrinsic/kubernetes/acl/cc/header_names.h"
#include "intrinsic/util/grpc/auth.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {

class TokenPlugin : public grpc::MetadataCredentialsPlugin {
 public:
  explicit TokenPlugin(std::multimap<std::string, std::string> metadata)
      : metadata_(std::move(metadata)) {}

  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, std::string>* metadata) override {
    for (const auto& md : metadata_) {
      metadata->insert({std::string(md.first.data(), md.first.length()),
                        std::string(md.second.data(), md.second.length())});
    }
    return grpc::Status::OK;
  }

 private:
  std::multimap<std::string, std::string> metadata_;
};

class ServerNamePlugin : public grpc::MetadataCredentialsPlugin {
 public:
  explicit ServerNamePlugin(std::string_view server_name)
      : server_name_(server_name) {}
  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, std::string>* metadata) override {
    metadata->insert({acl::kXServerNameCookieName, server_name_});
    return grpc::Status::OK;
  }

 private:
  std::string server_name_;
};

class OrgNamePlugin : public grpc::MetadataCredentialsPlugin {
 public:
  explicit OrgNamePlugin(std::string_view org_id) : org_id_(org_id) {}
  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, std::string>* metadata) override {
    metadata->insert(
        {"cookie", absl::StrCat(acl::kOrgIDCookieName, "=", org_id_)});
    metadata->insert({acl::kOrganizationHeaderName, org_id_});
    return grpc::Status::OK;
  }

 private:
  std::string org_id_;
};

}  // namespace

absl::StatusOr<Channel::OrgInfo> Channel::OrgInfo::FromString(
    std::string_view org_project_str) {
  std::vector<std::string_view> parts = absl::StrSplit(org_project_str, '@');
  if (parts.size() != 2 || parts[0].empty() || parts[1].empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid OrgInfo format, expected ORG@PROJECT: ", org_project_str));
  }
  return OrgInfo{.org = std::string(parts[0]),
                 .project = std::string(parts[1])};
}

absl::StatusOr<std::shared_ptr<Channel>> Channel::MakeFromAddress(
    const ConnectionParams& params, absl::Duration timeout) {
  // Set the max message size to unlimited to allow longer trajectories.
  // Please check with the motion team before changing the value (see
  // b/275280379).
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<grpc::Channel> channel,
                        connect::CreateClientChannel(
                            params.address, absl::Now() + timeout,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return std::shared_ptr<Channel>(
      new Channel(channel, params.instance_name, params.header));
}

absl::StatusOr<std::shared_ptr<Channel>> Channel::MakeFromCluster(
    const OrgInfo& org_info, std::string_view cluster,
    std::string_view instance_name, std::string_view header,
    absl::Duration timeout) {
  INTR_ASSIGN_OR_RETURN(auto metadata,
                        intrinsic::auth::GetRequestMetadata(org_info.project));

  auto token_creds = grpc::MetadataCredentialsFromPlugin(
      std::make_unique<TokenPlugin>(metadata));
  auto org_creds = grpc::MetadataCredentialsFromPlugin(
      std::make_unique<OrgNamePlugin>(org_info.org));

  std::shared_ptr<grpc::CallCredentials> call_creds =
      grpc::CompositeCallCredentials(token_creds, org_creds);

  if (!cluster.empty()) {
    auto server_name_creds = grpc::MetadataCredentialsFromPlugin(
        std::make_unique<ServerNamePlugin>(cluster));
    call_creds = grpc::CompositeCallCredentials(call_creds, server_name_creds);
  }

  auto channel_creds = grpc::SslCredentials({});
  auto composite_channel_creds =
      grpc::CompositeChannelCredentials(channel_creds, call_creds);

  std::string address = absl::StrCat("dns:///www.endpoints.", org_info.project,
                                     ".cloud.goog:443");
  auto channel =
      grpc::CreateCustomChannel(address, composite_channel_creds,
                                connect::UnlimitedMessageSizeGrpcChannelArgs());
  if (!channel->WaitForConnected(absl::ToChronoTime(absl::Now() + timeout))) {
    return absl::UnavailableError(
        absl::StrCat("Could not connect to gRPC server at ", address,
                     ". The channel did not become ready by the deadline."));
  }

  return std::shared_ptr<Channel>(new Channel(channel, instance_name, header));
}

absl::StatusOr<std::shared_ptr<Channel>> Channel::MakeFromSolution(
    const OrgInfo& org_info, std::string_view solution_name,
    std::string_view instance_name, std::string_view header,
    absl::Duration timeout) {
  INTR_ASSIGN_OR_RETURN(
      auto discovery_channel,
      Channel::MakeFromCluster(org_info, /*cluster=*/"", /*instance_name=*/"",
                               /*header=*/"", timeout));

  using intrinsic_proto::frontend::v1::SolutionDiscoveryService;
  auto stub =
      SolutionDiscoveryService::NewStub(discovery_channel->GetChannel());

  ::intrinsic_proto::frontend::v1::GetSolutionDescriptionRequest request;
  request.set_name(solution_name);
  ::intrinsic_proto::frontend::v1::GetSolutionDescriptionResponse response;
  grpc::ClientContext context;
  ConfigureClientContext(&context);
  context.set_deadline(absl::ToChronoTime(absl::Now() + timeout));
  INTR_RETURN_IF_ERROR(intrinsic::ToAbslStatus(
      stub->GetSolutionDescription(&context, request, &response)));

  if (response.solution().cluster_name().empty()) {
    return absl::NotFoundError(absl::StrCat(
        "Could not find cluster for solution '", solution_name, "'"));
  }

  return Channel::MakeFromCluster(org_info, response.solution().cluster_name(),
                                  instance_name, header, timeout);
}

std::shared_ptr<grpc::Channel> Channel::GetChannel() const { return channel_; }

ClientContextFactory Channel::GetClientContextFactory() const {
  return [header = header_, instance_name = instance_name_]() {
    auto context = std::make_unique<::grpc::ClientContext>();
    ConfigureClientContext(context.get());
    if (!header.empty() && !instance_name.empty()) {
      context->AddMetadata(header, instance_name);
    }
    return context;
  };
}

Channel::Channel(std::shared_ptr<grpc::Channel> channel,
                 std::string_view instance_name, std::string_view header)
    : channel_(std::move(channel)),
      instance_name_(instance_name),
      header_(header) {}

}  // namespace intrinsic
