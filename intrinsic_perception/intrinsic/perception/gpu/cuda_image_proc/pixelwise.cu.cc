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

#include "intrinsic/perception/gpu/cuda_image_proc/pixelwise.h"

#include <cuda_runtime_api.h>
#include <device_types.h>
#include <vector_types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_math.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {
namespace {

template <typename OutputScalarType, typename InputScalarType,
          size_t NumChannels>
__global__ void CastKernel(OutputScalarType* image_out,
                           const InputScalarType* image, int cols, int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  const size_t pixel_index = y * cols * NumChannels + x * NumChannels;
  image_out += pixel_index;
  image += pixel_index;
#pragma unroll
  for (int i = 0; i < NumChannels; ++i) {
    image_out[i] = static_cast<OutputScalarType>(image[i]);
  }
}

template <typename OutputScalarType, typename InputScalarType,
          size_t NumChannels>
__global__ void RoundThenCastKernel(OutputScalarType* image_out,
                                    const InputScalarType* image, int cols,
                                    int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  const size_t pixel_index = y * cols * NumChannels + x * NumChannels;
  image_out += pixel_index;
  image += pixel_index;
#pragma unroll
  for (int i = 0; i < NumChannels; ++i) {
    image_out[i] = static_cast<OutputScalarType>(std::round(image[i]));
  }
}

template <typename ScalarType, size_t NumChannels>
__global__ void ClampKernel(ScalarType* image, int cols, int rows,
                            ScalarType min, ScalarType max) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  image += y * cols * NumChannels + x * NumChannels;
#pragma unroll
  for (int i = 0; i < NumChannels; ++i) {
    image[i] = std::clamp(image[i], min, max);
  }
}

template <typename OutputScalarType, typename InputScalarTypeA,
          typename InputScalarTypeB, size_t NumChannels>
__global__ void AddKernel(OutputScalarType* image_out,
                          const InputScalarTypeA* image_a,
                          const InputScalarTypeB* image_b, int cols, int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  const size_t pixel_index = y * cols * NumChannels + x * NumChannels;
#pragma unroll
  for (int i = 0; i < NumChannels; ++i) {
    image_out[pixel_index + i] =
        static_cast<OutputScalarType>(image_a[pixel_index + i]) +
        static_cast<OutputScalarType>(image_b[pixel_index + i]);
  }
}

template <typename OutputScalarType, typename InputScalarTypeA,
          typename InputScalarTypeB, size_t NumChannels>
__global__ void SubtractKernel(OutputScalarType* image_out,
                               const InputScalarTypeA* image_a,
                               const InputScalarTypeB* image_b, int cols,
                               int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  const size_t pixel_index = y * cols * NumChannels + x * NumChannels;
#pragma unroll
  for (int i = 0; i < NumChannels; ++i) {
    image_out[pixel_index + i] =
        static_cast<OutputScalarType>(image_a[pixel_index + i]) -
        static_cast<OutputScalarType>(image_b[pixel_index + i]);
  }
}

template <typename OutputImageTraits, typename InputImageTraits>
__global__ void ConvertImageKernel(
    typename OutputImageTraits::ScalarType* image_out,
    const typename InputImageTraits::ScalarType* image, int cols, int rows) {
  const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
  const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= cols || y >= rows) {
    return;
  }
  constexpr int kNumChannels = OutputImageTraits::kNumChannels;
  const size_t pixel_index = y * cols * kNumChannels + x * kNumChannels;
  constexpr float kInputIntensityMax =
      static_cast<float>(InputImageTraits::kIntensityMax);
  constexpr float kOutputIntensityMax =
      static_cast<float>(OutputImageTraits::kIntensityMax);
  constexpr float scale = kOutputIntensityMax / kInputIntensityMax;
#pragma unroll
  for (int i = 0; i < kNumChannels; ++i) {
    float scaled_value = scale * static_cast<float>(image[pixel_index + i]);
    if constexpr (std::is_integral_v<typename OutputImageTraits::ScalarType>) {
      scaled_value = std::round(scaled_value);
    }
    scaled_value = std::clamp(scaled_value, 0.0f, kOutputIntensityMax);
    image_out[pixel_index + i] =
        static_cast<typename OutputImageTraits::ScalarType>(scaled_value);
  }
}

}  // namespace

