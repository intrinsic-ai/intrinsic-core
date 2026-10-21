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

#include "intrinsic/perception/gpu/cuda_hdr/hdr_mertens.h"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/gpu/cuda_hdr/hdr_mertens_weight_map.h"
#include "intrinsic/perception/gpu/cuda_image_proc/border_extrapolation.h"
#include "intrinsic/perception/gpu/cuda_image_proc/pixelwise.h"
#include "intrinsic/perception/gpu/cuda_image_proc/pyramid.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_image_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {
namespace {

absl::Status CheckHdrMertensParams(const HdrMertensParams& params) {
  if (params.contrast_exponent < 0.0f) {
    return absl::InvalidArgumentError("contrast_exponent must be >= 0.0.");
  }
  if (params.saturation_exponent < 0.0f) {
    return absl::InvalidArgumentError("saturation_exponent must be >= 0.0.");
  }
  if (params.well_exposedness_exponent < 0.0f) {
    return absl::InvalidArgumentError(
        "well_exposedness_exponent must be >= 0.0.");
  }
  if (params.pyramid_min_dimensions.cols < 1 ||
      params.pyramid_min_dimensions.rows < 1) {
    return absl::InvalidArgumentError("pyramid_min_dimensions must be >= 1x1.");
  }
  return absl::OkStatus();
}

template <typename InputTraits>
absl::Status ValidateHdrMertensInputs(
    const std::vector<CudaImage<InputTraits>>& ldr_images,
    const HdrMertensParams& params) {
  if (ldr_images.empty()) {
    return absl::InvalidArgumentError("ldr_images must not be empty.");
  }
  INTR_RETURN_IF_ERROR(CheckHdrMertensParams(params));

  const Dimensions base_dims = ldr_images[0].dimensions();
  if (base_dims.cols < params.pyramid_min_dimensions.cols ||
      base_dims.rows < params.pyramid_min_dimensions.rows) {
    return absl::InvalidArgumentError(
        "image dimensions must be >= pyramid_min_dimensions.");
  }
  for (size_t i = 1; i < ldr_images.size(); ++i) {
    if (ldr_images[i].dimensions() != base_dims) {
      return absl::InvalidArgumentError(
          "Image dimensions mismatch. Ensure that all images have the same "
          "dimensions.");
    }
  }
  return absl::OkStatus();
}

std::vector<Dimensions> ComputePyramidLevelDimensions(
    Dimensions base_dims, const HdrMertensParams& params) {
  std::vector<Dimensions> level_dims;
  Dimensions cur_dims = base_dims;
  while (true) {
    level_dims.push_back(cur_dims);
    const Dimensions next_dims = DimensionsPyrDown(cur_dims);
    if (next_dims.cols < params.pyramid_min_dimensions.cols ||
        next_dims.rows < params.pyramid_min_dimensions.rows ||
        next_dims.cols <= 1 || next_dims.rows <= 1) {
      break;
    }
    cur_dims = next_dims;
  }
  return level_dims;
}

template <typename ComputeTraits, typename InputTraits>
absl::StatusOr<CudaImage<ComputeTraits>> ComputeHdrMertensImpl(
    const std::vector<CudaImage<InputTraits>>& ldr_images,
    const HdrMertensParams& params);

}  // namespace

