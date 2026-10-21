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

#include "intrinsic/perception/gpu/cuda_image_proc/smooth.h"

#include <cuda_runtime_api.h>
#include <device_types.h>
#include <vector_types.h>

#include <algorithm>
#include <limits>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation_device.h"
#include "intrinsic/perception/gpu/cuda_image_proc/smooth_device.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_math.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {
namespace {

template <typename ImageTraits>
__global__ void SmoothImageWithGaussian5x5Kernel(
    const typename ImageTraits::ScalarType* image_src,
    typename ImageTraits::ScalarType* image_dst, int cols, int rows,
    BorderType border_type, float sum_weight) {
  constexpr int kNumChannels = ImageTraits::kNumChannels;

  constexpr int kHalfKernelWidth = kGaussian5x5HalfKernelWidth;
  constexpr int kPaddedBlockWidth = kGaussian5x5BlockSizePadded;

  // Compute global image row and col coordinates for this thread id.
  const int image_col =
      blockIdx.x * kGaussian5x5BlockSize + threadIdx.x - kHalfKernelWidth;
  const int image_row =
      blockIdx.y * kGaussian5x5BlockSize + threadIdx.y - kHalfKernelWidth;

  __shared__ typename ImageTraits::ScalarType
      block_src[kPaddedBlockWidth * kPaddedBlockWidth];
  __shared__ float block_src_temp[kPaddedBlockWidth * kPaddedBlockWidth];

  for (int i = 0; i < kNumChannels; ++i) {
    typename ImageTraits::ScalarType src_pix =
        static_cast<ImageTraits::ScalarType>(0);
    int image_col_extrap =
        GetExtrapolatedPixelIndex(image_col, cols, border_type);
    int image_row_extrap =
        GetExtrapolatedPixelIndex(image_row, rows, border_type);
    if (image_col_extrap >= 0 && image_row_extrap >= 0) {
      src_pix = image_src[image_row_extrap * cols * kNumChannels +
                          image_col_extrap * kNumChannels + i];
    }
    block_src[threadIdx.y * kPaddedBlockWidth + threadIdx.x] = src_pix;
    __syncthreads();

    // Computes convolution in x direction. This convolution is performed by
    // all threads whose x coordinate falls into the un-padded region of size
    // kGaussian5x5BlockSize x kGaussian5x5BlockSize, but whose y coordinate
    // extends top and bottom into the padded region.
    if (threadIdx.x >= kHalfKernelWidth &&
        threadIdx.x < kHalfKernelWidth + kGaussian5x5BlockSize) {
      block_src_temp[threadIdx.y * kPaddedBlockWidth + threadIdx.x] =
          ConvolveGaussian5x5Row(block_src, threadIdx.x, threadIdx.y);
    }
    __syncthreads();

    // Computes convolution in y direction. This convolution is performed by
    // all threads whose x and y coordinates fall into the un-padded region of
    // size kGaussian5x5BlockSize x kGaussian5x5BlockSize.
    if (IsGaussian5x5InteriorThread()) {
      const float sum =
          ConvolveGaussian5x5Col(block_src_temp, threadIdx.x, threadIdx.y);

      if (image_col >= 0 && image_col < cols && image_row >= 0 &&
          image_row < rows) {
        image_dst[image_row * cols * kNumChannels + image_col * kNumChannels +
                  i] = static_cast<ImageTraits::ScalarType>(sum_weight * sum);
      }
    }
    __syncthreads();
  }
}

absl::Status ValidateDownsample2x2InputDimensions(Dimensions dims) {
  if (dims.cols < 2 || dims.rows < 2) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Input image must have size at least 2x2 (cols x "
                        "rows), but it had size %d x %d.",
                        dims.cols, dims.rows));
  }
  return absl::OkStatus();
}

absl::Status ValidateUpsample2x2Dimensions(Dimensions dims,
                                           Dimensions dimensions_up) {
  if (dims.cols < 1 || dims.rows < 1) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Input image must have size at least 1x1 (cols x "
                        "rows), but it had size %d x %d.",
                        dims.cols, dims.rows));
  }
  if (dimensions_up.cols < 2 || dimensions_up.rows < 2) {
    return absl::InvalidArgumentError(
        absl::StrFormat("dimensions_up must have size at least 2x2 (cols x "
                        "rows), but it had size %d x %d.",
                        dimensions_up.cols, dimensions_up.rows));
  }
  if (dimensions_up.cols != (dims.cols * 2) &&
      dimensions_up.cols != ((dims.cols * 2) - 1)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "dimensions_up cols must be %d or %d (was %d)", dims.cols * 2,
        (dims.cols * 2) - 1, dimensions_up.cols));
  }
  if (dimensions_up.rows != (dims.rows * 2) &&
      dimensions_up.rows != ((dims.rows * 2) - 1)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "dimensions_up rows must be %d or %d (was %d)", dims.rows * 2,
        (dims.rows * 2) - 1, dimensions_up.rows));
  }
  return absl::OkStatus();
}