template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>> Cast(
    const CudaImage<InputImageTraits>& image) {
  static_assert(
      OutputImageTraits::kNumChannels == InputImageTraits::kNumChannels,
      "Input and output image types must have the same number of channels.");
  if (image.size() == 0) {
    return CudaImage<OutputImageTraits>();
  }
  INTR_ASSIGN_OR_RETURN(
      CudaImage<OutputImageTraits> image_out,
      CudaImage<OutputImageTraits>::Create(image.dimensions()));
  if constexpr (std::is_same_v<typename OutputImageTraits::ScalarType,
                               typename InputImageTraits::ScalarType>) {
    image_out.CopyDataFromDevice(image.data());
    return image_out;
  }
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kBlockSize),
                        CeilSizeDiv(image.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  CastKernel<typename OutputImageTraits::ScalarType,
             typename InputImageTraits::ScalarType,
             InputImageTraits::kNumChannels>
      <<<num_blocks, num_threads_per_block>>>(image_out.data(), image.data(),
                                              image.cols(), image.rows());
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return image_out;
}

template absl::StatusOr<CudaImage<Generic16i>> Cast(
    const CudaImage<Gray8u>& image);

template absl::StatusOr<CudaImage<Generic16i3>> Cast(
    const CudaImage<Rgb8u>& image);

template absl::StatusOr<CudaImage<Generic32f>> Cast(
    const CudaImage<Gray8u>& image);

template absl::StatusOr<CudaImage<Generic32f3>> Cast(
    const CudaImage<Rgb8u>& image);

template absl::StatusOr<CudaImage<Generic32f>> Cast(
    const CudaImage<Gray32f>& image);

template absl::StatusOr<CudaImage<Generic32f3>> Cast(
    const CudaImage<Rgb32f>& image);

template absl::StatusOr<CudaImage<Gray32f>> Cast(
    const CudaImage<Generic32f>& image);

template absl::StatusOr<CudaImage<Rgb32f>> Cast(
    const CudaImage<Generic32f3>& image);

template absl::StatusOr<CudaImage<Gray8u>> Cast(
    const CudaImage<Generic16i>& image);

template absl::StatusOr<CudaImage<Rgb8u>> Cast(
    const CudaImage<Generic16i3>& image);

template absl::StatusOr<CudaImage<Gray8u>> Cast(
    const CudaImage<Generic32f>& image);

template absl::StatusOr<CudaImage<Rgb8u>> Cast(
    const CudaImage<Generic32f3>& image);

template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>> RoundThenCast(
    const CudaImage<InputImageTraits>& image) {
  static_assert(
      OutputImageTraits::kNumChannels == InputImageTraits::kNumChannels,
      "Input and output image types must have the same number of channels.");
  if (image.size() == 0) {
    return CudaImage<OutputImageTraits>();
  }
  INTR_ASSIGN_OR_RETURN(
      CudaImage<OutputImageTraits> image_out,
      CudaImage<OutputImageTraits>::Create(image.dimensions()));
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kBlockSize),
                        CeilSizeDiv(image.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  RoundThenCastKernel<typename OutputImageTraits::ScalarType,
                      typename InputImageTraits::ScalarType,
                      InputImageTraits::kNumChannels>
      <<<num_blocks, num_threads_per_block>>>(image_out.data(), image.data(),
                                              image.cols(), image.rows());
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return image_out;
}

template absl::StatusOr<CudaImage<Gray8u>> RoundThenCast(
    const CudaImage<Generic32f>& image);

template absl::StatusOr<CudaImage<Rgb8u>> RoundThenCast(
    const CudaImage<Generic32f3>& image);

