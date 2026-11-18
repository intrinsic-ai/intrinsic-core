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

#include "intrinsic/perception/core/undistortion.h"

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/util/status/ret_check.h"
#include "opencv2/core/base.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgproc/imgproc.hpp"

namespace intrinsic {
namespace perception {

namespace {

template <typename TImageType>
auto undistorted(TImageType&& distorted_image,
                 const Image<Generic16i2>& undist_map_xy,
                 const Image<Generic16u>& undist_interp_tables,
                 int interpolation_type = cv::INTER_LINEAR) {
  using ImageType = std::decay_t<TImageType>;

  if (undist_map_xy.empty() || undist_interp_tables.empty()) {
    return std::forward<TImageType>(distorted_image);
  }

  ImageType undistorted_image{distorted_image.dimensions()};

  const cv::Mat cv_distorted_image = UnsafeConstCastCvMat(distorted_image);
  cv::Mat cv_undistorted_image = ToCvMat(undistorted_image);

  const cv::Mat cv_map_xy = UnsafeConstCastCvMat(undist_map_xy);
  const cv::Mat cv_interp_tables = UnsafeConstCastCvMat(undist_interp_tables);

  cv::remap(cv_distorted_image, cv_undistorted_image, cv_map_xy,
            cv_interp_tables, interpolation_type, cv::BORDER_CONSTANT,
            cv::Scalar{});

  return undistorted_image;
}

}  // namespace

Undistortion::Undistortion(const CameraParams& camera_params)
    : camera_params_(camera_params) {
  if (!camera_params.distortion_params.has_value()) {
    return;
  }
  const DistortionParams& dp = camera_params.distortion_params.value();
  if (DistortionParamsNear(dp, DistortionParams(), 0.0).ok()) {
    return;
  }

  const IntrinsicParams& ip = camera_params.intrinsic_params;
  // clang-format off
  double raw_intrinsic_matrix[9] = {
    ip.focal_length_x(), 0.0f,                ip.principal_point().x(),
    0.0f,                ip.focal_length_y(), ip.principal_point().y(),
    0.0f,                0.0f,                1.0f
  };
  // clang-format on

  const cv::Mat intrinsic_matrix{3, 3, CV_64F, raw_intrinsic_matrix};

  std::vector<double> distortion_coeffs = {dp.k1, dp.k2, dp.p1, dp.p2, dp.k3,
                                           dp.k4, dp.k5, dp.k6, dp.s1, dp.s2,
                                           dp.s3, dp.s4, dp.tx, dp.ty};

  // Determine the required size of the distortion coefficient vector based on
  // the highest-order non-zero parameter. OpenCV's `initUndistortRectifyMap`
  // expects a vector containing all parameters up to the highest-order
  // non-zero coefficient.
  size_t required_size = 4;  // Minimum size for k1, k2, p1, p2
  if (dp.tx != 0.0 || dp.ty != 0.0) {
    required_size = 14;
  } else if (dp.s1 != 0.0 || dp.s2 != 0.0 || dp.s3 != 0.0 || dp.s4 != 0.0) {
    required_size = 12;
  } else if (dp.k4 != 0.0 || dp.k5 != 0.0 || dp.k6 != 0.0) {
    required_size = 8;
  } else if (dp.k3 != 0.0) {
    required_size = 5;
  }
  distortion_coeffs.resize(required_size);

  const cv::Size2i image_size{ip.dimensions().cols, ip.dimensions().rows};

  cv::Mat cv_map_xy;
  cv::Mat cv_interp_tables;

  // This function writes to / initializes undist_map_x_ and undist_map_y_
  cv::initUndistortRectifyMap(intrinsic_matrix, distortion_coeffs, cv::Mat(),
                              intrinsic_matrix, image_size, CV_16SC2, cv_map_xy,
                              cv_interp_tables);

  undist_map_xy_ = MoveToImage<Generic16i2>(std::move(cv_map_xy));
  undist_interp_tables_ = MoveToImage<Generic16u>(std::move(cv_interp_tables));
}

const std::optional<CameraParams>& Undistortion::camera_params() const {
  return camera_params_;
}

template <>
Image<Rgb8u> Undistortion::operator()(
    const Image<Rgb8u>& distorted_image) const {
  return undistorted(distorted_image, undist_map_xy_, undist_interp_tables_);
}

template <>
Image<Gray8u> Undistortion::operator()(
    const Image<Gray8u>& distorted_image) const {
  return undistorted(distorted_image, undist_map_xy_, undist_interp_tables_);
}

template <>
Image<Gray32f> Undistortion::operator()(
    const Image<Gray32f>& distorted_image) const {
  return undistorted(distorted_image, undist_map_xy_, undist_interp_tables_);
}

template <>
Image<Depth32f> Undistortion::operator()(
    const Image<Depth32f>& distorted_image) const {
  return undistorted(distorted_image, undist_map_xy_, undist_interp_tables_,
                     cv::INTER_NEAREST);
}

template <>
Image<Rgb8u> Undistortion::operator()(Image<Rgb8u>&& distorted_image) const {
  return undistorted(std::move(distorted_image), undist_map_xy_,
                     undist_interp_tables_);
}

template <>
Image<Gray8u> Undistortion::operator()(Image<Gray8u>&& distorted_image) const {
  return undistorted(std::move(distorted_image), undist_map_xy_,
                     undist_interp_tables_);
}

template <>
Image<Gray32f> Undistortion::operator()(
    Image<Gray32f>&& distorted_image) const {
  return undistorted(std::move(distorted_image), undist_map_xy_,
                     undist_interp_tables_);
}

template <>
Image<Depth32f> Undistortion::operator()(
    Image<Depth32f>&& distorted_image) const {
  return undistorted(std::move(distorted_image), undist_map_xy_,
                     undist_interp_tables_, cv::INTER_NEAREST);
}

absl::StatusOr<Image<Generic32f2>> CreateUndistortionMap(
    const CameraParams& camera_params) {
  INTR_RET_CHECK(camera_params.distortion_params.has_value());

  const IntrinsicParams& ip = camera_params.intrinsic_params;
  // clang-format off
  double raw_intrinsic_matrix[9] = {
    ip.focal_length_x(), 0.0f,                ip.principal_point().x(),
    0.0f,                ip.focal_length_y(), ip.principal_point().y(),
    0.0f,                0.0f,                1.0f
  };
  // clang-format on

  const cv::Mat intrinsic_matrix{3, 3, CV_64F, raw_intrinsic_matrix};

  const DistortionParams& dp = camera_params.distortion_params.value();
  std::vector<double> distortion_coeffs = {dp.k1, dp.k2, dp.p1, dp.p2, dp.k3,
                                           dp.k4, dp.k5, dp.k6, dp.s1, dp.s2,
                                           dp.s3, dp.s4, dp.tx, dp.ty};

  // Determine the required size of the distortion coefficient vector based on
  // the highest-order non-zero parameter. OpenCV's `initUndistortRectifyMap`
  // expects a vector containing all parameters up to the highest-order
  // non-zero coefficient.
  size_t required_size = 4;  // Minimum size for k1, k2, p1, p2
  if (dp.tx != 0.0 || dp.ty != 0.0) {
    required_size = 14;
  } else if (dp.s1 != 0.0 || dp.s2 != 0.0 || dp.s3 != 0.0 || dp.s4 != 0.0) {
    required_size = 12;
  } else if (dp.k4 != 0.0 || dp.k5 != 0.0 || dp.k6 != 0.0) {
    required_size = 8;
  } else if (dp.k3 != 0.0) {
    required_size = 5;
  }
  distortion_coeffs.resize(required_size);

  const cv::Size2i image_size{ip.dimensions().cols, ip.dimensions().rows};

  cv::Mat cv_map_xy;
  cv::Mat unused;
  cv::initUndistortRectifyMap(intrinsic_matrix, distortion_coeffs, cv::Mat(),
                              intrinsic_matrix, image_size, CV_32FC2, cv_map_xy,
                              unused);

  return MoveToImage<Generic32f2>(std::move(cv_map_xy));
}

}  // namespace perception
}  // namespace intrinsic
