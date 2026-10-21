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

#ifndef INTRINSIC_UTIL_GRPC_AUTH_H_
#define INTRINSIC_UTIL_GRPC_AUTH_H_

#include <map>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace intrinsic {
namespace auth {

// Reads project credentials and returns metadata for gRPC authentication.
absl::StatusOr<std::multimap<std::string, std::string>> GetRequestMetadata(
    std::string_view project_name);

// Retrieves the API key for project `project_name`.
// It first infers the environment of the project from its name and tries to
// get the API key for the environment.
// If there's no API key for the environment, it falls back to the project key.
absl::StatusOr<std::string> GetApiKey(std::string_view project_name);

}  // namespace auth
}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_GRPC_AUTH_H_
