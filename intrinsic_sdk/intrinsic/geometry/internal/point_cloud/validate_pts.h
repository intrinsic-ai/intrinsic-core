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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_POINT_CLOUD_VALIDATE_PTS_H_
#define INTRINSIC_GEOMETRY_INTERNAL_POINT_CLOUD_VALIDATE_PTS_H_

#include <cstddef>
#include <istream>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace intrinsic::geo {

// Minimum number of bytes per point record in a PTS file (e.g., "0 0 0\n").
inline constexpr size_t kMinBytesPerPtsLine = 6;

// Validates that `num_vertices` is positive and does not exceed the capacity
// of a buffer with size `buffer_size`.
absl::Status ValidatePtsPointCount(int num_vertices, size_t buffer_size);

// Reads and validates the point count from `stream` given `buffer_size`.
absl::StatusOr<int> ParseAndValidatePtsPointCount(std::istream& stream,
                                                  size_t buffer_size);

// Reads and validates the point count from `file_content`.
absl::StatusOr<int> ParseAndValidatePtsPointCount(
    absl::string_view file_content);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_POINT_CLOUD_VALIDATE_PTS_H_