template <typename ImageTraits>
__global__ void SmoothWithGaussian5x5AndDownsample2x2Kernel(
    const typename ImageTraits::ScalarType* image_src, int cols, int rows,
    typename ImageTraits::ScalarType* image_down, int cols_down,
    BorderType border_type) {
  using ScalarType = typename ImageTraits::ScalarType;
  constexpr int kNumChannels = ImageTraits::kNumChannels;

  const int image_col = static_cast<int>(blockIdx.x * kGaussian5x5BlockSize) +
                        static_cast<int>(threadIdx.x) -
                        kGaussian5x5HalfKernelWidth;
  const int image_row = static_cast<int>(blockIdx.y * kGaussian5x5BlockSize) +
                        static_cast<int>(threadIdx.y) -
                        kGaussian5x5HalfKernelWidth;
  const int image_col_extrap =
      GetExtrapolatedPixelIndex(image_col, cols, border_type);
  const int image_row_extrap =
      GetExtrapolatedPixelIndex(image_row, rows, border_type);

  __shared__ ScalarType
      block_src[kGaussian5x5BlockSizePadded * kGaussian5x5BlockSizePadded];
  __shared__ float
      block_src_temp[kGaussian5x5BlockSizePadded * kGaussian5x5BlockSizePadded];

  const bool compute_x =
      (threadIdx.x >= kGaussian5x5HalfKernelWidth &&
       threadIdx.x < kGaussian5x5HalfKernelWidth + kGaussian5x5BlockSize &&
       ((image_col & 1) == 0));
  const bool compute_y =
      (compute_x && threadIdx.y >= kGaussian5x5HalfKernelWidth &&
       threadIdx.y < kGaussian5x5HalfKernelWidth + kGaussian5x5BlockSize &&
       ((image_row & 1) == 0));
  const bool is_active_output = compute_y && image_col >= 0 &&
                                image_col < cols && image_row >= 0 &&
                                image_row < rows;
  const int col_down = image_col >> 1;
  const int row_down = image_row >> 1;

  for (int chan = 0; chan < kNumChannels; ++chan) {
    ScalarType src_pix = static_cast<ScalarType>(0);
    if (image_col_extrap >= 0 && image_row_extrap >= 0) {
      src_pix = image_src[(image_row_extrap * cols * kNumChannels) +
                          (image_col_extrap * kNumChannels) + chan];
    }
    block_src[(threadIdx.y * kGaussian5x5BlockSizePadded) + threadIdx.x] =
        src_pix;
    __syncthreads();

    if (compute_x) {
      block_src_temp[(threadIdx.y * kGaussian5x5BlockSizePadded) +
                     threadIdx.x] =
          ConvolveGaussian5x5Row(block_src, threadIdx.x, threadIdx.y);
    }
    __syncthreads();

    if (is_active_output) {
      const float sum =
          ConvolveGaussian5x5Col(block_src_temp, threadIdx.x, threadIdx.y);
      image_down[(row_down * cols_down * kNumChannels) +
                 (col_down * kNumChannels) + chan] =
          static_cast<ScalarType>(sum);
    }
    __syncthreads();
  }
}

