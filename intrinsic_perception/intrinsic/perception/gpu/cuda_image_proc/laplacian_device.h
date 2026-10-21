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

#ifndef INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_LAPLACIAN_DEVICE_H_
#define INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_LAPLACIAN_DEVICE_H_

#include <cuda_runtime.h>

namespace intrinsic::perception {

constexpr int kLaplacian3x3BlockSize = 14;
constexpr int kLaplacian3x3HalfKernelWidth = 1;
constexpr int kLaplacian3x3BlockSizePadded =
    kLaplacian3x3BlockSize + 2 * kLaplacian3x3HalfKernelWidth;

// Returns true if the current thread lies within the unpadded interior of a
// `(kLaplacian3x3BlockSizePadded x kLaplacian3x3BlockSizePadded)` thread block.
__device__ __forceinline__ bool IsLaplacian3x3InteriorThread() {
  return threadIdx.x >= kLaplacian3x3HalfKernelWidth &&
         threadIdx.x < kLaplacian3x3BlockSize + kLaplacian3x3HalfKernelWidth &&
         threadIdx.y >= kLaplacian3x3HalfKernelWidth &&
         threadIdx.y < kLaplacian3x3BlockSize + kLaplacian3x3HalfKernelWidth;
}

// Evaluates the 3x3 4-neighbor Laplacian (`[0, 1, 0; 1, -4, 1; 0, 1, 0]`) at
// `(block_col, block_row)` within a single-channel shared-memory tile
// `block_src` of row stride `kLaplacian3x3BlockSizePadded`.
template <typename ScalarType>
__device__ __forceinline__ float ConvolveLaplacian3x3(
    const ScalarType* block_src, int block_col, int block_row) {
  const int up = (block_row - 1) * kLaplacian3x3BlockSizePadded + block_col;
  const int left = block_row * kLaplacian3x3BlockSizePadded + (block_col - 1);
  const int center = block_row * kLaplacian3x3BlockSizePadded + block_col;
  const int right = block_row * kLaplacian3x3BlockSizePadded + (block_col + 1);
  const int down = (block_row + 1) * kLaplacian3x3BlockSizePadded + block_col;
  return static_cast<float>(block_src[up]) +
         static_cast<float>(block_src[left]) +
         static_cast<float>(block_src[right]) +
         static_cast<float>(block_src[down]) +
         -4.0f * static_cast<float>(block_src[center]);
}

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_GPU_CUDA_IMAGE_PROC_LAPLACIAN_DEVICE_H_
