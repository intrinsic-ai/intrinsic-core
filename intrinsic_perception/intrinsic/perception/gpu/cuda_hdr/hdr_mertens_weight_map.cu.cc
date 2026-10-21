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

#include "intrinsic/perception/gpu/cuda_hdr/hdr_mertens_weight_map.h"

#include <cuda_runtime_api.h>
#include <device_types.h>
#include <vector_types.h>

#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation_device.h"
#include "intrinsic/perception/gpu/cuda_image_proc/color_conversion_device.h"
#include "intrinsic/perception/gpu/cuda_image_proc/laplacian_device.h"
#include "intrinsic/perception/gpu/cuda_image_proc/smooth_device.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_array.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_math.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {
namespace {

constexpr int kMaxInlineWeightMaps = 16;
constexpr float kWeightMapEpsilon = 1e-12f;

struct InlineWeightMapPtrs {
  float* ptrs[kMaxInlineWeightMaps];
  __device__ __forceinline__ float* operator[](int i) const { return ptrs[i]; }
};

template <typename ImageTraits>
__device__ __forceinline__ void AccumulateSaturationAndWellExposedness(
    const typename ImageTraits::ScalarType image[ImageTraits::kNumChannels],
    float saturation_exponent, float well_exposedness_exponent, float& weight) {
  constexpr int kNumChannels = ImageTraits::kNumChannels;
  constexpr float kIntensityMaxInverse = 1.0f / ImageTraits::kIntensityMax;
  // The saturation metric does not need to be computed for monochrome images.
  if constexpr (kNumChannels > 1) {
    if (saturation_exponent > 0.0f) {
      float pixels[kNumChannels];
      float mean = 0.0f;
#pragma unroll
      for (int i = 0; i < kNumChannels; ++i) {
        pixels[i] = static_cast<float>(image[i]) * kIntensityMaxInverse;
        mean += pixels[i];
      }
      mean /= static_cast<float>(kNumChannels);
      float variance = 0.0f;
#pragma unroll
      for (int i = 0; i < kNumChannels; ++i) {
        float val = pixels[i] - mean;
        variance += val * val;
      }
      variance /= static_cast<float>(kNumChannels);
      const float stdev = sqrtf(variance);
      weight *= (saturation_exponent == 1.0f)
                    ? stdev
                    : powf(stdev, saturation_exponent);
    }
  }
  if (well_exposedness_exponent > 0.0f) {
    constexpr float kWellExposednessSigma = 0.2f;
    constexpr float alpha =
        -0.5f / (kWellExposednessSigma * kWellExposednessSigma);
    float all_channel_product = 1.0f;
#pragma unroll
    for (int i = 0; i < kNumChannels; ++i) {
      const float x =
          __fmaf_rn(static_cast<float>(image[i]), kIntensityMaxInverse, -0.5f);
      all_channel_product *= expf(alpha * (x * x));
    }
    weight *= (well_exposedness_exponent == 1.0f)
                  ? all_channel_product
                  : powf(all_channel_product, well_exposedness_exponent);
  }
}

// Fuses RgbToGray (when kNumChannels == 3), ApplyLaplacian3x3, and weight map
// evaluation into a single kernel pass using shared memory for the 3x3
// Laplacian neighborhood.
template <typename ImageTraits>
__global__ void ComputeHdrMertensWeightMapWithContrastKernel(
    const typename ImageTraits::ScalarType* image, float* weight_map, int cols,
    int rows, float contrast_exponent, float saturation_exponent,
    float well_exposedness_exponent, BorderType border_type) {
  using ScalarType = typename ImageTraits::ScalarType;
  constexpr int kNumChannels = ImageTraits::kNumChannels;
  constexpr float kIntensityMaxInverse = 1.0f / ImageTraits::kIntensityMax;

  const int image_col = static_cast<int>(blockIdx.x * kLaplacian3x3BlockSize) +
                        static_cast<int>(threadIdx.x) -
                        kLaplacian3x3HalfKernelWidth;
  const int image_row = static_cast<int>(blockIdx.y * kLaplacian3x3BlockSize) +
                        static_cast<int>(threadIdx.y) -
                        kLaplacian3x3HalfKernelWidth;
  const int image_col_extrap =
      GetExtrapolatedPixelIndex(image_col, cols, border_type);
  const int image_row_extrap =
      GetExtrapolatedPixelIndex(image_row, rows, border_type);

  __shared__ ScalarType
      block_gray[kLaplacian3x3BlockSizePadded * kLaplacian3x3BlockSizePadded];

  ScalarType raw_pix[kNumChannels];
#pragma unroll
  for (int i = 0; i < kNumChannels; ++i) {
    raw_pix[i] = static_cast<ScalarType>(0);
  }

  ScalarType src_gray = static_cast<ScalarType>(0);
  if (image_col_extrap >= 0 && image_row_extrap >= 0) {
    const size_t offset =
        ((static_cast<size_t>(image_row_extrap) * cols) + image_col_extrap) *
        kNumChannels;
#pragma unroll
    for (int i = 0; i < kNumChannels; ++i) {
      raw_pix[i] = image[offset + i];
    }
    if constexpr (kNumChannels == 3) {
      src_gray =
          RgbPixelToGray<ImageTraits>(raw_pix[0], raw_pix[1], raw_pix[2]);
    } else {
      src_gray = raw_pix[0];
    }
  }

  block_gray[(threadIdx.y * kLaplacian3x3BlockSizePadded) + threadIdx.x] =
      src_gray;
  __syncthreads();

  if (image_col >= 0 && image_col < cols && image_row >= 0 &&
      image_row < rows && IsLaplacian3x3InteriorThread()) {
    const float laplacian =
        ConvolveLaplacian3x3(block_gray, threadIdx.x, threadIdx.y);

    const float contrast = fabsf(laplacian * kIntensityMaxInverse);
    float weight = (contrast_exponent == 1.0f)
                       ? contrast
                       : powf(contrast, contrast_exponent);
    AccumulateSaturationAndWellExposedness<ImageTraits>(
        raw_pix, saturation_exponent, well_exposedness_exponent, weight);

    // Add small constant to avoid potential division by zero during
    // normalization.
    weight_map[(static_cast<size_t>(image_row) * cols) + image_col] =
        weight + kWeightMapEpsilon;
  }
}

template <typename ImageTraits>
__global__ void ComputeHdrMertensWeightMapNoContrastKernel(
    const typename ImageTraits::ScalarType* image, float* weight_map, int cols,
    int rows, float saturation_exponent, float well_exposedness_exponent) {
  const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
  const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
  if (x >= cols || y >= rows) {
    return;
  }
  using ScalarType = typename ImageTraits::ScalarType;
  constexpr int kNumChannels = ImageTraits::kNumChannels;
  image += y * cols * kNumChannels + x * kNumChannels;
  ScalarType raw_pix[kNumChannels];
#pragma unroll
  for (int i = 0; i < kNumChannels; ++i) {
    raw_pix[i] = image[i];
  }
  float weight = 1.0f;
  AccumulateSaturationAndWellExposedness<ImageTraits>(
      raw_pix, saturation_exponent, well_exposedness_exponent, weight);
  weight_map[y * cols + x] = weight + kWeightMapEpsilon;
}

template <typename PtrArray>
__global__ void NormalizeWeightMapsKernel(PtrArray weight_maps_dataptrs,
                                          int num_weight_maps, int cols,
                                          int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  const size_t pixel_index = y * cols + x;
  float sum = 0.0f;
  for (int i = 0; i < num_weight_maps; ++i) {
    sum += weight_maps_dataptrs[i][pixel_index];
  }
  for (int i = 0; i < num_weight_maps; ++i) {
    // Sum will never be zero because we add a small constant to the final
    // weight in ComputeHdrMertensWeightMapKernel.
    weight_maps_dataptrs[i][pixel_index] /= sum;
  }
}

template <typename ImageTraits, bool kAccumulate>
__global__ void BlendWeightMapAndImageKernel(
    const float* weight_map, const typename ImageTraits::ScalarType* image,
    float* accumulator, int cols, int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  constexpr int kNumChannels = ImageTraits::kNumChannels;
  const size_t pixel_index = y * cols + x;
  const size_t pixel_index_with_channels = pixel_index * kNumChannels;
  const float weight = weight_map[pixel_index];
  image += pixel_index_with_channels;
  accumulator += pixel_index_with_channels;
#pragma unroll
  for (int i = 0; i < kNumChannels; ++i) {
    if constexpr (kAccumulate) {
      accumulator[i] += weight * static_cast<float>(image[i]);
    } else {
      accumulator[i] = weight * static_cast<float>(image[i]);
    }
  }
}

template <typename ImageTraits, bool kAccumulate>
__global__ void PyrUpAndBlendLaplacianKernel(
    const float* weight_map, const typename ImageTraits::ScalarType* image_curr,
    const typename ImageTraits::ScalarType* image_down, float* accumulator,
    int src_cols, int dst_cols, int dst_rows, BorderType border_type) {
  using ScalarType = typename ImageTraits::ScalarType;
  constexpr int kNumChannels = ImageTraits::kNumChannels;

  const int image_col = static_cast<int>(blockIdx.x * kGaussian5x5BlockSize) +
                        static_cast<int>(threadIdx.x) -
                        kGaussian5x5HalfKernelWidth;
  const int image_row = static_cast<int>(blockIdx.y * kGaussian5x5BlockSize) +
                        static_cast<int>(threadIdx.y) -
                        kGaussian5x5HalfKernelWidth;
  const int image_col_extrap =
      GetExtrapolatedPixelIndex(image_col, dst_cols, border_type);
  const int image_row_extrap =
      GetExtrapolatedPixelIndex(image_row, dst_rows, border_type);

  __shared__ ScalarType
      block_src[kGaussian5x5BlockSizePadded * kGaussian5x5BlockSizePadded];
  __shared__ float
      block_src_temp[kGaussian5x5BlockSizePadded * kGaussian5x5BlockSizePadded];

  const bool is_active_output = IsGaussian5x5InteriorThread() &&
                                image_col >= 0 && image_col < dst_cols &&
                                image_row >= 0 && image_row < dst_rows;

  float weight = 0.0f;
  if (is_active_output) {
    const size_t pixel_index =
        (static_cast<size_t>(image_row) * dst_cols) + image_col;
    weight = weight_map[pixel_index];
    image_curr += pixel_index * kNumChannels;
    accumulator += pixel_index * kNumChannels;
  }

  const bool valid_even_extrap =
      image_col_extrap >= 0 && image_row_extrap >= 0 &&
      (image_col_extrap & 1) == 0 && (image_row_extrap & 1) == 0;
  const size_t down_offset =
      valid_even_extrap
          ? ((static_cast<size_t>(image_row_extrap >> 1) * src_cols) +
             (image_col_extrap >> 1)) *
                kNumChannels
          : 0;

  for (int i = 0; i < kNumChannels; ++i) {
    const ScalarType src_pix = valid_even_extrap ? image_down[down_offset + i]
                                                 : static_cast<ScalarType>(0);
    const ScalarType upsampled_pix = SmoothUpsampledChannelTileWithGaussian5x5(
        src_pix, block_src, block_src_temp, is_active_output);
    if (is_active_output) {
      float laplacian = 0.0f;
      if constexpr (std::is_integral_v<ScalarType>) {
        laplacian = static_cast<float>(static_cast<int>(image_curr[i]) -
                                       static_cast<int>(upsampled_pix));
      } else {
        laplacian = __fsub_rn(static_cast<float>(image_curr[i]),
                              static_cast<float>(upsampled_pix));
      }
      if constexpr (kAccumulate) {
        accumulator[i] += weight * laplacian;
      } else {
        accumulator[i] = weight * laplacian;
      }
    }
    __syncthreads();
  }
}

}  // namespace

