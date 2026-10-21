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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_PIXELWISE_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_PIXELWISE_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"

namespace intrinsic {
namespace perception {

// Casts `image` from InputImageTraits to OutputImageTraits. OutputImageTraits
// and InputImageTraits must have the same number of channels. When
// OutputImageTrais and InputImageTraits have the same ScalarType, a simple
// device-to-device copy is performed.
template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>> Cast(
    const CudaImage<InputImageTraits>& image);

extern template absl::StatusOr<CudaImage<Generic16i>> Cast(
    const CudaImage<Gray8u>& image);

extern template absl::StatusOr<CudaImage<Generic16i3>> Cast(
    const CudaImage<Rgb8u>& image);

extern template absl::StatusOr<CudaImage<Generic32f>> Cast(
    const CudaImage<Gray8u>& image);

extern template absl::StatusOr<CudaImage<Generic32f3>> Cast(
    const CudaImage<Rgb8u>& image);

extern template absl::StatusOr<CudaImage<Generic32f>> Cast(
    const CudaImage<Gray32f>& image);

extern template absl::StatusOr<CudaImage<Generic32f3>> Cast(
    const CudaImage<Rgb32f>& image);

extern template absl::StatusOr<CudaImage<Gray32f>> Cast(
    const CudaImage<Generic32f>& image);

extern template absl::StatusOr<CudaImage<Rgb32f>> Cast(
    const CudaImage<Generic32f3>& image);

extern template absl::StatusOr<CudaImage<Gray8u>> Cast(
    const CudaImage<Generic16i>& image);

extern template absl::StatusOr<CudaImage<Rgb8u>> Cast(
    const CudaImage<Generic16i3>& image);

extern template absl::StatusOr<CudaImage<Gray8u>> Cast(
    const CudaImage<Generic32f>& image);

extern template absl::StatusOr<CudaImage<Rgb8u>> Cast(
    const CudaImage<Generic32f3>& image);

// First rounds and then casts `image` from InputImageTraits (normally a
// floating point type) to OutputImageTraits (normally an integral type).
// OutputImageTraits and InputImageTraits must have the same number of channels.
template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>> RoundThenCast(
    const CudaImage<InputImageTraits>& image);

extern template absl::StatusOr<CudaImage<Gray8u>> RoundThenCast(
    const CudaImage<Generic32f>& image);

extern template absl::StatusOr<CudaImage<Rgb8u>> RoundThenCast(
    const CudaImage<Generic32f3>& image);

// Clamps `image` in-place to the range [min, max].
template <typename ImageTraits>
absl::Status Clamp(CudaImage<ImageTraits>& image,
                   typename ImageTraits::ScalarType min,
                   typename ImageTraits::ScalarType max);

extern template absl::Status Clamp(CudaImage<Generic16i>& image, int16_t min,
                                   int16_t max);

extern template absl::Status Clamp(CudaImage<Generic16i3>& image, int16_t min,
                                   int16_t max);

extern template absl::Status Clamp(CudaImage<Generic32f>& image, float min,
                                   float max);

extern template absl::Status Clamp(CudaImage<Generic32f3>& image, float min,
                                   float max);

extern template absl::Status Clamp(CudaImage<Gray32f>& image, float min,
                                   float max);

extern template absl::Status Clamp(CudaImage<Rgb32f>& image, float min,
                                   float max);

// Adds corresponding pixel values and creates image_a + image_b. Input and
// output images must have the same number of channels.
template <typename OutputImageTraits, typename ImageTraitsA,
          typename ImageTraitsB>
absl::StatusOr<CudaImage<OutputImageTraits>> Add(
    const CudaImage<ImageTraitsA>& image_a,
    const CudaImage<ImageTraitsB>& image_b);

extern template absl::StatusOr<CudaImage<Generic16i>> Add(
    const CudaImage<Generic16i>& image_a, const CudaImage<Generic16i>& image_b);

extern template absl::StatusOr<CudaImage<Generic16i3>> Add(
    const CudaImage<Generic16i3>& image_a,
    const CudaImage<Generic16i3>& image_b);

extern template absl::StatusOr<CudaImage<Generic32f>> Add(
    const CudaImage<Generic32f>& image_a, const CudaImage<Generic32f>& image_b);

extern template absl::StatusOr<CudaImage<Generic32f3>> Add(
    const CudaImage<Generic32f3>& image_a,
    const CudaImage<Generic32f3>& image_b);

// Subtracts corresponding pixel values and creates image_a - image_b. Input and
// output images must have the same number of channels.
template <typename OutputImageTraits, typename ImageTraitsA,
          typename ImageTraitsB>
absl::StatusOr<CudaImage<OutputImageTraits>> Subtract(
    const CudaImage<ImageTraitsA>& image_a,
    const CudaImage<ImageTraitsB>& image_b);

extern template absl::StatusOr<CudaImage<Generic16i>> Subtract(
    const CudaImage<Gray8u>& image_a, const CudaImage<Gray8u>& image_b);

extern template absl::StatusOr<CudaImage<Generic16i3>> Subtract(
    const CudaImage<Rgb8u>& image_a, const CudaImage<Rgb8u>& image_b);

extern template absl::StatusOr<CudaImage<Generic32f>> Subtract(
    const CudaImage<Generic32f>& image_a, const CudaImage<Generic32f>& image_b);

extern template absl::StatusOr<CudaImage<Generic32f3>> Subtract(
    const CudaImage<Generic32f3>& image_a,
    const CudaImage<Generic32f3>& image_b);

// Scales the values in the input `image` by the ratio
// OutputImageTraits::kIntensityMax / InputImageTraits::kIntensityMax into a
// pre-allocated `image_out` buffer (which must have the same dimensions as
// `image`, returning `absl::InvalidArgumentError` otherwise). If the output
// type is integral, the output value is rounded. Finally the output is clamped
// to ensure it has the [0..OutputImageTraits::kIntensityMax] range.
template <typename OutputImageTraits, typename InputImageTraits>
absl::Status ConvertImage(const CudaImage<InputImageTraits>& image,
                          CudaImage<OutputImageTraits>& image_out);

extern template absl::Status ConvertImage(const CudaImage<Gray8u>& image,
                                          CudaImage<Gray32f>& image_out);

extern template absl::Status ConvertImage(const CudaImage<Rgb8u>& image,
                                          CudaImage<Rgb32f>& image_out);

extern template absl::Status ConvertImage(const CudaImage<Gray32f>& image,
                                          CudaImage<Gray8u>& image_out);

extern template absl::Status ConvertImage(const CudaImage<Rgb32f>& image,
                                          CudaImage<Rgb8u>& image_out);

// Scales the values in the input `image` by the ratio
// OutputImageTraits::kIntensityMax / InputImageTraits::kIntensityMax. If the
// output type is integral, the output value is rounded. Finally the output is
// clamped to ensure it has the [0..OutputIntensityTraits::kIntensityMax] range.
template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>> ConvertImage(
    const CudaImage<InputImageTraits>& image);

extern template absl::StatusOr<CudaImage<Gray32f>> ConvertImage(
    const CudaImage<Gray8u>& image);

extern template absl::StatusOr<CudaImage<Rgb32f>> ConvertImage(
    const CudaImage<Rgb8u>& image);

extern template absl::StatusOr<CudaImage<Gray8u>> ConvertImage(
    const CudaImage<Gray32f>& image);

extern template absl::StatusOr<CudaImage<Rgb8u>> ConvertImage(
    const CudaImage<Rgb32f>& image);

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_PIXELWISE_H_