template <typename ImageTraits>
absl::Status Clamp(CudaImage<ImageTraits>& image,
                   typename ImageTraits::ScalarType min,
                   typename ImageTraits::ScalarType max) {
  if (image.size() == 0) {
    return absl::OkStatus();
  }
  if (max < min) {
    return absl::InvalidArgumentError("max must be >= min.");
  }
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kBlockSize),
                        CeilSizeDiv(image.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  ClampKernel<typename ImageTraits::ScalarType, ImageTraits::kNumChannels>
      <<<num_blocks, num_threads_per_block>>>(image.data(), image.cols(),
                                              image.rows(), min, max);
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template absl::Status Clamp(CudaImage<Generic16i>& image, int16_t min,
                            int16_t max);

template absl::Status Clamp(CudaImage<Generic16i3>& image, int16_t min,
                            int16_t max);

template absl::Status Clamp(CudaImage<Generic32f>& image, float min, float max);

template absl::Status Clamp(CudaImage<Generic32f3>& image, float min,
                            float max);

template absl::Status Clamp(CudaImage<Gray32f>& image, float min, float max);

template absl::Status Clamp(CudaImage<Rgb32f>& image, float min, float max);

template <typename OutputImageTraits, typename ImageTraitsA,
          typename ImageTraitsB>
absl::StatusOr<CudaImage<OutputImageTraits>> Add(
    const CudaImage<ImageTraitsA>& image_a,
    const CudaImage<ImageTraitsB>& image_b) {
  static_assert(ImageTraitsA::kNumChannels == ImageTraitsB::kNumChannels,
                "image_a and image_b must have the same number of channels.");
  static_assert(
      OutputImageTraits::kNumChannels == ImageTraitsA::kNumChannels,
      "input and output images must have the same number of channels.");
  if (image_a.dimensions() != image_b.dimensions()) {
    return absl::InvalidArgumentError(
        "image_a and image_b must have the same dimensions.");
  }
  INTR_ASSIGN_OR_RETURN(
      auto image_out,
      (CudaImage<OutputImageTraits>::Create(image_a.dimensions())));
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image_out.cols(), kBlockSize),
                        CeilSizeDiv(image_out.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  AddKernel<typename OutputImageTraits::ScalarType,
            typename ImageTraitsA::ScalarType,
            typename ImageTraitsB::ScalarType, ImageTraitsA::kNumChannels>
      <<<num_blocks, num_threads_per_block>>>(image_out.data(), image_a.data(),
                                              image_b.data(), image_out.cols(),
                                              image_out.rows());
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return image_out;
}

template absl::StatusOr<CudaImage<Generic16i3>> Add(
    const CudaImage<Rgb8u>& image_a, const CudaImage<Rgb8u>& image_b);

template absl::StatusOr<CudaImage<Generic16i>> Add(
    const CudaImage<Generic16i>& image_a, const CudaImage<Generic16i>& image_b);

template absl::StatusOr<CudaImage<Generic16i3>> Add(
    const CudaImage<Generic16i3>& image_a,
    const CudaImage<Generic16i3>& image_b);

template absl::StatusOr<CudaImage<Generic32f>> Add(
    const CudaImage<Generic32f>& image_a, const CudaImage<Generic32f>& image_b);

template absl::StatusOr<CudaImage<Generic32f3>> Add(
    const CudaImage<Generic32f3>& image_a,
    const CudaImage<Generic32f3>& image_b);

template <typename OutputImageTraits, typename ImageTraitsA,
          typename ImageTraitsB>
absl::StatusOr<CudaImage<OutputImageTraits>> Subtract(
    const CudaImage<ImageTraitsA>& image_a,
    const CudaImage<ImageTraitsB>& image_b) {
  static_assert(ImageTraitsA::kNumChannels == ImageTraitsB::kNumChannels,
                "image_a and image_b must have the same number of channels.");
  static_assert(
      OutputImageTraits::kNumChannels == ImageTraitsA::kNumChannels,
      "input and output images must have the same number of channels.");
  if (image_a.dimensions() != image_b.dimensions()) {
    return absl::InvalidArgumentError(
        "image_a and image_b must have the same dimensions.");
  }
  INTR_ASSIGN_OR_RETURN(auto image_out, CudaImage<OutputImageTraits>::Create(
                                            image_a.dimensions()));
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image_out.cols(), kBlockSize),
                        CeilSizeDiv(image_out.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  SubtractKernel<typename OutputImageTraits::ScalarType,
                 typename ImageTraitsA::ScalarType,
                 typename ImageTraitsB::ScalarType, ImageTraitsA::kNumChannels>
      <<<num_blocks, num_threads_per_block>>>(image_out.data(), image_a.data(),
                                              image_b.data(), image_out.cols(),
                                              image_out.rows());
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return image_out;
}

template absl::StatusOr<CudaImage<Generic16i>> Subtract(
    const CudaImage<Gray8u>& image_a, const CudaImage<Gray8u>& image_b);

template absl::StatusOr<CudaImage<Generic16i3>> Subtract(
    const CudaImage<Rgb8u>& image_a, const CudaImage<Rgb8u>& image_b);

template absl::StatusOr<CudaImage<Generic32f>> Subtract(
    const CudaImage<Gray8u>& image_a, const CudaImage<Gray8u>& image_b);

template absl::StatusOr<CudaImage<Generic32f3>> Subtract(
    const CudaImage<Rgb8u>& image_a, const CudaImage<Rgb8u>& image_b);

template absl::StatusOr<CudaImage<Generic32f>> Subtract(
    const CudaImage<Generic32f>& image_a, const CudaImage<Generic32f>& image_b);

template absl::StatusOr<CudaImage<Generic32f3>> Subtract(
    const CudaImage<Generic32f3>& image_a,
    const CudaImage<Generic32f3>& image_b);

template <typename OutputImageTraits, typename InputImageTraits>
absl::Status ConvertImage(const CudaImage<InputImageTraits>& image,
                          CudaImage<OutputImageTraits>& image_out) {
  static_assert(
      OutputImageTraits::kNumChannels == InputImageTraits::kNumChannels,
      "Input and output image types must have the same number of channels.");
  if (image.dimensions() != image_out.dimensions()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "image and image_out must have the same dimensions. Input: ",
        image.cols(), "x", image.rows(), ", Output: ", image_out.cols(), "x",
        image_out.rows()));
  }
  if (image.size() == 0) {
    return absl::OkStatus();
  }
  constexpr int kBlockSize = 16;
  const dim3 num_blocks(CeilSizeDiv(image.cols(), kBlockSize),
                        CeilSizeDiv(image.rows(), kBlockSize));
  const dim3 num_threads_per_block(kBlockSize, kBlockSize);
  ConvertImageKernel<OutputImageTraits, InputImageTraits>
      <<<num_blocks, num_threads_per_block>>>(image_out.data(), image.data(),
                                              image.cols(), image.rows());
  CUDA_CHECK(cudaStreamSynchronize(nullptr));
  return absl::OkStatus();
}

