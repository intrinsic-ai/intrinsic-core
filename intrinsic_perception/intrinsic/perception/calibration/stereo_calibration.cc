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

#include "intrinsic/perception/calibration/stereo_calibration.h"

#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/util/status/status_macros.h"
#include "opencv2/calib3d.hpp"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/types.hpp"

namespace intrinsic {
namespace perception {

namespace {

absl::StatusOr<std::vector<std::vector<cv::Point3f>>> ToCvPoints(
    const std::vector<std::vector<Vector3f>>& vector_points) {
  std::vector<std::vector<cv::Point3f>> cv_points(vector_points.size());
  for (size_t i = 0; i < vector_points.size(); ++i) {
    std::vector<cv::Point3f> cvx_points3f = ToCvPoints3f(vector_points[i]);
    cv_points[i] = cvx_points3f;
  }
  return cv_points;
}

absl::StatusOr<std::vector<std::vector<cv::Point2f>>> ToCvPoints(
    const std::vector<std::vector<Vector2f>>& vector_points) {
  std::vector<std::vector<cv::Point2f>> cv_points(vector_points.size());
  for (size_t i = 0; i < vector_points.size(); ++i) {
    std::vector<cv::Point2f> cvx_points2f = ToCvPoints2f(vector_points[i]);
    cv_points[i] = cvx_points2f;
  }
  return cv_points;
}
}  // namespace

absl::StatusOr<StereoCalibrationResult> CalibrateStereoExtrinsic(
    const std::vector<std::vector<Vector3f>>& obj_points,
    const std::vector<std::vector<Vector2f>>& img_points0,
    const std::vector<std::vector<Vector2f>>& img_points1,
    const CameraParams& camera_params0, const CameraParams& camera_params1) {
  // Distortion coefficients must be non-const as cv::stereoCalibrate requires
  // mutable InputOutputArray arguments, even when CALIB_FIX_INTRINSIC is
  // specified. It seems as though even with CALIB_FIX_KN, any additional
  // coefficients trigger some changes which cause a segfault if the camera
  // matrix is const.
  const cv::Mat intrinsics0 =
      ToCvIntrinsicParams(camera_params0.intrinsic_params);
  cv::Mat dist_coeffs0 = ToCvDistortionParams(
      camera_params0.distortion_params.value_or(DistortionParams{}));
  const cv::Mat intrinsics1 =
      ToCvIntrinsicParams(camera_params1.intrinsic_params);
  cv::Mat dist_coeffs1 = ToCvDistortionParams(
      camera_params1.distortion_params.value_or(DistortionParams{}));
  const cv::Size image_size = DimensionsToCvSize(camera_params0.Dimensions());

  if (obj_points.empty() || img_points0.empty() || img_points1.empty()) {
    return absl::InvalidArgumentError("Calibration points cannot be empty.");
  }
  if (obj_points.size() != img_points0.size() ||
      obj_points.size() != img_points1.size()) {
    return absl::InvalidArgumentError("Calibration points size mismatch.");
  }

  const auto obj_points_cv = ToCvPoints(obj_points).value();
  const auto img_points0_cv = ToCvPoints(img_points0).value();
  const auto img_points1_cv = ToCvPoints(img_points1).value();

  int flags = cv::CALIB_FIX_INTRINSIC;

  if (dist_coeffs0.total() < 5 || dist_coeffs1.total() < 5) {
    flags |= cv::CALIB_FIX_K3;
  }

  if (dist_coeffs0.total() < 8 || dist_coeffs1.total() < 8) {
    flags |= cv::CALIB_FIX_K4 | cv::CALIB_FIX_K5 | cv::CALIB_FIX_K6;
  } else {
    flags |= cv::CALIB_RATIONAL_MODEL;
  }

  if (dist_coeffs0.total() < 12 || dist_coeffs1.total() < 12) {
    flags |= cv::CALIB_FIX_S1_S2_S3_S4;
  } else {
    flags |= cv::CALIB_THIN_PRISM_MODEL;
  }

  if (dist_coeffs0.total() < 14 || dist_coeffs1.total() < 14) {
    flags |= cv::CALIB_FIX_TAUX_TAUY;
  } else {
    flags |= cv::CALIB_TILTED_MODEL;
  }

  cv::TermCriteria stereocalib_criteria(
      cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, 1000, 1e-5);
  cv::Mat rotation, translation, essential_matrix, fundamental_matrix;
  double rpe = 0.0;
  INTR_RETURN_IF_ERROR(TryCatchCvFunction([&]() {
    rpe = cv::stereoCalibrate(obj_points_cv, img_points0_cv, img_points1_cv,
                              intrinsics0, dist_coeffs0, intrinsics1,
                              dist_coeffs1, image_size, rotation, translation,
                              essential_matrix, fundamental_matrix, flags,
                              stereocalib_criteria);
  }));

  INTR_ASSIGN_OR_RETURN(const Pose3d cam1_t_cam0,
                        FromCVPose(rotation, translation));
  return StereoCalibrationResult{.cam1_t_cam0 = cam1_t_cam0,
                                 .reprojection_error = rpe};
}

}  // namespace perception
}  // namespace intrinsic
