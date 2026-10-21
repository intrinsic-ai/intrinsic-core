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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_SMOOTH_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_SMOOTH_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"

namespace intrinsic {
namespace perception {

// Each pixel value in the output image is computed as the weighted sum of
// pixels in the neighborhood of a pixel in the input image, using the separable
// gaussian filter with coefficients [1.0, 4.0, 6.0, 4.0, 1.0]. Out-of-bound
// pixels will be extrapolated based on the `border_type` setting. The weights
// in the 2D filter normally sum to 1.0f but after the summation they are
// further multiplied by sum_weight. This can be useful for example to compute
// properly scaled pixel values after smoothing zero-pixels following a 2x2
// upsampling operation and in which case the sum_weight should be set to 4.0.
template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> SmoothImageWithGaussian5x5(
    const CudaImage<ImageTraits>& image,
    BorderType border_type = BorderType::kBorderConstantZero,
    float sum_weight = 1.0f);

extern template absl::StatusOr<CudaImage<Gray8u>>
SmoothImageWithGaussian5x5<Gray8u>(const CudaImage<Gray8u>& image,
                                   BorderType border_type, float sum_weight);

extern template absl::StatusOr<CudaImage<Rgb8u>>
SmoothImageWithGaussian5x5<Rgb8u>(const CudaImage<Rgb8u>& image,
                                  BorderType border_type, float sum_weight);

extern template absl::StatusOr<CudaImage<Gray32f>>
SmoothImageWithGaussian5x5<Gray32f>(const CudaImage<Gray32f>& image,
                                    BorderType border_type, float sum_weight);

extern template absl::StatusOr<CudaImage<Generic32f>>
SmoothImageWithGaussian5x5<Generic32f>(const CudaImage<Generic32f>& image,
                                       BorderType border_type,
                                       float sum_weight);

extern template absl::StatusOr<CudaImage<Generic32f3>>
SmoothImageWithGaussian5x5<Generic32f3>(const CudaImage<Generic32f3>& image,
                                        BorderType border_type,
                                        float sum_weight);

extern template absl::StatusOr<CudaImage<Generic16i>>
SmoothImageWithGaussian5x5<Generic16i>(const CudaImage<Generic16i>& image,
                                       BorderType border_type,
                                       float sum_weight);

extern template absl::StatusOr<CudaImage<Generic16i3>>
SmoothImageWithGaussian5x5<Generic16i3>(const CudaImage<Generic16i3>& image,
                                        BorderType border_type,
                                        float sum_weight);

// Smooths the input image using the separable 5x5 Gaussian filter with
// coefficients [1.0, 4.0, 6.0, 4.0, 1.0] / 16.0 (out-of-bound pixels are
// extrapolated based on `border_type`), followed by 2x2 downsampling into a
// pre-allocated `image_down` buffer.
template <typename ImageTraits>
absl::Status SmoothWithGaussian5x5AndDownsample2x2(
    const CudaImage<ImageTraits>& image, CudaImage<ImageTraits>& image_down,
    BorderType border_type = BorderType::kBorderConstantZero);

extern template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Gray8u>(
    const CudaImage<Gray8u>& image, CudaImage<Gray8u>& image_down,
    BorderType border_type);

extern template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Rgb8u>(
    const CudaImage<Rgb8u>& image, CudaImage<Rgb8u>& image_down,
    BorderType border_type);

extern template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Gray32f>(
    const CudaImage<Gray32f>& image, CudaImage<Gray32f>& image_down,
    BorderType border_type);

extern template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Rgb32f>(
    const CudaImage<Rgb32f>& image, CudaImage<Rgb32f>& image_down,
    BorderType border_type);

extern template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Generic32f>(
    const CudaImage<Generic32f>& image, CudaImage<Generic32f>& image_down,
    BorderType border_type);

extern template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Generic32f3>(
    const CudaImage<Generic32f3>& image, CudaImage<Generic32f3>& image_down,
    BorderType border_type);

// Fused 5x5 Gaussian smoothing (sum_weight = 1.0) followed by 2x2 downsampling.
template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> SmoothWithGaussian5x5AndDownsample2x2(
    const CudaImage<ImageTraits>& image,
    BorderType border_type = BorderType::kBorderConstantZero);

extern template absl::StatusOr<CudaImage<Gray8u>>
SmoothWithGaussian5x5AndDownsample2x2<Gray8u>(const CudaImage<Gray8u>& image,
                                              BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb8u>>
SmoothWithGaussian5x5AndDownsample2x2<Rgb8u>(const CudaImage<Rgb8u>& image,
                                             BorderType border_type);

extern template absl::StatusOr<CudaImage<Gray32f>>
SmoothWithGaussian5x5AndDownsample2x2<Gray32f>(const CudaImage<Gray32f>& image,
                                               BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb32f>>
SmoothWithGaussian5x5AndDownsample2x2<Rgb32f>(const CudaImage<Rgb32f>& image,
                                              BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f>>
SmoothWithGaussian5x5AndDownsample2x2<Generic32f>(
    const CudaImage<Generic32f>& image, BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f3>>
SmoothWithGaussian5x5AndDownsample2x2<Generic32f3>(
    const CudaImage<Generic32f3>& image, BorderType border_type);

// Fused 2x2 zero-interleaved upsampling followed by 5x5 Gaussian smoothing
// (sum_weight = 4.0) into a pre-allocated `image_up_smooth` buffer. If
// `accumulate` is true, adds the result to `image_up_smooth` in-place.
template <typename ImageTraits>
absl::Status Upsample2x2AndSmoothWithGaussian5x5(
    const CudaImage<ImageTraits>& image,
    CudaImage<ImageTraits>& image_up_smooth,
    BorderType border_type = BorderType::kBorderConstantZero,
    bool accumulate = false);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Gray8u>(
    const CudaImage<Gray8u>& image, CudaImage<Gray8u>& image_up_smooth,
    BorderType border_type, bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Rgb8u>(
    const CudaImage<Rgb8u>& image, CudaImage<Rgb8u>& image_up_smooth,
    BorderType border_type, bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Gray32f>(
    const CudaImage<Gray32f>& image, CudaImage<Gray32f>& image_up_smooth,
    BorderType border_type, bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Rgb32f>(
    const CudaImage<Rgb32f>& image, CudaImage<Rgb32f>& image_up_smooth,
    BorderType border_type, bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic16i>(
    const CudaImage<Generic16i>& image, CudaImage<Generic16i>& image_up_smooth,
    BorderType border_type, bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic16i3>(
    const CudaImage<Generic16i3>& image,
    CudaImage<Generic16i3>& image_up_smooth, BorderType border_type,
    bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic32f>(
    const CudaImage<Generic32f>& image, CudaImage<Generic32f>& image_up_smooth,
    BorderType border_type, bool accumulate);

extern template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic32f3>(
    const CudaImage<Generic32f3>& image,
    CudaImage<Generic32f3>& image_up_smooth, BorderType border_type,
    bool accumulate);

// Fused 2x2 zero-interleaved upsampling followed by 5x5 Gaussian smoothing
// (sum_weight = 4.0).
template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> Upsample2x2AndSmoothWithGaussian5x5(
    const CudaImage<ImageTraits>& image, Dimensions dimensions_up = {},
    BorderType border_type = BorderType::kBorderConstantZero);

extern template absl::StatusOr<CudaImage<Gray8u>>
Upsample2x2AndSmoothWithGaussian5x5<Gray8u>(const CudaImage<Gray8u>& image,
                                            Dimensions dimensions_up,
                                            BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb8u>>
Upsample2x2AndSmoothWithGaussian5x5<Rgb8u>(const CudaImage<Rgb8u>& image,
                                           Dimensions dimensions_up,
                                           BorderType border_type);

extern template absl::StatusOr<CudaImage<Gray32f>>
Upsample2x2AndSmoothWithGaussian5x5<Gray32f>(const CudaImage<Gray32f>& image,
                                             Dimensions dimensions_up,
                                             BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb32f>>
Upsample2x2AndSmoothWithGaussian5x5<Rgb32f>(const CudaImage<Rgb32f>& image,
                                            Dimensions dimensions_up,
                                            BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic16i>>
Upsample2x2AndSmoothWithGaussian5x5<Generic16i>(
    const CudaImage<Generic16i>& image, Dimensions dimensions_up,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic16i3>>
Upsample2x2AndSmoothWithGaussian5x5<Generic16i3>(
    const CudaImage<Generic16i3>& image, Dimensions dimensions_up,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f>>
Upsample2x2AndSmoothWithGaussian5x5<Generic32f>(
    const CudaImage<Generic32f>& image, Dimensions dimensions_up,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Generic32f3>>
Upsample2x2AndSmoothWithGaussian5x5<Generic32f3>(
    const CudaImage<Generic32f3>& image, Dimensions dimensions_up,
    BorderType border_type);

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_SMOOTH_H_