template <typename ImageTraits, bool kAccumulate>
__global__ void Upsample2x2AndSmoothWithGaussian5x5Kernel(
    const typename ImageTraits::ScalarType* image_src, int cols,
    typename ImageTraits::ScalarType* image_dst, int cols_up, int rows_up,
    BorderType border_type) {
  using ScalarType = typename ImageTraits::ScalarType;
  constexpr int kNumChannels = ImageTraits::kNumChannels;

  const int image_col = static_cast<int>(blockIdx.x * kGaussian5x5BlockSize) +
                        static_cast<int>(threadIdx.x) -
                        kGaussian5x5HalfKernelWidth;
  const int image_row = static_cast<int>(blockIdx.y * kGaussian5x5BlockSize) +
                        static_cast<int>(threadIdx.y) -
                        kGaussian5x5HalfKernelWidth;
  const int image_col_extrap =
      GetExtrapolatedPixelIndex(image_col, cols_up, border_type);
  const int image_row_extrap =
      GetExtrapolatedPixelIndex(image_row, rows_up, border_type);
  const bool has_non_zero_src =
      (image_col_extrap >= 0 && image_row_extrap >= 0 &&
       ((image_col_extrap & 1) == 0) && ((image_row_extrap & 1) == 0));
  const bool is_active_output = IsGaussian5x5InteriorThread() &&
                                image_col >= 0 && image_col < cols_up &&
                                image_row >= 0 && image_row < rows_up;

  __shared__ ScalarType
      block_src[kGaussian5x5BlockSizePadded * kGaussian5x5BlockSizePadded];
  __shared__ float
      block_src_temp[kGaussian5x5BlockSizePadded * kGaussian5x5BlockSizePadded];

  for (int chan = 0; chan < kNumChannels; ++chan) {
    const ScalarType src_pix =
        has_non_zero_src
            ? image_src[((image_row_extrap >> 1) * cols * kNumChannels) +
                        ((image_col_extrap >> 1) * kNumChannels) + chan]
            : static_cast<ScalarType>(0);
    const ScalarType val_out = SmoothUpsampledChannelTileWithGaussian5x5(
        src_pix, block_src, block_src_temp, is_active_output);
    if (is_active_output) {
      const int dst_idx = (image_row * cols_up * kNumChannels) +
                          (image_col * kNumChannels) + chan;
      if constexpr (kAccumulate) {
        if constexpr (std::is_same_v<ScalarType, float>) {
          image_dst[dst_idx] = __fadd_rn(image_dst[dst_idx], val_out);
        } else {
          const int32_t sum = static_cast<int32_t>(image_dst[dst_idx]) +
                              static_cast<int32_t>(val_out);
          constexpr int32_t kMin =
              static_cast<int32_t>(std::numeric_limits<ScalarType>::min());
          constexpr int32_t kMax =
              static_cast<int32_t>(std::numeric_limits<ScalarType>::max());
          image_dst[dst_idx] =
              static_cast<ScalarType>(std::clamp(sum, kMin, kMax));
        }
      } else {
        image_dst[dst_idx] = val_out;
      }
    }
    __syncthreads();
  }
}

}  // namespace

template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> SmoothImageWithGaussian5x5(
    const CudaImage<ImageTraits>& image, BorderType border_type,
    float sum_weight) {
  INTR_ASSIGN_OR_RETURN(CudaImage<ImageTraits> image_smooth,
                        CudaImage<ImageTraits>::Create(image.dimensions()));
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kGaussian5x5BlockSize),
                        CeilSizeDiv(image.rows(), kGaussian5x5BlockSize));
  const dim3 num_threads_per_block(kGaussian5x5BlockSizePadded,
                                   kGaussian5x5BlockSizePadded);
  SmoothImageWithGaussian5x5Kernel<ImageTraits>
      <<<num_blocks, num_threads_per_block>>>(image.data(), image_smooth.data(),
                                              image.cols(), image.rows(),
                                              border_type, sum_weight);
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return image_smooth;
}

template absl::StatusOr<CudaImage<Gray8u>> SmoothImageWithGaussian5x5<Gray8u>(
    const CudaImage<Gray8u>& image, BorderType border_type, float sum_weight);

template absl::StatusOr<CudaImage<Rgb8u>> SmoothImageWithGaussian5x5<Rgb8u>(
    const CudaImage<Rgb8u>& image, BorderType border_type, float sum_weight);

template absl::StatusOr<CudaImage<Gray32f>> SmoothImageWithGaussian5x5<Gray32f>(
    const CudaImage<Gray32f>& image, BorderType border_type, float sum_weight);

template absl::StatusOr<CudaImage<Generic32f>>
SmoothImageWithGaussian5x5<Generic32f>(const CudaImage<Generic32f>& image,
                                       BorderType border_type,
                                       float sum_weight);

template absl::StatusOr<CudaImage<Generic32f3>>
SmoothImageWithGaussian5x5<Generic32f3>(const CudaImage<Generic32f3>& image,
                                        BorderType border_type,
                                        float sum_weight);

template absl::StatusOr<CudaImage<Generic16i>>
SmoothImageWithGaussian5x5<Generic16i>(const CudaImage<Generic16i>& image,
                                       BorderType border_type,
                                       float sum_weight);

template absl::StatusOr<CudaImage<Generic16i3>>
SmoothImageWithGaussian5x5<Generic16i3>(const CudaImage<Generic16i3>& image,
                                        BorderType border_type,
                                        float sum_weight);

