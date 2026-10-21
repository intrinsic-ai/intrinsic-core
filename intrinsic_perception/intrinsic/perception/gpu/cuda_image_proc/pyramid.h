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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_PYRAMID_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_PYRAMID_H_

#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation.h"
#include "intrinsic/perception/gpu/cuda_image_proc/resize.h"
#include "intrinsic/perception/gpu/cuda_image_proc/smooth.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {

// Smooths the input image with a 5x5 gaussian kernel and then down-samples it
// by a factor of 0.5 to form a new image with dimensions
// Ceil((cols / 2.0)) x Ceil((rows / 2.0))
template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> PyrDown(
    const CudaImage<ImageTraits>& image,
    BorderType border_type = BorderType::kBorderConstantZero) {
  return SmoothWithGaussian5x5AndDownsample2x2(image, border_type);
}

// Smooths the input image with a 5x5 gaussian kernel and down-samples it by a
// factor of 0.5 into a pre-allocated `image_down` buffer with dimensions
// Ceil((cols / 2.0)) x Ceil((rows / 2.0)).
template <typename ImageTraits>
absl::Status PyrDown(const CudaImage<ImageTraits>& image,
                     CudaImage<ImageTraits>& image_down,
                     BorderType border_type = BorderType::kBorderConstantZero) {
  return SmoothWithGaussian5x5AndDownsample2x2(image, image_down, border_type);
}

// Downsamples the input `dimensions` by a factor of 0.5 and returns
// Dimensions(Ceil((cols / 2.0)), Ceil((rows / 2.0))).
inline Dimensions DimensionsPyrDown(const Dimensions& dimensions) {
  return DimensionsDownsample2x2(dimensions);
}

// Up-samples an image with dimensions cols x rows to (cols * 2) x (rows * 2)
// and then smooths it with a 5x5 gaussian kernel. Optionally, we can directly
// specify 'dimensions_up' to select whether a row or a column should be dropped
// which can result in odd number of columns or rows in the returned image. This
// can be useful in a pyramid downsampling and upsampling scheme to ensure that
// all corresponding images have equal dimensions. 'dimensions_up' can be
// specified using any combination of (cols * 2 - i) x (rows * 2 - j) where the
// integers i and j can be either 0 or 1.
template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> PyrUp(
    const CudaImage<ImageTraits>& image, Dimensions dimensions_up = {},
    BorderType border_type = BorderType::kBorderConstantZero) {
  return Upsample2x2AndSmoothWithGaussian5x5(image, dimensions_up, border_type);
}

// Up-samples an image with dimensions cols x rows and smooths it with a 5x5
// gaussian kernel into a pre-allocated `image_up` buffer. If `accumulate` is
// true, adds the result to `image_up` in-place.
template <typename ImageTraits>
absl::Status PyrUp(const CudaImage<ImageTraits>& image,
                   CudaImage<ImageTraits>& image_up,
                   BorderType border_type = BorderType::kBorderConstantZero,
                   bool accumulate = false) {
  return Upsample2x2AndSmoothWithGaussian5x5(image, image_up, border_type,
                                             accumulate);
}

// Creates a gaussian pyramid by applying the smooth-and-downsample operation
// recursively while ensuring that the coarsest image in the resulting pyramid
// has dimensions of at least `min_dimensions`. The highest resolution image in
// the resulting pyramid is the original image (with no smoothing).
template <typename ImageTraits>
absl::StatusOr<std::vector<CudaImage<ImageTraits>>> CreateGaussianPyramid(
    const CudaImage<ImageTraits>& image, Dimensions min_dimensions = {1, 1},
    BorderType border_type = BorderType::kBorderConstantZero) {
  std::vector<CudaImage<ImageTraits>> gaussian_pyramid;
  if (min_dimensions.cols < 1 || min_dimensions.rows < 1) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "min_dimensions must be at least 1 x 1 (cols x rows). (was %d x %d)",
        min_dimensions.cols, min_dimensions.rows));
  }
  if (min_dimensions.cols > image.cols() ||
      min_dimensions.rows > image.rows()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "min_dimensions must be at most the same as the dimensions %d x %d "
        "(cols x rows) of the input image. (min_dimensions was %d x %d)",
        image.cols(), image.rows(), min_dimensions.cols, min_dimensions.rows));
  }
  gaussian_pyramid.push_back(image);
  while (true) {
    Dimensions dimensions_down =
        DimensionsPyrDown(gaussian_pyramid.back().dimensions());
    if (dimensions_down.cols < min_dimensions.cols ||
        dimensions_down.rows < min_dimensions.rows ||
        dimensions_down.cols == 1 || dimensions_down.rows == 1) {
      break;
    }
    INTR_ASSIGN_OR_RETURN(CudaImage<ImageTraits> image_cur,
                          PyrDown(gaussian_pyramid.back(), border_type));
    INTR_RET_CHECK_EQ(dimensions_down, image_cur.dimensions())
        << "The predicted dimensions are different from the actual "
           "dimensions of the downsampled image.";
    gaussian_pyramid.push_back(std::move(image_cur));
  }
  return gaussian_pyramid;
}

// Creates a laplacian pyramid which is implemented using the DoG approximation
// (Difference of Gaussians) instead of using the non-separable Laplacian
// filter.
template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<std::vector<CudaImage<OutputImageTraits>>>
CreateLaplacianPyramid(
    const CudaImage<InputImageTraits>& image,
    Dimensions min_dimensions = {1, 1},
    BorderType border_type = BorderType::kBorderConstantZero);

extern template absl::StatusOr<std::vector<CudaImage<Generic16i>>>
CreateLaplacianPyramid(const CudaImage<Gray8u>& image,
                       Dimensions min_dimensions, BorderType border_type);

extern template absl::StatusOr<std::vector<CudaImage<Generic16i3>>>
CreateLaplacianPyramid(const CudaImage<Rgb8u>& image, Dimensions min_dimensions,
                       BorderType border_type);

extern template absl::StatusOr<std::vector<CudaImage<Generic32f>>>
CreateLaplacianPyramid(const CudaImage<Generic32f>& image,
                       Dimensions min_dimensions, BorderType border_type);

extern template absl::StatusOr<std::vector<CudaImage<Generic32f3>>>
CreateLaplacianPyramid(const CudaImage<Generic32f3>& image,
                       Dimensions min_dimensions, BorderType border_type);

// Reconstructs an image from the input `laplacian_pyramid`.
template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<InputImageTraits>>& laplacian_pyramid,
    BorderType border_type = BorderType::kBorderConstantZero);

extern template absl::StatusOr<CudaImage<Gray8u>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<Generic16i>>& laplacian_pyramid,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb8u>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<Generic16i3>>& laplacian_pyramid,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Gray8u>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<Generic32f>>& laplacian_pyramid,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb8u>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<Generic32f3>>& laplacian_pyramid,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Gray32f>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<Generic32f>>& laplacian_pyramid,
    BorderType border_type);

extern template absl::StatusOr<CudaImage<Rgb32f>>
ReconstructImageFromLaplacianPyramid(
    const std::vector<CudaImage<Generic32f3>>& laplacian_pyramid,
    BorderType border_type);

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_PYRAMID_H_