template <typename ImageTraits>
absl::StatusOr<CudaImage<Generic32f>> ComputeHdrMertensWeightMap(
    const CudaImage<ImageTraits>& image, float contrast_exponent,
    float saturation_exponent, float well_exposedness_exponent,
    BorderType border_type) {
  static_assert(
      ImageTraits::kNumChannels == 1 || ImageTraits::kNumChannels == 3,
      "image must have either 1 or 3 channels.");
  INTR_ASSIGN_OR_RETURN(CudaImage<Generic32f> weight_map,
                        CudaImage<Generic32f>::Create(image.dimensions()));
  if (contrast_exponent > 0.0f) {
    const dim3 num_blocks(CeilSizeDiv(image.cols(), kLaplacian3x3BlockSize),
                          CeilSizeDiv(image.rows(), kLaplacian3x3BlockSize));
    const dim3 num_threads_per_block(kLaplacian3x3BlockSizePadded,
                                     kLaplacian3x3BlockSizePadded);
    ComputeHdrMertensWeightMapWithContrastKernel<ImageTraits>
        <<<num_blocks, num_threads_per_block>>>(
            image.data(), weight_map.data(), image.cols(), image.rows(),
            contrast_exponent, saturation_exponent, well_exposedness_exponent,
            border_type);
  } else {
    constexpr int kBlockSize = 16;
    const dim3 num_blocks(CeilSizeDiv(image.cols(), kBlockSize),
                          CeilSizeDiv(image.rows(), kBlockSize));
    const dim3 num_threads_per_block(kBlockSize, kBlockSize);
    ComputeHdrMertensWeightMapNoContrastKernel<ImageTraits>
        <<<num_blocks, num_threads_per_block>>>(
            image.data(), weight_map.data(), image.cols(), image.rows(),
            saturation_exponent, well_exposedness_exponent);
  }
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return weight_map;
}

