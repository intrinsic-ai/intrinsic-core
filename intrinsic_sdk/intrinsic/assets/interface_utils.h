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

#ifndef INTRINSIC_ASSETS_INTERFACE_UTILS_H_
#define INTRINSIC_ASSETS_INTERFACE_UTILS_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace intrinsic {
namespace assets {

// The prefix used for gRPC service dependencies.
inline constexpr absl::string_view kGrpcUriPrefix = "grpc://";
// The prefix used for proto-based data dependencies.
inline constexpr absl::string_view kDataUriPrefix = "data://";

// Validates an interface name with a protocol prefix.
absl::Status ValidateInterfaceName(absl::string_view uri);

}  // namespace assets
}  // namespace intrinsic

#endif  // INTRINSIC_ASSETS_INTERFACE_UTILS_H_
