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


#include "intrinsic/perception/core/hdr_utils.h"

#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/gpu/cuda_hdr/hdr_mertens.h"
#include "intrinsic/perception/gpu/cuda_utils/cuda_utils.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "magic_enum/magic_enum.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/core/cvstd_wrapper.hpp"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/traits.hpp"
#include "opencv2/photo.hpp"

namespace intrinsic {
namespace perception {

template <typename ImageTraits>
absl::StatusOr<Image<ImageTraits>> ComputeHdrMertensCpu(
    const std::vector<const Image<ImageTraits>*>& ldr_images,
    float contrast_weight, float saturation_weight, float exposure_weight) {
  if (ldr_images.size() < 2) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "HDR requires 2 or more LDR images. " << ldr_images.size()
           << " provided.";
  }

  std::vector<cv::Mat> ldr_mats;
  ldr_mats.reserve(ldr_images.size());
  for (const Image<ImageTraits>* ldr_image : ldr_images) {
    if (ldr_image == nullptr) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Input LDR image cannot be null.";
    }
    // 1-d images can result into segmentation fault in OpenCV.
    if (ldr_image->rows() <= 1 || ldr_image->cols() <= 1) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Input LDR image has invalid dimensions. Rows and columns "
                "should be greater than 1.";
    }
    ldr_mats.push_back(UnsafeConstCastCvMat(*ldr_image));
  }

  cv::Mat hdr_mat;
  const absl::Time start_time = absl::Now();
  const cv::Ptr<cv::MergeMertens> merge_mertens = cv::createMergeMertens(
      contrast_weight, saturation_weight, exposure_weight);
  merge_mertens->process(ldr_mats, hdr_mat);

  hdr_mat.convertTo(
      hdr_mat,
      CV_MAKETYPE(cv::DataType<typename ImageTraits::ScalarType>::depth,
                  ImageTraits::kNumChannels),
      static_cast<double>(ImageTraits::kIntensityMax));
  LOG(INFO) << "Computing HDR using Mertens fusion took "
            << (absl::Now() - start_time) << ".";

  return MoveToImage<ImageTraits>(std::move(hdr_mat));
}

template absl::StatusOr<Image<Gray8u>> ComputeHdrMertensCpu(
    const std::vector<const Image<Gray8u>*>& ldr_images, float contrast_weight,
    float saturation_weight, float exposure_weight);

template absl::StatusOr<Image<Rgb8u>> ComputeHdrMertensCpu(
    const std::vector<const Image<Rgb8u>*>& ldr_images, float contrast_weight,
    float saturation_weight, float exposure_weight);

template <typename ImageTraits>
absl::StatusOr<Image<ImageTraits>> LdrToHdr(
    const std::vector<const Image<ImageTraits>*>& ldr_images,
    HdrOperator hdr_operator) {
  if (ldr_images.size() < 2) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "HDR requires 2 or more LDR images. " << ldr_images.size()
           << " provided.";
  }

  if (hdr_operator != HdrOperator::kMertens) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << magic_enum::enum_name(hdr_operator)
           << " is not a valid HDR operator. Use mertens.";
  }

  if (GpuIsAvailable()) {
    const absl::Status status =
        VerifyCompiledBinaryAgainstCurrentComputeCapability();
    if (status.ok()) {
      const absl::Time start_time = absl::Now();
      const HdrMertensParams params = {.contrast_exponent = 1.0f,
                                       .saturation_exponent = 1.0f,
                                       .well_exposedness_exponent = 1.0f};
      INTR_ASSIGN_OR_RETURN(Image<ImageTraits> hdr_image,
                            ComputeHdrMertens(ldr_images, params,
                                              /*use_float_precision=*/true));
      LOG(INFO) << "Computing HDR using Mertens fusion on the GPU took "
                << (absl::Now() - start_time) << ".";
      return hdr_image;
    }
    LOG(WARNING) << status;
  }

  return ComputeHdrMertensCpu(ldr_images);
}

template absl::StatusOr<Image<Gray8u>> LdrToHdr(
    const std::vector<const Image<Gray8u>*>& ldr_images,
    HdrOperator hdr_operator);

template absl::StatusOr<Image<Rgb8u>> LdrToHdr(
    const std::vector<const Image<Rgb8u>*>& ldr_images,
    HdrOperator hdr_operator);

}  // namespace perception
}  // namespace intrinsic
