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

#include "intrinsic/perception/calibration/intrinsic_calibration_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/color_gradient.h"
#include "intrinsic/perception/core/coordinate.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/core/operators.h"
#include "intrinsic/perception/core/rectangle.h"
#include "intrinsic/perception/proto/v1/pattern_detection_result.pb.h"
#include "intrinsic/perception/proto/vector.pb.h"
#include "intrinsic/perception/proto_conversion/vector.h"
#include "intrinsic/util/status/status_macros.h"
#include "opencv2/calib3d.hpp"
#include "opencv2/core.hpp"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/geometry.hpp"
#include "opencv2/imgproc.hpp"

namespace intrinsic {
namespace perception {
namespace {

absl::Status IsValidPolygon(const std::vector<Vector2f>& polygon) {
  if (polygon.empty()) {
    return absl::InternalError(
        "Polygon empty. Could not remove polygon from coverage map.");
  }
  if (polygon.size() < 3) {
    return absl::InternalError("Polygon needs to contain at least 3 vertices.");
  }
  return absl::OkStatus();
}

Image<Gray8u> GetPolygonCoverageImage(const std::vector<Vector2f>& polygon,
                                      const Dimensions& dimensions) {
  const Image<Bool8u> polygon_mask = PolygonMask(dimensions, polygon);
  Image<Gray8u> coverage_map(dimensions, Gray8u::PixelType(0));
  FillIf(coverage_map, polygon_mask, Gray8u::PixelType(1));
  return coverage_map;
}

struct LineSegment {
  double estimated_length = 0.0;
  double true_length = 0.0;
  double depth_normalization_factor = 0.0;
};

// Computes the intersection between:
// - a ray defined by the reprojected camera ray of an image point
// - a plane defined by `plane_coefficients`
absl::StatusOr<Vector3d> CalculateRayPlaneIntersection(
    const IntrinsicParams& intrinsic_params, const Vector2d& image_point,
    const Vector4d& plane_coefficients) {
  Vector3d homogenous_image_point = Vector3d(image_point[0], image_point[1], 1);
  Vector3d normalized_image_point =
      intrinsic_params.inverse_matrix() * homogenous_image_point;
  double t = -plane_coefficients[3] /
             (plane_coefficients[0] * normalized_image_point[0] +
              plane_coefficients[1] * normalized_image_point[1] +
              plane_coefficients[2]);
  Vector3d intersection_point = normalized_image_point * t;
  return intersection_point;
}

// Computes estimated and known 3D distances between detected charuco corners.
// Each pair uses the first corner detection in the input list as the reference.
absl::StatusOr<std::vector<LineSegment>> GetLineSegments(
    const IntrinsicParams& intrinsic_params,
    const std::vector<Vector2d>& image_points,
    const Vector4d& plane_coefficients_camera,
    const std::vector<Vector3d>& model_points) {
  std::vector<LineSegment> line_segments;
  if (image_points.empty()) {
    return absl::InvalidArgumentError("`image_points` empty.");
  }
  if (image_points.size() != model_points.size()) {
    return absl::InvalidArgumentError(
        "`image_points` and `model_points` have different number of points.");
  }
  const Vector2d& reference_image_point = image_points[0];
  const Vector3d& reference_model_point = model_points[0];
  INTR_ASSIGN_OR_RETURN(
      const Vector3d reference_estimated_3d_point,
      CalculateRayPlaneIntersection(intrinsic_params, reference_image_point,
                                    plane_coefficients_camera));
  for (int i = 0; i < image_points.size(); ++i) {
    INTR_ASSIGN_OR_RETURN(
        const Vector3d point_3d,
        CalculateRayPlaneIntersection(intrinsic_params, image_points[i],
                                      plane_coefficients_camera));
    line_segments.push_back(
        {.estimated_length = (reference_estimated_3d_point - point_3d).norm(),
         .true_length = (reference_model_point - model_points[i]).norm(),
         .depth_normalization_factor =
             (reference_estimated_3d_point[2] * point_3d[2])});
  }

  return line_segments;
}

std::vector<intrinsic_proto::perception::v1::PatternDetection>
UndistortPatternDetections(
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        pattern_detections,
    const CameraParams& camera_params) {
  cv::Mat intrinsic_params =
      ToCvIntrinsicParams(camera_params.intrinsic_params);
  cv::Mat distortion_params = ToCvDistortionParams(
      camera_params.distortion_params.value_or(DistortionParams()));
  std::vector<intrinsic_proto::perception::v1::PatternDetection>
      undistorted_pattern_detections;
  undistorted_pattern_detections.reserve(pattern_detections.size());
  for (const auto& pattern_detection : pattern_detections) {
    std::vector<cv::Point2f> points_2d;
    points_2d.reserve(pattern_detection.image_points_size());
    std::transform(pattern_detection.image_points().begin(),
                   pattern_detection.image_points().end(),
                   std::back_inserter(points_2d), [](const auto& point_2d) {
                     return cv::Point2f(point_2d.x(), point_2d.y());
                   });
    std::vector<cv::Point2f> undistorted_points_2d;
    undistorted_points_2d.reserve(pattern_detection.image_points_size());
    cv::undistortPoints(points_2d, undistorted_points_2d, intrinsic_params,
                        distortion_params, cv::noArray(), intrinsic_params);
    intrinsic_proto::perception::v1::PatternDetection
        undistorted_pattern_detection = pattern_detection;
    undistorted_pattern_detection.clear_image_points();
    std::transform(undistorted_points_2d.begin(), undistorted_points_2d.end(),
                   google::protobuf::RepeatedFieldBackInserter(
                       undistorted_pattern_detection.mutable_image_points()),
                   [](const cv::Point2f& point) {
                     intrinsic_proto::perception::Vector2f point_proto;
                     point_proto.set_x(point.x);
                     point_proto.set_y(point.y);
                     return point_proto;
                   });
    undistorted_pattern_detections.push_back(undistorted_pattern_detection);
  }
  return undistorted_pattern_detections;
}

inline double round_to(double value, double precision = 1.0) {
  return std::round(value / precision) * precision;
}

absl::StatusOr<Intrinsics3DErrorStats> Compute3DErrorStats(
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        pattern_detections,
    const IntrinsicParams& intrinsic_params) {
  if (pattern_detections.empty()) {
    return absl::InvalidArgumentError(
        "At least one pattern detection needs to be provided in "
        "pattern_detections.");
  }

  double sum_mean_errors = 0;
  double max_error = 0;
  int valid_pattern_detections = 0;
  for (const auto& pattern_detection : pattern_detections) {
    if (pattern_detection.marker_points_size() < 4) continue;
    // Extract 3D model points from PatternDetection.
    std::vector<Vector3d> model_points;
    model_points.reserve(pattern_detection.marker_points_size());
    const auto& marker_points = pattern_detection.marker_points();
    std::transform(
        marker_points.begin(), marker_points.end(),
        std::back_inserter(model_points),
        [](const intrinsic_proto::perception::Vector3f& marker_point) {
          return Vector3d(marker_point.x(), marker_point.y(), marker_point.z());
        });
    absl::StatusOr<Pose3d> camera_t_board = ComputePoseFromPatternDetection(
        pattern_detection, CameraParams(intrinsic_params));

    if (!camera_t_board.ok()) {
      continue;
    }
    valid_pattern_detections++;

    const Vector4d plane_coefficients_board = Vector4d(0, 0, 1, 0);
    const Vector4d plane_coefficients_camera =
        camera_t_board->inverse().matrix().transpose() *
        plane_coefficients_board;

    // Extract 2D image points from PatternDetection.
    std::vector<Vector2d> image_points;
    image_points.reserve(pattern_detection.image_points_size());
    std::transform(pattern_detection.image_points().begin(),
                   pattern_detection.image_points().end(),
                   std::back_inserter(image_points),
                   [](const intrinsic_proto::perception::Vector2f& point) {
                     return Vector2d(point.x(), point.y());
                   });
    std::vector<LineSegment> line_segments;
    INTR_ASSIGN_OR_RETURN(
        line_segments,
        GetLineSegments(intrinsic_params, image_points,
                        plane_coefficients_camera, model_points));
    double total_error = 0;
    for (const auto& segment : line_segments) {
      double err = std::abs(segment.true_length - segment.estimated_length);
      err = err * segment.true_length / segment.depth_normalization_factor;
      total_error += err;
      if (err > max_error) {
        max_error = err;
      }
    }
    sum_mean_errors += total_error / line_segments.size();
  }

  if (valid_pattern_detections == 0) {
    return absl::InvalidArgumentError(
        "At least one valid pattern detection with 4 non collinear marker "
        "points needs to be provided.");
  }
  sum_mean_errors /= valid_pattern_detections;
  return Intrinsics3DErrorStats{
      .mean_3d_error = round_to(sum_mean_errors, 0.000001),
      .max_3d_error = round_to(max_error, 0.000001)};
}

}  // namespace

absl::StatusOr<std::vector<Vector2f>> ComputeConvexHull(
    const std::vector<Vector2f>& image_points) {
  if (image_points.empty()) {
    return absl::InvalidArgumentError("`image_points` empty.");
  }
  std::vector<cv::Point2f> cvx_points;
  cvx_points.reserve(image_points.size());
  std::transform(
      image_points.begin(), image_points.end(), std::back_inserter(cvx_points),
      [](const Vector2f& point) { return cv::Point2f(point.x(), point.y()); });
  std::vector<cv::Point2f> cvx_hull;
  cv::convexHull(cvx_points, cvx_hull);
  std::vector<Vector2f> hull;
  hull.reserve(cvx_hull.size());
  std::transform(
      cvx_hull.begin(), cvx_hull.end(), std::back_inserter(hull),
      [](const cv::Point2f& point) { return Vector2f(point.x, point.y); });
  return hull;
}

absl::StatusOr<Rectangle> ComputeRoi(
    const std::vector<Vector2f>& image_points) {
  if (image_points.empty()) {
    return absl::InvalidArgumentError("`image_points` empty.");
  }

  Vector2f min = image_points[0];
  Vector2f max = image_points[0];
  for (int i = 1; i < image_points.size(); ++i) {
    min.x() = std::min(min.x(), image_points[i].x());
    min.y() = std::min(min.y(), image_points[i].y());
    max.x() = std::max(max.x(), image_points[i].x());
    max.y() = std::max(max.y(), image_points[i].y());
  }
  return Rectangle(Coordinate(min.x(), min.y()), Coordinate(max.x(), max.y()));
}

absl::StatusOr<Image<Gray8u>> AddPolygonToCoverageMap(
    const std::vector<Vector2f>& polygon, const Image<Gray8u>& coverage_map) {
  INTR_RETURN_IF_ERROR(IsValidPolygon(polygon));
  const Image<Gray8u> polygon_coverage =
      GetPolygonCoverageImage(polygon, coverage_map.dimensions());
  return PixelwiseAdd(polygon_coverage, coverage_map);
}

absl::StatusOr<Image<Gray8u>> RemovePolygonFromCoverageMap(
    const std::vector<Vector2f>& polygon, const Image<Gray8u>& coverage_map) {
  INTR_RETURN_IF_ERROR(IsValidPolygon(polygon));
  const Image<Gray8u> polygon_coverage =
      GetPolygonCoverageImage(polygon, coverage_map.dimensions());
  return PixelwiseSubtract(coverage_map, polygon_coverage);
}

Image<Rgb8u> GenerateHeatmap(const Image<Gray8u>& coverage_map,
                             const Dimensions& resize, int max_count) {
  if (coverage_map.dimensions().cols == 0 ||
      coverage_map.dimensions().rows == 0) {
    return Image<Rgb8u>(coverage_map.dimensions());
  }
  Image<Gray8u> resized(resize);
  cv::resize(UnsafeConstCastCvMat(coverage_map), ToCvMat(resized),
             DimensionsToCvSize(resized.dimensions()), 0, 0, cv::INTER_AREA);

  const Rgb8u::PixelType red(255, 0, 0);
  const Rgb8u::PixelType yellow(255, 255, 0);
  const Rgb8u::PixelType blue(60, 177, 188);
  const auto color_gradient = CustomColorGradientFunction<Rgb8u>({
      {0.0f, red},
      {0.5f * max_count, yellow},
      {max_count, blue},
  });
  Image<Rgb8u> heatmap = Transform<Rgb8u>(
      resized, [&color_gradient](const Gray8u::PixelType& pix) {
        return color_gradient(pix);
      });
  cv::Mat mat = UnsafeConstCastCvMat(heatmap);
  cv::blur(mat, mat, cv::Size(2, 2));
  return heatmap;
}

absl::StatusOr<Pose3d> ComputePoseFromPatternDetection(
    const intrinsic_proto::perception::v1::PatternDetection& pattern_detection,
    const CameraParams& camera_params) {
  if (pattern_detection.image_points_size() < 4) {
    return absl::InternalError(absl::StrFormat(
        "Not enough point correspondences to compute pose. Need at least 4 "
        "2D-3D correspondences, but only got %d.",
        pattern_detection.image_points_size()));
  }
  std::vector<cv::Point3f> points_3d;
  points_3d.reserve(pattern_detection.marker_points_size());
  std::transform(pattern_detection.marker_points().begin(),
                 pattern_detection.marker_points().end(),
                 std::back_inserter(points_3d), [](const auto& point_3d) {
                   return cv::Point3f(point_3d.x(), point_3d.y(), point_3d.z());
                 });
  std::vector<cv::Point2f> points_2d;
  points_2d.reserve(pattern_detection.image_points_size());
  std::transform(pattern_detection.image_points().begin(),
                 pattern_detection.image_points().end(),
                 std::back_inserter(points_2d), [](const auto& point_2d) {
                   return cv::Point2f(point_2d.x(), point_2d.y());
                 });
  cv::Mat intrinsic_params =
      ToCvIntrinsicParams(camera_params.intrinsic_params);
  cv::Mat distortion_params = ToCvDistortionParams(
      camera_params.distortion_params.value_or(DistortionParams()));
  cv::Mat translation;
  cv::Mat rotation;
  const bool found = cv::solvePnP(points_3d, points_2d, intrinsic_params,
                                  distortion_params, rotation, translation);
  // solvePnP may return NaN values for rotation or translation.
  if (!found || !cv::checkRange(rotation) || !cv::checkRange(translation)) {
    return absl::NotFoundError("No pose could be computed.");
  }
  cv::Mat rotation_vec;
  cv::Rodrigues(rotation, rotation_vec);
  return FromCVPose(rotation_vec, translation);
}

absl::StatusOr<Intrinsics3DErrorStats>
GetIntrinsics3DErrorFromPatternDetections(
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        pattern_detections,
    const IntrinsicParams& intrinsic_params) {
  return Compute3DErrorStats(pattern_detections, intrinsic_params);
}

absl::StatusOr<Intrinsics3DErrorStats>
GetIntrinsics3DErrorFromPatternDetections(
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        pattern_detections,
    const CameraParams& camera_params) {
  if (pattern_detections.empty()) {
    return absl::InvalidArgumentError(
        "At least one pattern detection needs to be provided in "
        "pattern_detections.");
  }
  return GetIntrinsics3DErrorFromPatternDetections(
      UndistortPatternDetections(pattern_detections, camera_params),
      camera_params.intrinsic_params);
}

bool IsRedundantPatternDetection(
    const intrinsic_proto::perception::v1::PatternDetection& pattern_detection,
    const intrinsic_proto::perception::v1::PatternDetection&
        previous_pattern_detection,
    const int pixel_wise_difference_threshold) {
  int marker_id_matches = 0;
  for (int i = 0; i < previous_pattern_detection.marker_ids_size(); ++i) {
    auto id = std::find(pattern_detection.marker_ids().begin(),
                        pattern_detection.marker_ids().end(),
                        previous_pattern_detection.marker_ids(i));
    if (id != pattern_detection.marker_ids().end()) {
      ++marker_id_matches;
      const int idx = id - pattern_detection.marker_ids().begin();
      const Vector2f image_point1 =
          FromProto(pattern_detection.image_points(idx));
      const Vector2f image_point2 =
          FromProto(previous_pattern_detection.image_points(i));
      const double pixel_wise_difference = (image_point1 - image_point2).norm();
      if (pixel_wise_difference > pixel_wise_difference_threshold) {
        // Once we know that at least one point is different, we can return
        // immediately.
        return false;
      }
    }
    if (marker_id_matches >= 3) {
      // We need to check at most 3 points, since 3 points uniquely determine a
      // pose.
      break;
    }
  }
  // If less than 3 total matches were found, the detections cover a different
  // area of the calibration pattern.
  return marker_id_matches >= 3;
}

bool IsLowRankPatternDetection(
    const intrinsic_proto::perception::v1::PatternDetection&
        pattern_detection) {
  if (pattern_detection.marker_points().empty()) {
    return true;
  }
  Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
  for (const intrinsic_proto::perception::Vector3f& p :
       pattern_detection.marker_points()) {
    centroid += FromProto(p);
  }
  centroid /= pattern_detection.marker_points_size();
  Eigen::Matrix3f covariance = Eigen::Matrix3f::Zero();
  for (const intrinsic_proto::perception::Vector3f& p :
       pattern_detection.marker_points()) {
    const Eigen::Vector3f pc = FromProto(p) - centroid;
    covariance += pc * pc.transpose();
  }
  JacobiSVD<Matrix3f> svd(covariance, EigenComputeFullU);
  return svd.rank() < 2;
}

absl::StatusOr<ReprojectionErrorResult> ComputeReprojectionErrorForDetection(
    const intrinsic_proto::perception::v1::PatternDetection& detection,
    const CameraParams& camera_params, const Pose3d& camera_t_board) {
  if (detection.marker_points_size() != detection.image_points_size()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "PatternDetection size mismatch: %d marker points vs "
        "%d image points.",
        detection.marker_points_size(), detection.image_points_size()));
  }
  ReprojectionErrorResult result;