template absl::StatusOr<CudaImage<Generic32f>> ComputeHdrMertensWeightMap(
    const CudaImage<Gray8u>& image, float contrast_exponent,
    float saturation_exponent, float well_exposedness_exponent,
    BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f>> ComputeHdrMertensWeightMap(
    const CudaImage<Rgb8u>& image, float contrast_exponent,
    float saturation_exponent, float well_exposedness_exponent,
    BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f>> ComputeHdrMertensWeightMap(
    const CudaImage<Gray32f>& image, float contrast_exponent,
    float saturation_exponent, float well_exposedness_exponent,
    BorderType border_type);

template absl::StatusOr<CudaImage<Generic32f>> ComputeHdrMertensWeightMap(
    const CudaImage<Rgb32f>& image, float contrast_exponent,
    float saturation_exponent, float well_exposedness_exponent,
    BorderType border_type);

absl::Status NormalizeWeightMaps(
    std::vector<CudaImage<Generic32f>>& weight_maps) {
  if (weight_maps.empty()) {
    return absl::InvalidArgumentError("weight_maps must not be empty.");
  }
  Dimensions dimensions = weight_maps[0].dimensions();
  for (int i = 1; i < weight_maps.size(); ++i) {
    if (weight_maps[i].dimensions() != dimensions) {
      return absl::InvalidArgumentError(
          "Ensure that all weight maps have the same dimensions.");
    }
  }
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(dimensions.cols, kBlockSize),
                        CeilSizeDiv(dimensions.rows, kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);

  if (weight_maps.size() <= kMaxInlineWeightMaps) {
    InlineWeightMapPtrs inline_ptrs{};
    for (size_t i = 0; i < weight_maps.size(); ++i) {
      inline_ptrs.ptrs[i] = weight_maps[i].data();
    }
    NormalizeWeightMapsKernel<<<num_blocks, num_threads_per_block>>>(
        inline_ptrs, weight_maps.size(), dimensions.cols, dimensions.rows);
    CUDA_CHECK(cudaStreamSynchronize(nullptr));
    return absl::OkStatus();
  }

  std::vector<const float*> weight_maps_dataptrs;
  for (const auto& weight_map : weight_maps) {
    weight_maps_dataptrs.push_back(weight_map.data());
  }
  INTR_ASSIGN_OR_RETURN(
      CudaArray<const float*> cuda_weight_maps_dataptrs,
      CudaArray<const float*>::CreateFromHostMemory(weight_maps_dataptrs));
  NormalizeWeightMapsKernel<<<num_blocks, num_threads_per_block>>>(
      const_cast<float**>(cuda_weight_maps_dataptrs.data()),
      cuda_weight_maps_dataptrs.size(), dimensions.cols, dimensions.rows);
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template <typename ImageTraits, typename AccumulatorTrait>
absl::Status BlendWeightMapAndImage(const CudaImage<Generic32f>& weight_map,
                                    const CudaImage<ImageTraits>& image,
                                    bool accumulate,
                                    CudaImage<AccumulatorTrait>& accumulator) {
  static_assert(std::is_same_v<typename AccumulatorTrait::ScalarType, float> &&
                    AccumulatorTrait::kNumChannels == ImageTraits::kNumChannels,
                "accumulator must have float scalar type and the same number "
                "of channels as image.");
  if (weight_map.dimensions() != image.dimensions()) {
    return absl::InvalidArgumentError(
        "weight_map and image must have the same dimensions.");
  }
  if (weight_map.dimensions() != accumulator.dimensions()) {
    return absl::InvalidArgumentError(
        "weight_map and accumulator must have the same dimensions.");
  }
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kBlockSize),
                        CeilSizeDiv(image.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  if (accumulate) {
    BlendWeightMapAndImageKernel<ImageTraits, /*kAccumulate=*/true>
        <<<num_blocks, num_threads_per_block>>>(weight_map.data(), image.data(),
                                                accumulator.data(),
                                                image.cols(), image.rows());
  } else {
    BlendWeightMapAndImageKernel<ImageTraits, /*kAccumulate=*/false>
        <<<num_blocks, num_threads_per_block>>>(weight_map.data(), image.data(),
                                                accumulator.data(),
                                                image.cols(), image.rows());
  }
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Generic16i>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Generic16i3>& image, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Generic32f>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Generic32f3>& image, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Gray8u>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb8u>& image,
    bool accumulate, CudaImage<Generic32f3>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Gray32f>& image,
    bool accumulate, CudaImage<Generic32f>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Gray32f>& image,
    bool accumulate, CudaImage<Gray32f>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb32f>& image,
    bool accumulate, CudaImage<Generic32f3>& accumulator);

template absl::Status BlendWeightMapAndImage(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb32f>& image,
    bool accumulate, CudaImage<Rgb32f>& accumulator);

template <typename ImageTraits, typename AccumulatorTrait>
absl::Status PyrUpAndBlendLaplacian(const CudaImage<Generic32f>& weight_map,
                                    const CudaImage<ImageTraits>& image_curr,
                                    const CudaImage<ImageTraits>& image_down,
                                    BorderType border_type, bool accumulate,
                                    CudaImage<AccumulatorTrait>& accumulator) {
  static_assert(std::is_same_v<typename AccumulatorTrait::ScalarType, float> &&
                    AccumulatorTrait::kNumChannels == ImageTraits::kNumChannels,
                "accumulator must have float scalar type and the same number "
                "of channels as image_curr.");
  if (weight_map.dimensions() != image_curr.dimensions() ||
      weight_map.dimensions() != accumulator.dimensions()) {
    return absl::InvalidArgumentError(
        "weight_map, image_curr, and accumulator must have the same "
        "dimensions.");
  }
  const int dst_cols = image_curr.cols();
  const int dst_rows = image_curr.rows();
  if (CeilSizeDiv(dst_cols, 2) != image_down.cols() ||
      CeilSizeDiv(dst_rows, 2) != image_down.rows()) {
    return absl::InvalidArgumentError(
        "image_down dimensions are incompatible with image_curr dimensions.");
  }
  const dim3 num_blocks(CeilSizeDiv(dst_cols, kGaussian5x5BlockSize),
                        CeilSizeDiv(dst_rows, kGaussian5x5BlockSize));
  const dim3 num_threads_per_block(kGaussian5x5BlockSizePadded,
                                   kGaussian5x5BlockSizePadded);
  if (accumulate) {
    PyrUpAndBlendLaplacianKernel<ImageTraits, /*kAccumulate=*/true>
        <<<num_blocks, num_threads_per_block>>>(
            weight_map.data(), image_curr.data(), image_down.data(),
            accumulator.data(), image_down.cols(), dst_cols, dst_rows,
            border_type);
  } else {
    PyrUpAndBlendLaplacianKernel<ImageTraits, /*kAccumulate=*/false>
        <<<num_blocks, num_threads_per_block>>>(
            weight_map.data(), image_curr.data(), image_down.data(),
            accumulator.data(), image_down.cols(), dst_cols, dst_rows,
            border_type);
  }
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Gray8u>& image_curr, const CudaImage<Gray8u>& image_down,
    BorderType border_type, bool accumulate,
    CudaImage<Generic32f>& accumulator);

template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map, const CudaImage<Rgb8u>& image_curr,
    const CudaImage<Rgb8u>& image_down, BorderType border_type, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Gray32f>& image_curr, const CudaImage<Gray32f>& image_down,
    BorderType border_type, bool accumulate,
    CudaImage<Generic32f>& accumulator);

template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Gray32f>& image_curr, const CudaImage<Gray32f>& image_down,
    BorderType border_type, bool accumulate, CudaImage<Gray32f>& accumulator);

template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Rgb32f>& image_curr, const CudaImage<Rgb32f>& image_down,
    BorderType border_type, bool accumulate,
    CudaImage<Generic32f3>& accumulator);

template absl::Status PyrUpAndBlendLaplacian(
    const CudaImage<Generic32f>& weight_map,
    const CudaImage<Rgb32f>& image_curr, const CudaImage<Rgb32f>& image_down,
    BorderType border_type, bool accumulate, CudaImage<Rgb32f>& accumulator);

}  // namespace perception
}  // namespace intrinsic
