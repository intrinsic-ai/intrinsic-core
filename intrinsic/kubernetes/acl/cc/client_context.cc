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

#include "intrinsic/kubernetes/acl/cc/client_context.h"

#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/string_ref.h"
#include "grpcpp/support/time.h"
#include "intrinsic/kubernetes/acl/cc/cookies.h"
#include "intrinsic/util/grpc/grpc.h"

namespace intrinsic {
namespace acl {

namespace {

constexpr char kAuthHeaderName[] = "authorization";
constexpr char kOrganizationHeaderName[] = "x-intrinsic-org";
constexpr char kApikeyTokenHeaderName[] = "apikey-token";

}  // namespace

std::unique_ptr<grpc::ClientContext> NewClientContext(
    const ::grpc::ServerContext* server_context, bool propagate_all_metadata) {
  std::unique_ptr<grpc::ClientContext> ctx;
  if (server_context && !server_context->client_metadata().empty()) {
    ctx = grpc::ClientContext::FromServerContext(*server_context);
    ctx = NewClientContext(std::move(ctx), server_context->client_metadata(),
                           propagate_all_metadata);
  } else {
    ctx = std::make_unique<grpc::ClientContext>();
  }
  ConfigureClientContext(ctx.get());
  return ctx;
}

std::unique_ptr<grpc::ClientContext> NewClientContext(
    std::unique_ptr<grpc::ClientContext> context,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    bool propagate_all_metadata) {
  if (propagate_all_metadata) {
    for (auto iter = metadata.begin(); iter != metadata.end(); ++iter) {
      context->AddMetadata(
          std::string(iter->first.data(), iter->first.size()),
          std::string(iter->second.data(), iter->second.size()));
    }
    return context;
  }
  auto cookies = metadata.equal_range(std::string(kCookieHeaderName));
  for (auto c = cookies.first; c != cookies.second; ++c) {
    context->AddMetadata(kCookieHeaderName,
                         std::string(c->second.data(), c->second.size()));
  }

  // Only propagate auth header if there is a single auth header to avoid
  // confusion.
  auto auth = metadata.equal_range(std::string(kAuthHeaderName));
  if (std::distance(auth.first, auth.second) == 1) {
    context->AddMetadata(
        kAuthHeaderName,
        std::string(auth.first->second.data(), auth.first->second.size()));
  }

  // Only propagate apikey token header if there is a single apikey header to
  // avoid confusion.
  auto apikey = metadata.equal_range(std::string(kApikeyTokenHeaderName));
  if (std::distance(apikey.first, apikey.second) == 1) {
    context->AddMetadata(
        kApikeyTokenHeaderName,
        std::string(apikey.first->second.data(), apikey.first->second.size()));
  }

  // Only propagate organization header if there is a single organization header
  // to avoid confusion.
  auto org = metadata.equal_range(std::string(kOrganizationHeaderName));
  if (std::distance(org.first, org.second) == 1) {
    context->AddMetadata(
        kOrganizationHeaderName,
        std::string(org.first->second.data(), org.first->second.size()));
  }

  return context;
}

std::unique_ptr<grpc::ClientContext> NewClientContextWithDeadline(
    const ::grpc::ServerContext* server_context, absl::Duration deadline,
    bool propagate_all_metadata) {
  std::unique_ptr<grpc::ClientContext> ctx =
      NewClientContext(server_context, propagate_all_metadata);
  ctx->set_deadline(::grpc::DeadlineFromDuration(deadline));
  return ctx;
}

/// GrpcMetadataUser.

// User identity represented as a copy of a gRPC context's metadata.
class GrpcMetadataUser : public User {
 public:
  explicit GrpcMetadataUser(const ::grpc::ServerContext& server_context) {
    for (auto iter = server_context.client_metadata().begin();
         iter != server_context.client_metadata().end(); ++iter) {
      grpc_context_metadata_.insert(
          {std::string(iter->first.data(), iter->first.size()),
           std::string(iter->second.data(), iter->second.size())});
    }
  }

  absl::StatusOr<UserIdentifier> GetUserIdentifier() const override;
  std::unique_ptr<grpc::ClientContext> NewClientContext() const override;

 private:
  std::multimap<std::string, std::string> grpc_context_metadata_;
};

absl::StatusOr<UserIdentifier> GrpcMetadataUser::GetUserIdentifier() const {
  return absl::UnimplementedError("not implemented");
}

std::unique_ptr<grpc::ClientContext> GrpcMetadataUser::NewClientContext()
    const {
  auto ctx = std::make_unique<grpc::ClientContext>();
  if (grpc_context_metadata_.empty()) {
    return ctx;
  }

  std::multimap<grpc::string_ref, grpc::string_ref> metadata;
  for (const auto& [key, value] : grpc_context_metadata_) {
    metadata.insert({grpc::string_ref(key), grpc::string_ref(value)});
  }

  return ::intrinsic::acl::NewClientContext(std::move(ctx), metadata);
}

std::unique_ptr<User> CreateGrpcMetadataUserFromServerContext(
    const ::grpc::ServerContext& server_context) {
  return std::make_unique<GrpcMetadataUser>(server_context);
}

}  // namespace acl
}  // namespace intrinsic