template <typename ImageTraits>
absl::Status SmoothWithGaussian5x5AndDownsample2x2(
    const CudaImage<ImageTraits>& image, CudaImage<ImageTraits>& image_down,
    BorderType border_type) {
  INTR_RETURN_IF_ERROR(
      ValidateDownsample2x2InputDimensions(image.dimensions()));
  const Dimensions expected_dimensions_down(CeilSizeDiv(image.cols(), 2),
                                            CeilSizeDiv(image.rows(), 2));
  if (image_down.dimensions() != expected_dimensions_down) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "image_down dimensions must be %d x %d (was %d x %d).",
        expected_dimensions_down.cols, expected_dimensions_down.rows,
        image_down.cols(), image_down.rows()));
  }
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kGaussian5x5BlockSize),
                        CeilSizeDiv(image.rows(), kGaussian5x5BlockSize));
  const dim3 num_threads_per_block(kGaussian5x5BlockSizePadded,
                                   kGaussian5x5BlockSizePadded);
  SmoothWithGaussian5x5AndDownsample2x2Kernel<ImageTraits>
      <<<num_blocks, num_threads_per_block>>>(image.data(), image.cols(),
                                              image.rows(), image_down.data(),
                                              image_down.cols(), border_type);
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Gray8u>(
    const CudaImage<Gray8u>& image, CudaImage<Gray8u>& image_down,
    BorderType border_type);

template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Rgb8u>(
    const CudaImage<Rgb8u>& image, CudaImage<Rgb8u>& image_down,
    BorderType border_type);

template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Gray32f>(
    const CudaImage<Gray32f>& image, CudaImage<Gray32f>& image_down,
    BorderType border_type);

template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Rgb32f>(
    const CudaImage<Rgb32f>& image, CudaImage<Rgb32f>& image_down,
    BorderType border_type);

template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Generic32f>(
    const CudaImage<Generic32f>& image, CudaImage<Generic32f>& image_down,
    BorderType border_type);

template absl::Status SmoothWithGaussian5x5AndDownsample2x2<Generic32f3>(
    const CudaImage<Generic32f3>& image, CudaImage<Generic32f3>& image_down,
    BorderType border_type);

template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> SmoothWithGaussian5x5AndDownsample2x2(
    const CudaImage<ImageTraits>& image, BorderType border_type) {
  INTR_RETURN_IF_ERROR(
      ValidateDownsample2x2InputDimensions(image.dimensions()));
  const Dimensions dimensions_down(CeilSizeDiv(image.cols(), 2),
                                   CeilSizeDiv(image.rows(), 2));
  INTR_ASSIGN_OR_RETURN(CudaImage<ImageTraits> image_down,
                        CudaImage<ImageTraits>::Create(dimensions_down));
  INTR_RETURN_IF_ERROR(
      SmoothWithGaussian5x5AndDownsample2x2(image, image_down, border_type));
  return image_down;
}

template absl::StatusOr<CudaImage<Gray8u>>
SmoothWithGaussian5x5AndDownsample2x2<Gray8u>(const CudaImage<Gray8u>& image,
                                              BorderType border_type);

template absl::StatusOr<CudaImage<Rgb8u>>
SmoothWithGaussian5x5AndDownsample2x2<Rgb8u>(const CudaImage<Rgb8u>& image,
                                             BorderType border_type);

template absl::StatusOr<CudaImage<Gray32f>>
SmoothWithGaussian5x5AndDownsample2x2<Gray32f>(const CudaImage<Gray32f>& image,
                                               BorderType border_type);