template <typename ImageTraits>
absl::StatusOr<Image<ImageTraits>> ComputeHdrMertens(
    const std::vector<const Image<ImageTraits>*>& ldr_images,
    const HdrMertensParams& params, bool use_float_precision) {
  if (ldr_images.empty()) {
    return absl::InvalidArgumentError("ldr_images must not be empty.");
  }
  INTR_RETURN_IF_ERROR(CheckHdrMertensParams(params));
  std::vector<CudaImage<ImageTraits>> cuda_ldr_images;
  cuda_ldr_images.reserve(ldr_images.size());
  for (const auto& image : ldr_images) {
    INTR_ASSIGN_OR_RETURN(CudaImage<ImageTraits> cuda_image,
                          ToCudaImage(*image));
    cuda_ldr_images.push_back(std::move(cuda_image));
  }

  if (!use_float_precision) {
    INTR_ASSIGN_OR_RETURN(const CudaImage<ImageTraits> cuda_hdr_image,
                          ComputeHdrMertens(cuda_ldr_images, params));
    return FromCudaImage(cuda_hdr_image);
  }

  using FloatImageTrait =
      std::conditional_t<std::is_same_v<ImageTraits, Rgb8u>, Rgb32f, Gray32f>;
  INTR_ASSIGN_OR_RETURN(
      const CudaImage<FloatImageTrait> cuda_hdr_image_float,
      ComputeHdrMertensImpl<FloatImageTrait>(cuda_ldr_images, params));
  // Reuse cuda_ldr_images[0] as the output buffer to avoid an extra GPU
  // allocation.
  CudaImage<ImageTraits>& cuda_hdr_image = cuda_ldr_images[0];
  INTR_RETURN_IF_ERROR(ConvertImage(cuda_hdr_image_float, cuda_hdr_image));
  return FromCudaImage(cuda_hdr_image);
}

template absl::StatusOr<Image<Gray8u>> ComputeHdrMertens(
    const std::vector<const Image<Gray8u>*>& ldr_images,
    const HdrMertensParams& params, bool use_float_precision);

template absl::StatusOr<Image<Rgb8u>> ComputeHdrMertens(
    const std::vector<const Image<Rgb8u>*>& ldr_images,
    const HdrMertensParams& params, bool use_float_precision);

