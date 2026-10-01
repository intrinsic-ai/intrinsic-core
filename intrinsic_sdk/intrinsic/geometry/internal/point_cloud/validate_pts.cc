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

#include "intrinsic/geometry/internal/point_cloud/validate_pts.h"

#include <cstddef>
#include <istream>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

absl::Status ValidatePtsPointCount(int num_vertices, size_t buffer_size) {
  if (num_vertices <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid point count: ", num_vertices));
  }
  if (static_cast<size_t>(num_vertices) > buffer_size / kMinBytesPerPtsLine) {
    return absl::InvalidArgumentError(
        absl::StrCat("Point count exceeds buffer capacity: ", num_vertices));
  }
  return absl::OkStatus();
}

absl::StatusOr<int> ParseAndValidatePtsPointCount(std::istream& stream,
                                                  size_t buffer_size) {
  int num_vertices = 0;
  stream >> num_vertices;
  if (stream.bad() || stream.fail()) {
    return absl::InvalidArgumentError("Bad point count");
  }
  INTR_RETURN_IF_ERROR(ValidatePtsPointCount(num_vertices, buffer_size));
  return num_vertices;
}

absl::StatusOr<int> ParseAndValidatePtsPointCount(
    absl::string_view file_content) {
  std::stringstream stream{std::string(file_content)};
  return ParseAndValidatePtsPointCount(stream, file_content.size());
}

}  // namespace intrinsic::geo
