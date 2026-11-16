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

#ifndef INTRINSIC_PERCEPTION_CORE_POST_PROCESSING_H_
#define INTRINSIC_PERCEPTION_CORE_POST_PROCESSING_H_

#include <cstdint>
#include <optional>
#include <ostream>

#include "absl/container/flat_hash_map.h"
#include "intrinsic/perception/core/rectangle.h"

namespace intrinsic::perception {

struct PostProcessing {
  std::optional<Rectangle> crop_region;
  std::optional<int32_t> cols;
  std::optional<int32_t> rows;
  bool skip_undistortion = false;
};

// A map from sensor id to post processing parameters.
using PostProcessingBySensorId = absl::flat_hash_map<int64_t, PostProcessing>;

std::ostream& operator<<(std::ostream& os, const PostProcessing& p);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CORE_POST_PROCESSING_H_