template absl::StatusOr<CudaImage<Rgb32f>>
SmoothWithGaussian5x5AndDownsample2x2<Rgb32f>(const CudaImage<Rgb32f>& image,
                                              BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f>>
SmoothWithGaussian5x5AndDownsample2x2<Generic32f>(
    const CudaImage<Generic32f>& image, BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f3>>
SmoothWithGaussian5x5AndDownsample2x2<Generic32f3>(
    const CudaImage<Generic32f3>& image, BorderType border_type);

template <typename ImageTraits>
absl::Status Upsample2x2AndSmoothWithGaussian5x5(
    const CudaImage<ImageTraits>& image,
    CudaImage<ImageTraits>& image_up_smooth, BorderType border_type,
    bool accumulate) {
  const Dimensions dimensions_up = image_up_smooth.dimensions();
  INTR_RETURN_IF_ERROR(
      ValidateUpsample2x2Dimensions(image.dimensions(), dimensions_up));
  const dim3 num_blocks(CeilSizeDiv(dimensions_up.cols, kGaussian5x5BlockSize),
                        CeilSizeDiv(dimensions_up.rows, kGaussian5x5BlockSize));
  const dim3 num_threads_per_block(kGaussian5x5BlockSizePadded,
                                   kGaussian5x5BlockSizePadded);
  if (accumulate) {
    Upsample2x2AndSmoothWithGaussian5x5Kernel<ImageTraits,
                                              /*kAccumulate=*/true>
        <<<num_blocks, num_threads_per_block>>>(
            image.data(), image.cols(), image_up_smooth.data(),
            dimensions_up.cols, dimensions_up.rows, border_type);
  } else {
    Upsample2x2AndSmoothWithGaussian5x5Kernel<ImageTraits,
                                              /*kAccumulate=*/false>
        <<<num_blocks, num_threads_per_block>>>(
            image.data(), image.cols(), image_up_smooth.data(),
            dimensions_up.cols, dimensions_up.rows, border_type);
  }
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Gray8u>(
    const CudaImage<Gray8u>& image, CudaImage<Gray8u>& image_up_smooth,
    BorderType border_type, bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Rgb8u>(
    const CudaImage<Rgb8u>& image, CudaImage<Rgb8u>& image_up_smooth,
    BorderType border_type, bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Gray32f>(
    const CudaImage<Gray32f>& image, CudaImage<Gray32f>& image_up_smooth,
    BorderType border_type, bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Rgb32f>(
    const CudaImage<Rgb32f>& image, CudaImage<Rgb32f>& image_up_smooth,
    BorderType border_type, bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic16i>(
    const CudaImage<Generic16i>& image, CudaImage<Generic16i>& image_up_smooth,
    BorderType border_type, bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic16i3>(
    const CudaImage<Generic16i3>& image,
    CudaImage<Generic16i3>& image_up_smooth, BorderType border_type,
    bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic32f>(
    const CudaImage<Generic32f>& image, CudaImage<Generic32f>& image_up_smooth,
    BorderType border_type, bool accumulate);

template absl::Status Upsample2x2AndSmoothWithGaussian5x5<Generic32f3>(
    const CudaImage<Generic32f3>& image,
    CudaImage<Generic32f3>& image_up_smooth, BorderType border_type,
    bool accumulate);

template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> Upsample2x2AndSmoothWithGaussian5x5(
    const CudaImage<ImageTraits>& image, Dimensions dimensions_up,
    BorderType border_type) {
  if (dimensions_up == Dimensions()) {
    dimensions_up = Dimensions(image.cols() * 2, image.rows() * 2);
  }
  INTR_RETURN_IF_ERROR(
      ValidateUpsample2x2Dimensions(image.dimensions(), dimensions_up));
  INTR_ASSIGN_OR_RETURN(CudaImage<ImageTraits> image_up_smooth,
                        CudaImage<ImageTraits>::Create(dimensions_up));
  INTR_RETURN_IF_ERROR(Upsample2x2AndSmoothWithGaussian5x5(
      image, image_up_smooth, border_type, /*accumulate=*/false));
  return image_up_smooth;
}

template absl::StatusOr<CudaImage<Gray8u>>
Upsample2x2AndSmoothWithGaussian5x5<Gray8u>(const CudaImage<Gray8u>& image,
                                            Dimensions dimensions_up,
                                            BorderType border_type);

template absl::StatusOr<CudaImage<Rgb8u>>
Upsample2x2AndSmoothWithGaussian5x5<Rgb8u>(const CudaImage<Rgb8u>& image,
                                           Dimensions dimensions_up,
                                           BorderType border_type);

template absl::StatusOr<CudaImage<Gray32f>>
Upsample2x2AndSmoothWithGaussian5x5<Gray32f>(const CudaImage<Gray32f>& image,
                                             Dimensions dimensions_up,
                                             BorderType border_type);

template absl::StatusOr<CudaImage<Rgb32f>>
Upsample2x2AndSmoothWithGaussian5x5<Rgb32f>(const CudaImage<Rgb32f>& image,
                                            Dimensions dimensions_up,
                                            BorderType border_type);

template absl::StatusOr<CudaImage<Generic16i>>
Upsample2x2AndSmoothWithGaussian5x5<Generic16i>(
    const CudaImage<Generic16i>& image, Dimensions dimensions_up,
    BorderType border_type);

template absl::StatusOr<CudaImage<Generic16i3>>
Upsample2x2AndSmoothWithGaussian5x5<Generic16i3>(
    const CudaImage<Generic16i3>& image, Dimensions dimensions_up,
    BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f>>
Upsample2x2AndSmoothWithGaussian5x5<Generic32f>(
    const CudaImage<Generic32f>& image, Dimensions dimensions_up,
    BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f3>>
Upsample2x2AndSmoothWithGaussian5x5<Generic32f3>(
    const CudaImage<Generic32f3>& image, Dimensions dimensions_up,
    BorderType border_type);

}  // namespace perception
}  // namespace intrinsic