  cv::Mat rvec(3, 1, CV_64F);
  cv::Mat tvec(3, 1, CV_64F);
  Eigen::AngleAxisd angle_axis(camera_t_board.quaternion());
  Eigen::Vector3d rvec_eigen;
  if (angle_axis.angle() > 1e-12) {
    rvec_eigen = angle_axis.angle() * angle_axis.axis();
  } else {
    rvec_eigen = Eigen::Vector3d::Zero();
  }
  rvec.at<double>(0) = rvec_eigen[0];
  rvec.at<double>(1) = rvec_eigen[1];
  rvec.at<double>(2) = rvec_eigen[2];
  tvec.at<double>(0) = camera_t_board.translation()[0];
  tvec.at<double>(1) = camera_t_board.translation()[1];
  tvec.at<double>(2) = camera_t_board.translation()[2];

  std::vector<cv::Point3f> object_points;
  object_points.reserve(detection.marker_points_size());
  for (const auto& pt : detection.marker_points()) {
    object_points.push_back(cv::Point3f(pt.x(), pt.y(), pt.z()));
  }

  cv::Mat intrinsic_matrix =
      ToCvIntrinsicParams(camera_params.intrinsic_params);
  cv::Mat distortion_coeffs = ToCvDistortionParams(
      camera_params.distortion_params.value_or(DistortionParams()));

  std::vector<cv::Point2f> projected_points;
  cv::projectPoints(object_points, rvec, tvec, intrinsic_matrix,
                    distortion_coeffs, projected_points);

  std::vector<cv::Point2f> detected_points;
  detected_points.reserve(detection.image_points_size());
  for (const auto& pt : detection.image_points()) {
    detected_points.push_back(cv::Point2f(pt.x(), pt.y()));
  }

  result.squared_error =
      cv::norm(detected_points, projected_points, cv::NORM_L2SQR);
  result.num_points = detected_points.size();

  for (size_t p = 0; p < detected_points.size(); ++p) {
    double pt_err = cv::norm(detected_points[p] - projected_points[p]);
    result.max_error = std::max(result.max_error, pt_err);
  }

  return result;
}
}  // namespace perception
}  // namespace intrinsic
