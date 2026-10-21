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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_HDR_HDR_MERTENS_WEIGHT_MAP_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_HDR_HDR_MERTENS_WEIGHT_MAP_H_

#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"

namespace intrinsic {
namespace perception {

template <typename ImageTraits>
absl::StatusOr<CudaImage<Generic32f>> ComputeHdrMertensWeightMap(
    const CudaImage<ImageTraits>& image, float contrast_exponent = 1.0f,
    float saturation_exponent = 1.0f, float well_exposedness_exponent = 1.0f,
    BorderType border_type = BorderType::kBorderConstantZero);

extern template absl::StatusOr<CudaImage<Generic32f>>
ComputeHdrMertensWeightMap(const CudaImage<Gray8u>& image,
                           float contrast_exponent, float saturation_exponent,
                           float well_exposedness_exponent,
                           BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f>>
ComputeHdrMertensWeightMap(const CudaImage<Rgb8u>& image,
                           float contrast_exponent, float saturation_exponent,
                           float well_exposedness_exponent,
                           BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f>>
ComputeHdrMertensWeightMap(const CudaImage<Gray32f>& image,
                           float contrast_exponent, float saturation_exponent,
                           float well_exposedness_exponent,
                           BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f>>
ComputeHdrMertensWeightMap(const CudaImage<Rgb32f>& image,
                           float contrast_exponent, float saturation_exponent,
                           float well_exposedness_exponent,
                           BorderType border_type);

// Normalizes `weight_maps` (in-place) so that the sum at each pixel is 1.0.
absl::Status NormalizeWeightMaps(
    std::vector<CudaImage<Generic32f>>& weight_maps);

// Computes accumulator = accumulator + weight_map * image (if accumulate is
// true), or accumulator = weight_map * image (if accumulate is false).
// `weight_map` has 1 channel only.
// `image` can be 1 or more channels.
// `accumulator` must have float scalar type and the same number of channels as
// `image`.
template <typename ImageTraits, typename AccumulatorTrait = GenericTrait<
                                    float, ImageTraits::kNumChannels>>
absl::Status BlendWeightMapAndImage(const CudaImage<Generic32f>& weight_map,
                                    const CudaImage<ImageTraits>& image,
                                    bool accumulate,
                                    CudaImage<AccumulatorTrait>& accumulator);

template <typename ImageTraits, typename AccumulatorTrait = GenericTrait<
                                    float, ImageTraits::kNumChannels>>
absl::Status BlendWeightMapAndImage(const CudaImage<Generic32f>& weight_map,
                                    const CudaImage<ImageTraits>& image,
                                    CudaImage<AccumulatorTrait>& accumulator) {
  return BlendWeightMapAndImage(weight_map, image, /*accumulate=*/true,
                                accumulator);
}

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Generic16i>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Generic16i3>& image, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Generic32f>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Generic32f3>& image, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Gray8u>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb8u>& image,
    bool accumulate, CudaImage<Generic32f3>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Gray32f>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Gray32f>& image,
    bool accumulate, CudaImage<Gray32f>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb32f>& image,
    bool accumulate, CudaImage<Generic32f3>& accumulator);

extern template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb32f>& image,
    bool accumulate, CudaImage<Rgb32f>& accumulator);

// Fuses `PyrUp(image_down)`, `Subtract(image_curr, image_up)`, and
// `BlendWeightMapAndImage(weight_map, laplacian, accumulator)` into a single
// kernel pass without allocating intermediate `image_up` or `laplacian`
// buffers.
template <typename ImageTraits, typename AccumulatorTrait>
absl::Status PyrUpAndBlendLaplacian(const CudaImage<Generic32f>& weight_map,
                                    const CudaImage<ImageTraits>& image_curr,
                                    const CudaImage<ImageTraits>& image_down,
                                    BorderType border_type, bool accumulate,
                                    CudaImage<AccumulatorTrait>& accumulator);

extern template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Gray8u>& image_curr, const CudaImage<Gray8u>& image_down,
    BorderType border_type, bool accumulate,
    CudaImage<Generic32f>& accumulator);

extern template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb8u>& image_curr,
    const CudaImage<Rgb8u>& image_down, BorderType border_type, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

extern template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Gray32f>& image_curr, const CudaImage<Gray32f>& image_down,
    BorderType border_type, bool accumulate,
    CudaImage<Generic32f>& accumulator);

extern template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Gray32f>& image_curr, const CudaImage<Gray32f>& image_down,
    BorderType border_type, bool accumulate, CudaImage<Gray32f>& accumulator);

extern template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Rgb32f>& image_curr, const CudaImage<Rgb32f>& image_down,
    BorderType border_type, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

extern template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Rgb32f>& image_curr, const CudaImage<Rgb32f>& image_down,
    BorderType border_type, bool accumulate, CudaImage<Rgb32f>& accumulator);

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_HDR_HDR_MERTENS_WEIGHT_MAP_H_
