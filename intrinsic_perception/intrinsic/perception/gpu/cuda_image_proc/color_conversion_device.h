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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_COLOR_CONVERSION_DEVICE_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_COLOR_CONVERSION_DEVICE_H_

#include <cuda_runtime.h>

#include <algorithm>
#include <type_traits>

namespace intrinsic::perception {

// Converts a single RGB pixel to grayscale using BT.601 luminance weights
// (0.299 * R + 0.587 * G + 0.114 * B), rounding for integral output types and
// clamping to `[0, OutputImageTraits::kIntensityMax]`. Inputs and outputs are
// expected to share the same dynamic range.
template <typename OutputImageTraits, typename InputScalarType>
__device__ __forceinline__ typename OutputImageTraits::ScalarType
RgbPixelToGray(InputScalarType r_in, InputScalarType g_in,
               InputScalarType b_in) {
  const float r = static_cast<float>(r_in);
  const float g = static_cast<float>(g_in);
  const float b = static_cast<float>(b_in);
  float gray = 0.299f * r + 0.587f * g + 0.114f * b;
  if constexpr (std::is_integral_v<typename OutputImageTraits::ScalarType>) {
    gray += 0.5f;
  }
  gray = std::clamp(gray, 0.0f,
                    static_cast<float>(OutputImageTraits::kIntensityMax));
  return static_cast<typename OutputImageTraits::ScalarType>(gray);
}

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_COLOR_CONVERSION_DEVICE_H_
