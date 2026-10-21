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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_SMOOTH_DEVICE_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_SMOOTH_DEVICE_H_

#include <cuda_runtime.h>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace intrinsic::perception {

constexpr int kGaussian5x5BlockSize = 12;
constexpr int kGaussian5x5KernelWidth = 5;
constexpr int kGaussian5x5HalfKernelWidth = kGaussian5x5KernelWidth / 2;
constexpr int kGaussian5x5BlockSizePadded =
    kGaussian5x5BlockSize + 2 * kGaussian5x5HalfKernelWidth;
constexpr float kGaussian5x5UpsampleScale = 4.0f;

inline constexpr float kGaussian5x5Kernel1d[kGaussian5x5KernelWidth] = {
    0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};

// Returns true if the current thread lies within the unpadded interior of a
// `(kGaussian5x5BlockSizePadded x kGaussian5x5BlockSizePadded)` thread block.
__device__ __forceinline__ bool IsGaussian5x5InteriorThread() {
  return threadIdx.x >= kGaussian5x5HalfKernelWidth &&
         threadIdx.x < kGaussian5x5HalfKernelWidth + kGaussian5x5BlockSize &&
         threadIdx.y >= kGaussian5x5HalfKernelWidth &&
         threadIdx.y < kGaussian5x5HalfKernelWidth + kGaussian5x5BlockSize;
}

// Evaluates the 1D 5x5 Gaussian horizontal convolution at
// `(block_col, block_row)` within a shared-memory tile `block_src` of row
// stride `kGaussian5x5BlockSizePadded`.
template <typename ScalarType>
__device__ __forceinline__ float ConvolveGaussian5x5Row(
    const ScalarType* block_src, int block_col, int block_row) {
  float sum = 0.0f;
#pragma unroll
  for (int j = 0; j < kGaussian5x5KernelWidth; ++j) {
    const int block_col_with_offset =
        block_col - kGaussian5x5HalfKernelWidth + j;
    const float val =
        static_cast<float>(block_src[block_row * kGaussian5x5BlockSizePadded +
                                     block_col_with_offset]);
    sum += kGaussian5x5Kernel1d[j] * val;
  }
  return sum;
}

// Evaluates the 1D 5x5 Gaussian vertical convolution at
// `(block_col, block_row)` within a shared-memory tile `block_src` of row
// stride `kGaussian5x5BlockSizePadded`.
__device__ __forceinline__ float ConvolveGaussian5x5Col(const float* block_src,
                                                        int block_col,
                                                        int block_row) {
  float sum = 0.0f;
#pragma unroll
  for (int j = 0; j < kGaussian5x5KernelWidth; ++j) {
    const int block_row_with_offset =
        block_row - kGaussian5x5HalfKernelWidth + j;
    const float val =
        block_src[block_row_with_offset * kGaussian5x5BlockSizePadded +
                  block_col];
    sum += kGaussian5x5Kernel1d[j] * val;
  }
  return sum;
}

// Computes the separable 5x5 Gaussian smoothing (scaled by 4.0 for 2x2
// zero-interleaved upsampling) for a single channel across the current thread
// block using the provided shared memory buffers `block_src` and
// `block_src_temp` (each of size `kGaussian5x5BlockSizePadded *
// kGaussian5x5BlockSizePadded`). Returns the smoothed and scaled pixel value
// for threads where `compute_col` is true.
// Note: Callers iterating over multiple channels must call `__syncthreads()`
// after consuming the returned value before the next call to this function.
template <typename ScalarType>
__device__ __forceinline__ ScalarType SmoothUpsampledChannelTileWithGaussian5x5(
    ScalarType src_pix, ScalarType* block_src, float* block_src_temp,
    bool compute_col = IsGaussian5x5InteriorThread()) {
  block_src[threadIdx.y * kGaussian5x5BlockSizePadded + threadIdx.x] = src_pix;
  __syncthreads();

  if (threadIdx.x >= kGaussian5x5HalfKernelWidth &&
      threadIdx.x < kGaussian5x5HalfKernelWidth + kGaussian5x5BlockSize) {
    block_src_temp[threadIdx.y * kGaussian5x5BlockSizePadded + threadIdx.x] =
        ConvolveGaussian5x5Row(block_src, threadIdx.x, threadIdx.y);
  }
  __syncthreads();

  if (compute_col) {
    const float sum =
        ConvolveGaussian5x5Col(block_src_temp, threadIdx.x, threadIdx.y);
    float scaled = __fmul_rn(kGaussian5x5UpsampleScale, sum);
    if constexpr (std::is_integral_v<ScalarType>) {
      // Note: Rounding to nearest integer (e.g. via rintf(scaled) or + 0.5f for
      // unsigned types) would avoid a 0.5 LSB truncation bias across pyramid
      // levels, but we intentionally truncate here to remain bit-exact with
      // SmoothImageWithGaussian5x5Kernel,
      // SmoothWithGaussian5x5AndDownsample2x2Kernel, and existing golden tests.
      constexpr float kMin =
          static_cast<float>(std::numeric_limits<ScalarType>::min());
      constexpr float kMax =
          static_cast<float>(std::numeric_limits<ScalarType>::max());
      scaled = std::clamp(scaled, kMin, kMax);
    }
    return static_cast<ScalarType>(scaled);
  }
  return static_cast<ScalarType>(0);
}

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_SMOOTH_DEVICE_H_
