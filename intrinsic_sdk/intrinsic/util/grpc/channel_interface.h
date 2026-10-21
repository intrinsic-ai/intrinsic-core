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

#ifndef INTRINSIC_UTIL_GRPC_CHANNEL_INTERFACE_H_
#define INTRINSIC_UTIL_GRPC_CHANNEL_INTERFACE_H_

#include <functional>
#include <memory>

#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"

namespace intrinsic {

// Factory function that produces a ::grpc::ClientContext.
using ClientContextFactory =
    std::function<std::unique_ptr<::grpc::ClientContext>()>;

// Returns `std::make_unique<::grpc::ClientContext>()`.
std::unique_ptr<::grpc::ClientContext> DefaultClientContextFactory();

// A channel to an Intrinsic gRPC service.
//
// Internally, it can contain a specific address, a specific resource
// instance (i.e. one of multiple real-time control services), or a connection
// to an in-process fake server.
class ChannelInterface {
 public:
  virtual ~ChannelInterface() = default;

  // Returns a grpc::Channel to the server.
  virtual std::shared_ptr<grpc::Channel> GetChannel() const = 0;

  // Returns a factory function that produces a ::grpc::ClientContext. By
  // default, uses `std::make_unique<::grpc::ClientContext>`. This may be
  // overridden in order to set client metadata, or other ClientContext
  // settings, for all requests that use this channel.
  virtual ClientContextFactory GetClientContextFactory() const {
    return DefaultClientContextFactory;
  }
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_GRPC_CHANNEL_INTERFACE_H_