namespace {

template <typename ComputeTraits, typename InputTraits>
absl::StatusOr<CudaImage<ComputeTraits>> ComputeHdrMertensImpl(
    const std::vector<CudaImage<InputTraits>>& ldr_images,
    const HdrMertensParams& params) {
  INTR_RETURN_IF_ERROR(ValidateHdrMertensInputs(ldr_images, params));

  const Dimensions base_dims = ldr_images[0].dimensions();
  const std::vector<Dimensions> level_dims =
      ComputePyramidLevelDimensions(base_dims, params);
  const size_t num_levels = level_dims.size();

  using AccumulatorTrait = std::conditional_t<
      std::is_same_v<typename ComputeTraits::ScalarType, float>, ComputeTraits,
      GenericTrait<float, ComputeTraits::kNumChannels>>;
  std::vector<CudaImage<AccumulatorTrait>> blended_pyramid(num_levels);
  for (size_t j = 0; j < num_levels; ++j) {
    INTR_ASSIGN_OR_RETURN(blended_pyramid[j],
                          CudaImage<AccumulatorTrait>::Create(level_dims[j]));
  }

  // Pre-allocate reusable downsampled pyramid buffers across all exposures.
  std::vector<CudaImage<Generic32f>> weight_down_pyramid(
      num_levels > 1 ? num_levels - 1 : 0);
  std::vector<CudaImage<ComputeTraits>> image_down_pyramid(
      num_levels > 1 ? num_levels - 1 : 0);
  for (size_t j = 0; j + 1 < num_levels; ++j) {
    INTR_ASSIGN_OR_RETURN(weight_down_pyramid[j],
                          CudaImage<Generic32f>::Create(level_dims[j + 1]));
    INTR_ASSIGN_OR_RETURN(image_down_pyramid[j],
                          CudaImage<ComputeTraits>::Create(level_dims[j + 1]));
  }

  constexpr bool kNeedsConversion = !std::is_same_v<ComputeTraits, InputTraits>;
  CudaImage<ComputeTraits> staging_image;
  size_t staged_index = ldr_images.size();
  if constexpr (kNeedsConversion) {
    INTR_ASSIGN_OR_RETURN(staging_image,
                          CudaImage<ComputeTraits>::Create(base_dims));
  }
  auto get_compute_image =
      [&](size_t idx) -> absl::StatusOr<const CudaImage<ComputeTraits>*> {
    if constexpr (kNeedsConversion) {
      if (staged_index != idx) {
        INTR_RETURN_IF_ERROR(ConvertImage(ldr_images[idx], staging_image));
        staged_index = idx;
      }
      return &staging_image;
    } else {
      return &ldr_images[idx];
    }
  };

  std::vector<CudaImage<Generic32f>> weight_maps(ldr_images.size());
  // Iterate in reverse so staging_image retains exposure 0 for the next loop.
  for (size_t rev_idx = ldr_images.size(); rev_idx > 0; --rev_idx) {
    const size_t i = rev_idx - 1;
    INTR_ASSIGN_OR_RETURN(const CudaImage<ComputeTraits>* compute_image,
                          get_compute_image(i));
    INTR_ASSIGN_OR_RETURN(
        weight_maps[i],
        ComputeHdrMertensWeightMap(*compute_image, params.contrast_exponent,
                                   params.saturation_exponent,
                                   params.well_exposedness_exponent,
                                   params.border_type));
  }
  INTR_RETURN_IF_ERROR(NormalizeWeightMaps(weight_maps));

  for (size_t i = 0; i < ldr_images.size(); ++i) {
    INTR_ASSIGN_OR_RETURN(const CudaImage<ComputeTraits>* compute_image,
                          get_compute_image(i));
    const bool accumulate = i > 0;
    for (size_t j = 0; j + 1 < num_levels; ++j) {
      const CudaImage<Generic32f>& weight_curr =
          (j == 0) ? weight_maps[i] : weight_down_pyramid[j - 1];
      const CudaImage<ComputeTraits>& image_curr =
          (j == 0) ? *compute_image : image_down_pyramid[j - 1];
      INTR_RETURN_IF_ERROR(
          PyrDown(weight_curr, weight_down_pyramid[j], params.border_type));
      INTR_RETURN_IF_ERROR(
          PyrDown(image_curr, image_down_pyramid[j], params.border_type));
      INTR_RETURN_IF_ERROR(PyrUpAndBlendLaplacian(
          weight_curr, image_curr, image_down_pyramid[j], params.border_type,
          accumulate, blended_pyramid[j]));
    }
    const CudaImage<Generic32f>& weight_last =
        (num_levels == 1) ? weight_maps[i] : weight_down_pyramid.back();
    const CudaImage<ComputeTraits>& image_last =
        (num_levels == 1) ? *compute_image : image_down_pyramid.back();
    INTR_RETURN_IF_ERROR(BlendWeightMapAndImage(
        weight_last, image_last, accumulate, blended_pyramid.back()));
    weight_maps[i] = {};
  }

  for (int i = static_cast<int>(num_levels) - 2; i >= 0; --i) {
    INTR_RETURN_IF_ERROR(PyrUp(blended_pyramid[i + 1], blended_pyramid[i],
                               params.border_type, /*accumulate=*/true));
  }
  INTR_RETURN_IF_ERROR(Clamp(blended_pyramid[0], /*min=*/0.0f,
                             static_cast<float>(ComputeTraits::kIntensityMax)));
  if constexpr (std::is_same_v<AccumulatorTrait, ComputeTraits>) {
    return std::move(blended_pyramid[0]);
  } else {
    return RoundThenCast<ComputeTraits>(blended_pyramid[0]);
  }
}

}  // namespace

template <typename ImageTraits>
absl::StatusOr<CudaImage<ImageTraits>> ComputeHdrMertens(
    const std::vector<CudaImage<ImageTraits>>& ldr_images,
    const HdrMertensParams& params) {
  return ComputeHdrMertensImpl<ImageTraits>(ldr_images, params);
}

template absl::StatusOr<CudaImage<Gray8u>> ComputeHdrMertens(
    const std::vector<CudaImage<Gray8u>>& ldr_images,
    const HdrMertensParams& params);

template absl::StatusOr<CudaImage<Rgb8u>> ComputeHdrMertens(
    const std::vector<CudaImage<Rgb8u>>& ldr_images,
    const HdrMertensParams& params);

template absl::StatusOr<CudaImage<Gray32f>> ComputeHdrMertens(
    const std::vector<CudaImage<Gray32f>>& ldr_images,
    const HdrMertensParams& params);

template absl::StatusOr<CudaImage<Rgb32f>> ComputeHdrMertens(
    const std::vector<CudaImage<Rgb32f>>& ldr_images,
    const HdrMertensParams& params);

}  // namespace perception
}  // namespace intrinsic
