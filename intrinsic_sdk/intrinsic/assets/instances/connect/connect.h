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

#ifndef INTRINSIC_ASSETS_INSTANCES_CONNECT_CONNECT_H_
#define INTRINSIC_ASSETS_INSTANCES_CONNECT_CONNECT_H_

#include <memory>

#include "absl/status/statusor.h"
#include "grpcpp/channel.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/assets/proto/v1/grpc_connection.pb.h"

namespace intrinsic::assets::instances::connect {

// Creates a gRPC channel for communicating with the provider specified by
// the GrpcConnection message.
absl::StatusOr<std::shared_ptr<grpc::Channel>> Connect(
    const intrinsic_proto::assets::v1::GrpcConnection& connection,
    const ::grpc::ChannelArguments& channel_args = ::grpc::ChannelArguments());

}  // namespace intrinsic::assets::instances::connect

#endif  // INTRINSIC_ASSETS_INSTANCES_CONNECT_CONNECT_H_
