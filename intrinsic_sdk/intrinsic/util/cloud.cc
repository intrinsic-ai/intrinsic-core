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

#include "intrinsic/util/cloud.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "google/cloud/status.h"

namespace intrinsic {

absl::StatusOr<GcsBlobPathInformation> GetGcsBlobPathInformationFromUri(
    absl::string_view gcs_uri) {
  absl::string_view stripped_path = absl::StripPrefix(gcs_uri, kGcsUriPrefix);
  if (stripped_path.size() == gcs_uri.size()) {
    return absl::InvalidArgumentError(
        "Invalid GCS URI provided. Expected format: "
        "gs://my_bucket/my_folder/my_blob.dat#12345678.");
  }

  // Get bucket name and object name.
  std::deque<std::string> path_parts =
      absl::StrSplit(stripped_path, absl::MaxSplits('/', 1));

  if (path_parts.size() < 2) {
    return absl::InvalidArgumentError(
        "Invalid GCS URI provided. No valid blob name.");
  }

  std::string bucket_name = path_parts.front();
  std::string object_name = path_parts.back();
  std::optional<int64_t> generation = std::nullopt;

  // Get generation if available.
  std::deque<std::string> object_parts = absl::StrSplit(object_name, '#');
  int64_t generation_id;
  if (object_parts.size() > 1 &&
      absl::SimpleAtoi(object_parts.back(), &generation_id)) {
    generation = generation_id;
    object_parts.pop_back();
    object_name = absl::StrJoin(object_parts, "#");
  }

  return GcsBlobPathInformation{.bucket_name = std::move(bucket_name),
                                .object_name = std::move(object_name),
                                .generation = std::move(generation)};
}

absl::Status ToAbsl(const google::cloud::Status& status) {
  // Note: Status codes are compatible.
  return {static_cast<absl::StatusCode>(status.code()), status.message()};
}

}  // namespace intrinsic
