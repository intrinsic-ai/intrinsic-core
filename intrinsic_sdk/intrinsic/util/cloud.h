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

#ifndef INTRINSIC_UTIL_CLOUD_H_
#define INTRINSIC_UTIL_CLOUD_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/cloud/status.h"

namespace intrinsic {

const char kGcsUriPrefix[] = "gs://";

struct GcsBlobPathInformation {
  std::string bucket_name;
  std::string object_name;
  std::optional<int64_t> generation;

  template <typename GenerationType>
  GenerationType GenerationAs() const {
    return generation.has_value() ? GenerationType(*generation)
                                  : GenerationType();
  }
};

// Splits a GCS filename into a bucket name, an object name, and an optional
// generation id.
absl::StatusOr<GcsBlobPathInformation> GetGcsBlobPathInformationFromUri(
    absl::string_view gcs_uri);

// Converts a google cloud Status into an absl Status.
absl::Status ToAbsl(const ::google::cloud::Status& status);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_CLOUD_H_
