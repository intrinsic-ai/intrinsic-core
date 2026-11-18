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

#include "intrinsic/perception/calibration/charuco_pattern_utils.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/conversion.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/mesh.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/proto/v1/charuco_pattern.pb.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "opencv2/core/core.hpp"
#include "opencv2/core/cvstd_wrapper.hpp"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/objdetect/aruco_board.hpp"
#include "opencv2/objdetect/aruco_detector.hpp"
#include "opencv2/objdetect/aruco_dictionary.hpp"
#include "opencv2/objdetect/charuco_detector.hpp"

namespace intrinsic::perception {
namespace {

using intrinsic_proto::perception::v1::ArucoDictionary;

constexpr float kMinMarkerDistancePixels = 10.0f;

// Checks if any ArUco marker ID is detected multiple times.
// Note: This duplicate check is not 100% perfect. For some rare cases,
// like if for both boards exactly half the board is visible and they do not
// share any overlapping IDs, this would not trigger.
absl::Status CheckForDuplicateMarkers(const std::vector<int>& marker_ids) {
  absl::flat_hash_set<int> unique_marker_ids;
  for (int id : marker_ids) {
    if (!unique_marker_ids.insert(id).second) {
      return absl::FailedPreconditionError(
          "Multiple Charuco boards detected in the image. Pose estimation "
          "requires exactly one Charuco board to be visible.");
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<cv::Ptr<cv::aruco::CharucoBoard>> CreateCharucoBoard(
    const intrinsic_proto::perception::v1::CharucoPattern& charuco_pattern) {
  const auto kArucoDictionaryLookup =
      absl::flat_hash_map<ArucoDictionary, cv::aruco::PredefinedDictionaryType>(
          {{ArucoDictionary::DICT_ARUCO_ORIGINAL,
            cv::aruco::DICT_ARUCO_ORIGINAL},
           {ArucoDictionary::DICT_4X4_50, cv::aruco::DICT_4X4_50},
           {ArucoDictionary::DICT_4X4_100, cv::aruco::DICT_4X4_100},
           {ArucoDictionary::DICT_4X4_250, cv::aruco::DICT_4X4_250},
           {ArucoDictionary::DICT_4X4_1000, cv::aruco::DICT_4X4_1000},
           {ArucoDictionary::DICT_5X5_50, cv::aruco::DICT_5X5_50},
           {ArucoDictionary::DICT_5X5_100, cv::aruco::DICT_5X5_100},
           {ArucoDictionary::DICT_5X5_250, cv::aruco::DICT_5X5_250},
           {ArucoDictionary::DICT_5X5_1000, cv::aruco::DICT_5X5_1000},
           {ArucoDictionary::DICT_6X6_50, cv::aruco::DICT_6X6_50},
           {ArucoDictionary::DICT_6X6_100, cv::aruco::DICT_6X6_100},
           {ArucoDictionary::DICT_6X6_250, cv::aruco::DICT_6X6_250},
           {ArucoDictionary::DICT_6X6_1000, cv::aruco::DICT_6X6_1000},
           {ArucoDictionary::DICT_7X7_50, cv::aruco::DICT_7X7_50},
           {ArucoDictionary::DICT_7X7_100, cv::aruco::DICT_7X7_100},
           {ArucoDictionary::DICT_7X7_250, cv::aruco::DICT_7X7_250},
           {ArucoDictionary::DICT_7X7_1000, cv::aruco::DICT_7X7_1000}});

  const auto it = kArucoDictionaryLookup.find(charuco_pattern.dictionary());
  if (it == kArucoDictionaryLookup.end()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Invalid ArucoDictionary given: %s", ""));
  }
  const cv::aruco::Dictionary dictionary =
      cv::aruco::getPredefinedDictionary(it->second);
  return cv::makePtr<cv::aruco::CharucoBoard>(
      cv::Size(charuco_pattern.squares_x(), charuco_pattern.squares_y()),
      charuco_pattern.square_length(), charuco_pattern.marker_length(),
      dictionary);
}

absl::StatusOr<CharucoDetection> FindCharucoMarker(
    const Image<Rgb8u>& image,
    const intrinsic_proto::perception::v1::CharucoPattern& charuco_pattern,
    const std::optional<CameraParams>& camera_params) {
  INTR_ASSIGN_OR_RETURN(cv::Ptr<cv::aruco::CharucoBoard> charucoboard,
                        CreateCharucoBoard(charuco_pattern));

  cv::Mat intrinsic_params;
  cv::Mat distortion_params;
  if (camera_params) {
    intrinsic_params =
        ToCvIntrinsicParams(camera_params.value().intrinsic_params);
    distortion_params = ToCvDistortionParams(
        camera_params.value().distortion_params.value_or(DistortionParams()));
  }
  cv::aruco::CharucoParameters params;
  params.cameraMatrix = intrinsic_params;
  params.distCoeffs = distortion_params;
  cv::aruco::DetectorParameters detector_params;
  detector_params.minMarkerPerimeterRate =
      kMinMarkerDistancePixels * 4.0f / std::max(image.cols(), image.rows());
  detector_params.minSideLengthCanonicalImg = 0;

  cv::aruco::CharucoDetector detector(*charucoboard, params, detector_params);
  std::vector<int> charuco_ids;
  std::vector<cv::Point2f> charuco_corners;
  std::vector<int> marker_ids;
  std::vector<std::vector<cv::Point2f>> marker_corners;
  cv::Mat gray_image;
  cv::cvtColor(UnsafeConstCastCvMat(image), gray_image, cv::COLOR_BGR2GRAY);
  detector.detectBoard(gray_image, charuco_corners, charuco_ids, marker_corners,
                       marker_ids);

  INTR_RETURN_IF_ERROR(CheckForDuplicateMarkers(marker_ids));

  if (charuco_ids.empty()) {
    return absl::NotFoundError(
        "No chessboard corners of a ChArUco pattern were found in the image.");
  }

  LOG(INFO) << "Found " << marker_ids.size() << " marker corners in image.";
  LOG(INFO) << "Found " << charuco_ids.size() << " charuco corners in image.";
  return CharucoDetection{.aruco_ids = marker_ids,
                          .aruco_corners = marker_corners,
                          .chessboard_ids = charuco_ids,
                          .chessboard_corners = charuco_corners};
}

absl::StatusOr<Image<Rgb8u>> DrawCharucoBoard(const Image<Rgb8u>& image,
                                              const CharucoDetection& detection,
                                              const Rgb8u::PixelType& color) {
  if (detection.chessboard_ids.empty()) {
    return absl::InvalidArgumentError("ChArUco ids are empty.");
  }
  if (detection.chessboard_corners.size() != detection.chessboard_ids.size()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "charuco_corners and charuco_ids must have same size, "
        "but are %d and %d, respectively.",
        detection.chessboard_corners.size(), detection.chessboard_ids.size()));
  }
  if (detection.aruco_corners.empty()) {
    return absl::InvalidArgumentError("No ArUco marker ids were provided.");
  }
  const cv::Scalar cvx_color(color.x(), color.y(), color.z());
  cv::aruco::drawDetectedMarkers(UnsafeConstCastCvMat(image),
                                 detection.aruco_corners, {}, cvx_color);
  cv::aruco::drawDetectedCornersCharuco(UnsafeConstCastCvMat(image),
                                        detection.chessboard_corners,
                                        detection.chessboard_ids, cvx_color);
  return image;
}

absl::StatusOr<Image<Rgb8u>> FindAndDrawCharucoBoard(
    const Image<Rgb8u>& image,
    const intrinsic_proto::perception::v1::CharucoPattern& charuco_pattern,
    const std::optional<CameraParams>& camera_params,
    const Rgb8u::PixelType& color) {
  INTR_ASSIGN_OR_RETURN(
      const CharucoDetection charuco_detection,
      FindCharucoMarker(image, charuco_pattern, camera_params));
  return DrawCharucoBoard(image, charuco_detection, color);
}

absl::StatusOr<Mesh> CreateCharucoMesh(
    const intrinsic_proto::perception::v1::CharucoPattern& pattern,
    const CreateCharucoMeshOptions& options) {
  // Create charuco texture.
  INTR_ASSIGN_OR_RETURN(const auto charuco_board, CreateCharucoBoard(pattern));
  cv::Mat texture_cv;
  charuco_board->generateImage(
      cv::Size(options.texture_size, options.texture_size), texture_cv);
  Image<Gray8u> texture = MoveToImage<Gray8u>(std::move(texture_cv));

  std::vector<Vector3f> vertices;
  std::vector<Vector3f> normals;
  std::vector<Vector2f> texels;
  std::vector<uint32_t> indices;
  const Vector3f normal(0, 0, 1);
  // Helper lambda to add a quad as two triangles.
  const auto add_quad = [&](const Vector3f& xmin, const Vector3f& xmax,
                            const Vector2f& tex_min, const Vector2f& tex_max) {
    indices.push_back(vertices.size());
    indices.push_back(vertices.size() + 1);
    indices.push_back(vertices.size() + 2);
    indices.push_back(vertices.size() + 2);
    indices.push_back(vertices.size() + 1);
    indices.push_back(vertices.size() + 3);

    vertices.push_back(Vector3f(xmin(0), xmin(1), xmin(2)));
    vertices.push_back(Vector3f(xmax(0), xmin(1), xmin(2)));
    vertices.push_back(Vector3f(xmin(0), xmax(1), xmin(2)));
    vertices.push_back(Vector3f(xmax(0), xmax(1), xmin(2)));
    texels.push_back(Vector2f(tex_min(0), tex_min(1)));
    texels.push_back(Vector2f(tex_max(0), tex_min(1)));
    texels.push_back(Vector2f(tex_min(0), tex_max(1)));
    texels.push_back(Vector2f(tex_max(0), tex_max(1)));
    for (int i = 0; i < 4; ++i) {
      normals.push_back(normal);
    }
  };

  // Main board.
  INTR_RET_CHECK_GT(pattern.squares_x(), 0);
  INTR_RET_CHECK_GT(pattern.squares_y(), 0);
  const Vector3f origin(-pattern.squares_x() * pattern.square_length() / 2.0f,
                        -pattern.squares_y() * pattern.square_length() / 2.0f,
                        0.0f);
  if (pattern.squares_x() > pattern.squares_y()) {
    add_quad(
        origin,
        origin + Vector3f(pattern.square_length() * pattern.squares_x(),
                          pattern.square_length() * pattern.squares_y(), 0),
        Vector2f(0.0f, 0.5f + 0.5f / pattern.squares_x() * pattern.squares_y()),
        Vector2f(1.0f,
                 0.5f - 0.5f / pattern.squares_x() * pattern.squares_y()));
  } else {
    add_quad(
        origin,
        origin + Vector3f(pattern.square_length() * pattern.squares_x(),
                          pattern.square_length() * pattern.squares_y(), 0),
        Vector2f(0.5f - 0.5f / pattern.squares_y() * pattern.squares_x(), 1.0f),
        Vector2f(0.5f + 0.5f / pattern.squares_y() * pattern.squares_x(),
                 0.0f));
  }

  const Vector2f texel_white = pattern.squares_x() != pattern.squares_y()
                                   ? Vector2f::Zero()
                                   : Vector2f(1.5f / pattern.squares_x(), 0.0f);
  if (options.margin > 0) {
    const float margin = options.margin;
    const float board_size_x = pattern.squares_x() * pattern.square_length();
    const float board_size_y = pattern.squares_y() * pattern.square_length();
    add_quad(origin - Vector3f(margin, margin, 0.0f), origin, texel_white,
             texel_white);
    add_quad(origin - Vector3f(0.0f, margin, 0.0f),
             origin + Vector3f(board_size_x, 0.0f, 0.0f), texel_white,
             texel_white);
    add_quad(origin + Vector3f(board_size_x, -margin, 0.0f),
             origin + Vector3f(board_size_x + margin, 0.0f, 0.0f), texel_white,
             texel_white);
    add_quad(origin + Vector3f(-margin, 0.0f, 0.0f),
             origin + Vector3f(0.0f, board_size_y, 0.0f), texel_white,
             texel_white);
    add_quad(origin + Vector3f(board_size_x, 0.0f, 0.0f),
             origin + Vector3f(board_size_x + margin, board_size_y, 0.0f),
             texel_white, texel_white);

    add_quad(origin + Vector3f(-margin, board_size_y, 0.0f),
             origin + Vector3f(0.0f, board_size_y + margin, 0.0f), texel_white,
             texel_white);
    add_quad(origin + Vector3f(0, board_size_y, 0.0f),
             origin + Vector3f(board_size_x, board_size_y + margin, 0.0f),
             texel_white, texel_white);
    add_quad(
        origin + Vector3f(board_size_x, board_size_y, 0.0f),
        origin + Vector3f(board_size_x + margin, board_size_y + margin, 0.0f),
        texel_white, texel_white);
  }
  return Mesh(vertices, normals, texels, indices,
              ConvertGray8uToRgb8uImage(texture), MeshType::TRIANGLES);
}

}  // namespace intrinsic::perception
