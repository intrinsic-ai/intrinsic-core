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

#include "intrinsic/util/grpc/auth.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/config/environments.h"
#include "nlohmann/json.hpp"

namespace intrinsic {
namespace auth {

using json = nlohmann::json;

// LINT.IfChange
constexpr std::string_view kStoreDirectory = "intrinsic/projects";
constexpr std::string_view kEnvStoreDirectory = "intrinsic/environments";
constexpr std::string_view kAuthConfigExtension = ".user-token";
constexpr std::string_view kAliasDefaultToken = "default";
// LINT.ThenChange(//intrinsic_sdk/intrinsic/tools/inctl/auth/auth.go,
// //intrinsic_sdk/intrinsic/util/grpc/auth.py)

// Retrieves the API key for project `project_name`.
// It first infers the environment of the project from its name and tries to
// get the API key for the environment.
// If there's no API key for the environment, it falls back to the project key.
absl::StatusOr<std::string> GetApiKey(std::string_view project_name) {
  const char* home_dir = std::getenv("HOME");
  if (home_dir == nullptr) {
    return absl::NotFoundError("$HOME environment variable not set.");
  }

  std::string file_name;
  std::ifstream f;

  const std::string resolved_env = environments::FromAnyProject(project_name);
  const std::filesystem::path config_dir =
      std::filesystem::path(home_dir) / ".config";
  file_name = (config_dir / kEnvStoreDirectory /
               absl::StrCat(resolved_env, kAuthConfigExtension))
                  .string();
  f.open(file_name);

  // TODO: (b/530456416) remove fallback to project
  if (!f.is_open()) {
    file_name = (config_dir / kStoreDirectory /
                 absl::StrCat(project_name, kAuthConfigExtension))
                    .string();
    f.open(file_name);
  }

  if (!f.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Could not open project token for ", project_name));
  }

  json data = json::parse(f, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (data.is_discarded()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Could not parse ", file_name));
  }

  if (!data.contains("tokens") ||
      !data["tokens"].contains(kAliasDefaultToken) ||
      !data["tokens"][kAliasDefaultToken].contains("apiKey")) {
    return absl::FailedPreconditionError(
        absl::StrCat("Could not find default token in ", file_name,
                     ". Please run 'inctl auth login'"));
  }

  if (!data["tokens"][kAliasDefaultToken]["apiKey"].is_string()) {
    return absl::FailedPreconditionError(
        absl::StrCat("apiKey in default token is not a string in ", file_name));
  }

  return data["tokens"][kAliasDefaultToken]["apiKey"].get<std::string>();
}

absl::StatusOr<std::multimap<std::string, std::string>> GetRequestMetadata(
    std::string_view project_name) {
  absl::StatusOr<std::string> api_key = GetApiKey(project_name);
  if (!api_key.ok()) return api_key.status();

  std::multimap<std::string, std::string> metadata;
  metadata.insert({"authorization", absl::StrCat("Bearer ", *api_key)});
  return metadata;
}

}  // namespace auth
}  // namespace intrinsic
