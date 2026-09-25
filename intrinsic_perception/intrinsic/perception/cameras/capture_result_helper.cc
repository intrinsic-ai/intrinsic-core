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

#include "intrinsic/perception/cameras/capture_result_helper.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/conversion.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/operators.h"
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/perception/core/range_tools.h"
#include "intrinsic/perception/core/rectangle.h"
#include "intrinsic/perception/core/undistortion.h"
#include "intrinsic/perception/proto_conversion/eigen_conversions.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgproc/imgproc.hpp"

namespace intrinsic::perception {

namespace {

template <class TypeTrait, class Scalar>
std::optional<typename TypeTrait::PixelType> ConvertPadding(
    std::optional<Scalar> padding)
  requires std::same_as<typename TypeTrait::PixelType,
                        typename TypeTrait::ScalarType>
{
  if (padding.has_value()) {
    return static_cast<typename TypeTrait::ScalarType>(padding.value());
  }
  return std::nullopt;
}

template <class TypeTrait, class Scalar>
std::optional<typename TypeTrait::PixelType> ConvertPadding(
    std::optional<Scalar> padding) {
  using PixelType = typename TypeTrait::PixelType;
  if (padding.has_value()) {
    return PixelType::Constant(
        static_cast<typename TypeTrait::ScalarType>(padding.value()));
  }
  return std::nullopt;
}

}  // namespace

absl::StatusOr<CaptureResult> PostProcessCaptureResult(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id,
    UndistortionBySensorId& undistortion_by_sensor_id) {
  INTR_ASSIGN_OR_RETURN(capture_result, Undistort(std::move(capture_result),
                                                  post_processing_by_sensor_id,
                                                  undistortion_by_sensor_id));
  INTR_ASSIGN_OR_RETURN(
      capture_result,
      CropAndResize(std::move(capture_result), post_processing_by_sensor_id));
  return capture_result;
}

absl::StatusOr<SensorImage> CropAndResize(const SensorImage& sensor_image,
                                          const PostProcessing& post_processing,
                                          std::optional<double> fill_value) {
  if (sensor_image.IsEmpty()) return sensor_image;

  const std::optional<Rectangle>& crop_region = post_processing.crop_region;
  const std::optional<int32_t>& cols = post_processing.cols;
  const std::optional<int32_t>& rows = post_processing.rows;
  if (!crop_region.has_value() && !cols.has_value() && !rows.has_value())
    return sensor_image;

  std::optional<CameraParams> camera_params = sensor_image.camera_params();
  if (camera_params.has_value()) {
    if (crop_region.has_value()) {
      INTR_ASSIGN_OR_RETURN(camera_params->intrinsic_params,
                            Crop(camera_params->intrinsic_params,
                                 crop_region.value(), fill_value.has_value()));
    }
    INTR_ASSIGN_OR_RETURN(const Dimensions resized_dimensions,
                          Resize(camera_params->Dimensions(), cols, rows));
    camera_params->intrinsic_params =
        Resize(camera_params->intrinsic_params, resized_dimensions);
  }

  if (sensor_image.rgb8u().has_value()) {
    INTR_ASSIGN_OR_RETURN(
        Image<Rgb8u> image,
        CropAndResize(sensor_image.rgb8u().value(), post_processing,
                      cv::INTER_AREA, ConvertPadding<Rgb8u>(fill_value)));
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.gray8u().has_value()) {
    INTR_ASSIGN_OR_RETURN(
        Image<Gray8u> image,
        CropAndResize(sensor_image.gray8u().value(), post_processing,
                      cv::INTER_AREA, ConvertPadding<Gray8u>(fill_value)));
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.gray32f().has_value()) {
    INTR_ASSIGN_OR_RETURN(
        Image<Gray32f> image,
        CropAndResize(sensor_image.gray32f().value(), post_processing,
                      cv::INTER_AREA, ConvertPadding<Gray32f>(fill_value)));
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.depth32f().has_value()) {
    INTR_ASSIGN_OR_RETURN(
        Image<Depth32f> image,
        CropAndResize(sensor_image.depth32f().value(), post_processing,
                      cv::INTER_NEAREST, ConvertPadding<Depth32f>(fill_value)));
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.point32f().has_value()) {
    INTR_ASSIGN_OR_RETURN(
        Image<Point32f> image,
        CropAndResize(sensor_image.point32f().value(), post_processing,
                      cv::INTER_NEAREST, ConvertPadding<Point32f>(fill_value)));
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.normal32f().has_value()) {
    INTR_ASSIGN_OR_RETURN(Image<Normal32f> image,
                          CropAndResize(sensor_image.normal32f().value(),
                                        post_processing, cv::INTER_NEAREST,
                                        ConvertPadding<Normal32f>(fill_value)));
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else {
    return absl::InvalidArgumentError("No image to export!");
  }
}

absl::StatusOr<CaptureResult> CropAndResize(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id) {
  for (SensorImage& sensor_image : capture_result.sensor_images) {
    const auto it = post_processing_by_sensor_id.find(sensor_image.sensor_id());
    if (it != post_processing_by_sensor_id.end()) {
      INTR_ASSIGN_OR_RETURN(sensor_image,
                            CropAndResize(sensor_image, it->second));
    }
  }
  return capture_result;
}

absl::StatusOr<CaptureResult> Undistort(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id,
    UndistortionBySensorId& undistortion_by_sensor_id) {
  for (SensorImage& sensor_image : capture_result.sensor_images) {
    const auto it = post_processing_by_sensor_id.find(sensor_image.sensor_id());
    // Undistortion is only skipped if explicitly requested.
    if (it == post_processing_by_sensor_id.end() ||
        !it->second.skip_undistortion) {
      INTR_ASSIGN_OR_RETURN(sensor_image, Undistort(std::move(sensor_image),
                                                    undistortion_by_sensor_id));
    }
  }
  return capture_result;
}

absl::StatusOr<CaptureResult> Undistort(
    CaptureResult&& capture_result,
    UndistortionBySensorId& undistortion_by_sensor_id) {
  for (SensorImage& sensor_image : capture_result.sensor_images) {
    INTR_ASSIGN_OR_RETURN(sensor_image, Undistort(std::move(sensor_image),
                                                  undistortion_by_sensor_id));
  }
  return capture_result;
}

absl::StatusOr<SensorImage> Undistort(
    SensorImage&& sensor_image,
    UndistortionBySensorId& undistortion_by_sensor_id) {
  if (!sensor_image.camera_params().has_value() ||
      !sensor_image.camera_params()->distortion_params.has_value()) {
    return sensor_image;
  }
  const CameraParams& camera_params = *sensor_image.camera_params();
  auto it = undistortion_by_sensor_id.find(sensor_image.sensor_id());
  if (it == undistortion_by_sensor_id.end() ||
      !it->second.camera_params().has_value() ||
      !CameraParamsNear(*it->second.camera_params(), camera_params, 0.0).ok()) {
    bool _;
    std::tie(it, _) = undistortion_by_sensor_id.insert_or_assign(
        sensor_image.sensor_id(), Undistortion(camera_params));
  }
  return Undistort(std::move(sensor_image), it->second);
}

absl::StatusOr<SensorImage> Undistort(SensorImage&& sensor_image,
                                      const Undistortion& undistortion) {
  if (sensor_image.camera_params().has_value() !=
          undistortion.camera_params().has_value() ||
      (sensor_image.camera_params().has_value() &&
       undistortion.camera_params().has_value() &&
       !CameraParamsNear(sensor_image.camera_params().value(),
                         undistortion.camera_params().value(), 0.0)
            .ok())) {
    return absl::InvalidArgumentError(
        "Camera params of sensor image and undistortion don't match");
  }

  // TODO b/380194970 - Add support for point cloud undistortion.
  if (sensor_image.point32f().has_value()) {
    LOG(WARNING) << "Point sensor image undistortion unimplemented";
    return sensor_image;
  } else if (sensor_image.normal32f().has_value()) {
    LOG(WARNING) << "Normal sensor image undistortion unimplemented";
    return sensor_image;
  }

  std::optional<CameraParams> camera_params;
  if (sensor_image.camera_params().has_value()) {
    camera_params =
        CameraParams(sensor_image.camera_params()->intrinsic_params);
  }

  // We only undistort images for RGB/gray cameras.
  if (sensor_image.rgb8u().has_value()) {
    Image<Rgb8u> image = undistortion(sensor_image.rgb8u().value());
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.gray8u().has_value()) {
    Image<Gray8u> image = undistortion(sensor_image.gray8u().value());
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.gray32f().has_value()) {
    Image<Gray32f> image = undistortion(sensor_image.gray32f().value());
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else if (sensor_image.depth32f().has_value()) {
    Image<Depth32f> image = undistortion(sensor_image.depth32f().value());
    return SensorImage(sensor_image.sensor_id(),
                       sensor_image.acquisition_time(), camera_params,
                       sensor_image.camera_t_sensor(), std::move(image));
  } else {
    return absl::InvalidArgumentError("No image to export!");
  }
}

bool CanBeUndistorted(const CaptureResult& capture_result) {
  return std::any_of(
      capture_result.sensor_images.begin(), capture_result.sensor_images.end(),
      [](const SensorImage& sensor_image) {
        // TODO b/380194970 - Remove point cloud check, once point cloud
        // undistortion is implemented.
        return !sensor_image.point32f().has_value() &&
               !sensor_image.normal32f().has_value() &&
               sensor_image.camera_params().has_value() &&
               sensor_image.camera_params()->distortion_params.has_value() &&
               !DistortionParamsNear(
                    sensor_image.camera_params()->distortion_params.value(), {},
                    0.0)
                    .ok();
      });
}

absl::StatusOr<CaptureResult> ApplyCameraParams(
    CaptureResult&& capture_result,
    const CameraParamsBySensorId& camera_params_by_sensor_id) {
  for (SensorImage& sensor_image : capture_result.sensor_images) {
    std::optional<CameraParams> camera_params =
        OptionalCopyAt(camera_params_by_sensor_id, sensor_image.sensor_id());
    if (!camera_params.has_value()) {
      continue;
    }
    const Dimensions sensor_image_dimensions = sensor_image.Dimensions();
    if (sensor_image_dimensions != camera_params->Dimensions()) {
      camera_params = Resize(*camera_params, sensor_image_dimensions);
    }
    sensor_image = SensorImageBuilder()
                       .From(std::move(sensor_image))
                       .SetCameraParams(camera_params)
                       .Build();
  }
  return capture_result;
}

absl::StatusOr<CaptureResult> ApplyCameraTsSensor(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, Pose3d>& camera_t_sensor_by_sensor_id) {
  for (SensorImage& sensor_image : capture_result.sensor_images) {
    const std::optional<Pose3d> camera_t_sensor =
        OptionalCopyAt(camera_t_sensor_by_sensor_id, sensor_image.sensor_id());
    if (!camera_t_sensor.has_value()) {
      continue;
    }
    sensor_image = SensorImageBuilder()
                       .From(std::move(sensor_image))
                       .SetCameraTSensor(camera_t_sensor)
                       .Build();
  }
  return capture_result;
}

absl::StatusOr<Image<Rgb8u>> GetImageAsRgb8u(const SensorImage& sensor_image) {
  if (sensor_image.rgb8u().has_value()) {
    return sensor_image.rgb8u().value();
  } else if (sensor_image.gray8u().has_value()) {
    return ConvertGray8uToRgb8uImage(sensor_image.gray8u().value());
  } else if (sensor_image.gray32f().has_value()) {
    return ConvertGray32fToRgb8uImage(sensor_image.gray32f().value());
  }
  return absl::InvalidArgumentError("Image type cannot be converted to Rgb8u!");
}

absl::StatusOr<Image<Gray8u>> GetImageAsGray8u(
    const SensorImage& sensor_image) {
  if (sensor_image.gray8u().has_value()) {
    return sensor_image.gray8u().value();
  } else if (sensor_image.rgb8u().has_value()) {
    return ConvertRgb8uToGray8u(sensor_image.rgb8u().value());
  } else if (sensor_image.gray32f().has_value()) {
    return ConvertGray32fToGray8uImage(sensor_image.gray32f().value());
  }
  return absl::InvalidArgumentError(
      "Image type cannot be converted to Gray8u!");
}

absl::StatusOr<CaptureResult> AdjustCaptureForROI(
    const CaptureResult& capture_result, const Vector3f& dimensions,
    const Isometry3f& camera_t_box, bool ensure_same_size) {
  std::vector<Vector3f> roi_points;
  for (int i : {-1, 1}) {
    for (int j : {-1, 1}) {
      for (int k : {-1, 1}) {
        roi_points.push_back(camera_t_box * Vector3f(dimensions.x() * i / 2,
                                                     dimensions.y() * j / 2,
                                                     dimensions.z() * k / 2));
      }
    }
  }
  std::vector<Rectangle> roi_rectangles;
  roi_rectangles.reserve(capture_result.sensor_images.size());
  for (const SensorImage& sensor_image : capture_result.sensor_images) {
    INTR_RET_CHECK(sensor_image.camera_params().has_value());
    INTR_RET_CHECK(sensor_image.camera_t_sensor().has_value());
    std::vector<Vector2f> points = ProjectPointsToImage(
        sensor_image.camera_params()->intrinsic_params,
        ToEigen(*sensor_image.camera_t_sensor()).inverse().cast<float>(),
        roi_points);
    roi_rectangles.push_back(Intersected(CreateBoundingRectangle(points),
                                         Rectangle{sensor_image.Dimensions()}));
  }
  if (ensure_same_size) {
    Dimensions max_dimensions;
    for (const auto& rect : roi_rectangles) {
      max_dimensions = Union(max_dimensions, rect.dimensions);
    }
    for (auto& rect : roi_rectangles) {
      auto deficit = max_dimensions - rect.dimensions;
      rect.origin.col -= deficit.cols / 2;
      rect.origin.row -= deficit.rows / 2;
      rect.dimensions = max_dimensions;
    }
  }
  std::vector<SensorImage> sensor_images;
  sensor_images.reserve(capture_result.sensor_images.size());
  for (int i = 0; i < capture_result.sensor_images.size(); ++i) {
    INTR_ASSIGN_OR_RETURN(
        SensorImage cropped_image,
        CropAndResize(capture_result.sensor_images[i],
                      {.crop_region = roi_rectangles[i]}, /*fill_value=*/0.0));
    sensor_images.push_back(std::move(cropped_image));
  }
  return CaptureResult{.capture_at = capture_result.capture_at,
                       .sensor_images = std::move(sensor_images),
                       .capture_duration = capture_result.capture_duration};
}

absl::Status SensorImagesHaveSameTypeAndSize(
    const std::vector<const SensorImage*>& images) {
  if (images.empty()) return absl::OkStatus();
  for (const SensorImage* image : images) {
    INTR_RET_CHECK_NE(image, nullptr);
  }
  const bool gray_scale = images[0]->gray8u().has_value();
  const bool rgb = images[0]->rgb8u().has_value();
  const bool normal = images[0]->normal32f().has_value();
  const bool depth = images[0]->depth32f().has_value();
  const bool points = images[0]->point32f().has_value();
  const Dimensions dimensions = Visit(
      [](const auto& image) { return image.dimensions(); }, *images.front());
  for (const SensorImage* image : images) {
    if (image->gray8u().has_value() != gray_scale ||
        image->rgb8u().has_value() != rgb ||
        image->normal32f().has_value() != normal ||
        image->depth32f().has_value() != depth ||
        image->point32f().has_value() != points) {
      return absl::InvalidArgumentError(
          "Unsupported combination of images passed.");
    }
    if (const Dimensions current_dimensions =
            Visit([](const auto& image) { return image.dimensions(); }, *image);
        current_dimensions != dimensions) {
      return absl::InvalidArgumentError("Image size mismatch.");
    }
  }
  return absl::OkStatus();
}

CaptureResult MergeCaptureResults(std::vector<CaptureResult> capture_results,
                                  const std::vector<Pose3d>& world_ts_camera) {
  if (capture_results.empty()) {
    return CaptureResult{};
  }
  if (capture_results.size() == 1) {
    return std::move(capture_results[0]);
  }

  // Merge individual capture results.
  const Pose3d cam_0_t_world = world_ts_camera[0].inverse();
  CaptureResult merged_capture_results;
  merged_capture_results.sensor_images.reserve(capture_results.size());
  int sensor_id = 0;
  for (int i = 0; i < capture_results.size(); ++i) {
    CaptureResult& capture_result = capture_results[i];
    const Pose3d cam_0_t_cam_i = cam_0_t_world * world_ts_camera[i];
    for (SensorImage& sensor_image : capture_result.sensor_images) {
      const Pose3d cam_i_t_sensor = sensor_image.camera_t_sensor().has_value()
                                        ? sensor_image.camera_t_sensor().value()
                                        : Pose3d::Identity();
      SensorImage updated_sensor_image =
          SensorImageBuilder()
              .From(std::move(sensor_image))
              .SetSensorId(sensor_id++)
              .SetCameraTSensor(cam_0_t_cam_i * cam_i_t_sensor)
              .Build();
      merged_capture_results.sensor_images.push_back(
          std::move(updated_sensor_image));
    }
  }
  return merged_capture_results;
}

}  // namespace intrinsic::perception
