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

#ifndef INTRINSIC_PERCEPTION_CORE_HDR_UTILS_H_
#define INTRINSIC_PERCEPTION_CORE_HDR_UTILS_H_

#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"

namespace intrinsic::perception {

// Enumerates the supported HDR methods.
enum class HdrOperator { kMertens };

// Merges a set of LDR frames into one HDR frame using Mertens fusion on the
// CPU.
// `ldr_images` must contain at least 2 non-null images with dimensions greater
// than 1x1 (returns `absl::StatusCode::kInvalidArgument` otherwise).
// `contrast_weight`, `saturation_weight`, and `exposure_weight` are the
// exponent weights for the Mertens contrast, saturation, and well-exposedness
// quality measures.
// Explicitly instantiated for `Gray8u` and `Rgb8u`.
template <typename ImageTraits>
absl::StatusOr<Image<ImageTraits>> ComputeHdrMertensCpu(
    const std::vector<const Image<ImageTraits>*>& ldr_images,
    float contrast_weight = 1.0f, float saturation_weight = 1.0f,
    float exposure_weight = 1.0f);

extern template absl::StatusOr<Image<Gray8u>> ComputeHdrMertensCpu(
    const std::vector<const Image<Gray8u>*>& ldr_images, float contrast_weight,
    float saturation_weight, float exposure_weight);

extern template absl::StatusOr<Image<Rgb8u>> ComputeHdrMertensCpu(
    const std::vector<const Image<Rgb8u>*>& ldr_images, float contrast_weight,
    float saturation_weight, float exposure_weight);

// Merges a set of LDR frames into one HDR frame.
template <typename ImageTraits>
absl::StatusOr<Image<ImageTraits>> LdrToHdr(
    const std::vector<const Image<ImageTraits>*>& ldr_images,
    HdrOperator hdr_operator);

extern template absl::StatusOr<Image<Gray8u>> LdrToHdr(
    const std::vector<const Image<Gray8u>*>& ldr_images,
    HdrOperator hdr_operator);

extern template absl::StatusOr<Image<Rgb8u>> LdrToHdr(
    const std::vector<const Image<Rgb8u>*>& ldr_images,
    HdrOperator hdr_operator);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CORE_HDR_UTILS_H_