template absl::Status ConvertImage(const CudaImage<Gray8u>& image,
                                   CudaImage<Gray32f>& image_out);

template absl::Status ConvertImage(const CudaImage<Rgb8u>& image,
                                   CudaImage<Rgb32f>& image_out);

template absl::Status ConvertImage(const CudaImage<Gray32f>& image,
                                   CudaImage<Gray8u>& image_out);

template absl::Status ConvertImage(const CudaImage<Rgb32f>& image,
                                   CudaImage<Rgb8u>& image_out);

template <typename OutputImageTraits, typename InputImageTraits>
absl::StatusOr<CudaImage<OutputImageTraits>> ConvertImage(
    const CudaImage<InputImageTraits>& image) {
  static_assert(
      OutputImageTraits::kNumChannels == InputImageTraits::kNumChannels,
      "Input and output image types must have the same number of channels.");
  if (image.size() == 0) {
    return CudaImage<OutputImageTraits>();
  }
  INTR_ASSIGN_OR_RETURN(
      CudaImage<OutputImageTraits> image_out,
      CudaImage<OutputImageTraits>::Create(image.dimensions()));
  INTR_RETURN_IF_ERROR(ConvertImage(image, image_out));
  return image_out;
}

template absl::StatusOr<CudaImage<Gray32f>> ConvertImage(
    const CudaImage<Gray8u>& image);

template absl::StatusOr<CudaImage<Rgb32f>> ConvertImage(
    const CudaImage<Rgb8u>& image);

template absl::StatusOr<CudaImage<Gray8u>> ConvertImage(
    const CudaImage<Gray32f>& image);

template absl::StatusOr<CudaImage<Rgb8u>> ConvertImage(
    const CudaImage<Rgb32f>& image);

}  // namespace perception
}  // namespace intrinsic
