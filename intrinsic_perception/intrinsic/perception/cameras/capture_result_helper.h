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

#ifndef INTRINSIC_PERCEPTION_PUBLIC_CAPTURE_RESULT_HELPER_H_
#define INTRINSIC_PERCEPTION_PUBLIC_CAPTURE_RESULT_HELPER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/perception/core/undistortion.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::perception {

absl::StatusOr<CaptureResult> PostProcessCaptureResult(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id,
    UndistortionBySensorId& undistortion_by_sensor_id);

// For each sensor image in the capture result: crops the image, then resizes
// the image. Resizing uses linear interpolation for rgb8 and gray images,
// nearest neighbor for depth, normal, and point images. Camera parameters are
// updated accordingly. Optional `fill_value` sets a value for pixels outside of
// the image. If `fill_value` is not set, crop_region must be contained in the
// image rectangle. The double value of `fill_value` is converted to the pixel
// type of the image by static_cast and repeating the same value for
// multi-channel images.
absl::StatusOr<SensorImage> CropAndResize(
    const SensorImage& sensor_image, const PostProcessing& post_processing,
    std::optional<double> fill_value = std::nullopt);

absl::StatusOr<CaptureResult> CropAndResize(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id);

// Un-distorts all of the sensor images contained in the capture result while
// automatically synchronizing the passed undistortion map to the camera
// parameters contained in the sensor images, i.e. undistortion helpers are
// deleted or created if necessary.
absl::StatusOr<CaptureResult> Undistort(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id,
    UndistortionBySensorId& undistortion_by_sensor_id);

absl::StatusOr<CaptureResult> Undistort(
    CaptureResult&& capture_result,
    UndistortionBySensorId& undistortion_by_sensor_id);

absl::StatusOr<SensorImage> Undistort(
    SensorImage&& sensor_image,
    UndistortionBySensorId& undistortion_by_sensor_id);

// Un-distorts the sensor image, if distortion parameters exist.
absl::StatusOr<SensorImage> Undistort(SensorImage&& sensor_image,
                                      const Undistortion& undistortion);

// Returns true if any of the sensor images in the capture result can be
// undistorted.
bool CanBeUndistorted(const CaptureResult& capture_result);

absl::StatusOr<CaptureResult> ApplyCameraParams(
    CaptureResult&& capture_result,
    const CameraParamsBySensorId& camera_params_by_sensor_id);

absl::StatusOr<CaptureResult> ApplyCameraTsSensor(
    CaptureResult&& capture_result,
    const absl::flat_hash_map<int64_t, Pose3d>& camera_t_sensor_by_sensor_id);

// Calls the provided functor with the Image<T> held by SensorImage. Motivated
// by std::visit.
template <typename Visitor, typename SensorImageProxy>
auto Visit(Visitor&& visitor, SensorImageProxy&& sensor_image)
    -> decltype(visitor(
        std::forward<SensorImageProxy>(sensor_image).rgb8u().value()))
  requires(std::is_same_v<std::remove_cvref_t<SensorImageProxy>, SensorImage>)
{
  if (sensor_image.rgb8u().has_value())
    return visitor(
        std::forward<SensorImageProxy>(sensor_image).rgb8u().value());
  if (sensor_image.gray8u().has_value())
    return visitor(
        std::forward<SensorImageProxy>(sensor_image).gray8u().value());
  if (sensor_image.depth32f().has_value())
    return visitor(
        std::forward<SensorImageProxy>(sensor_image).depth32f().value());
  if (sensor_image.gray32f().has_value())
    return visitor(
        std::forward<SensorImageProxy>(sensor_image).gray32f().value());
  if (sensor_image.point32f().has_value())
    return visitor(
        std::forward<SensorImageProxy>(sensor_image).point32f().value());
  return visitor(
      std::forward<SensorImageProxy>(sensor_image).normal32f().value());
}

// Returns a reference to the image getter of SensorImage corresponding to they
// image type ImageTrait.
template <typename ImageTrait>
const std::optional<Image<ImageTrait>>& (
    SensorImage::* absl_nonnull(ImageGetterOfType()))() const& {
  if constexpr (std::is_same_v<Gray8u, ImageTrait>) {
    return &SensorImage::gray8u;
  } else if constexpr (std::is_same_v<Rgb8u, ImageTrait>) {
    return &SensorImage::rgb8u;
  } else if constexpr (std::is_same_v<Gray32f, ImageTrait>) {
    return &SensorImage::gray32f;
  } else if constexpr (std::is_same_v<Depth32f, ImageTrait>) {
    return &SensorImage::depth32f;
  } else if constexpr (std::is_same_v<Point32f, ImageTrait>) {
    return &SensorImage::point32f;
  } else if constexpr (std::is_same_v<Normal32f, ImageTrait>) {
    return &SensorImage::normal32f;
  } else {
    static_assert(false, "Unsupported image trait.");
  }
}

// Returns the index of the first sensor image in the capture result that
// contains an image of types ImageTraits, queried in the order of the types.
template <typename... ImageTraits>
absl::StatusOr<size_t> GetFirstSensorImageIndexOfType(
    const CaptureResult& capture_result) {
  const std::vector<SensorImage>& sensor_images = capture_result.sensor_images;
  absl::StatusOr<size_t> result;
  const auto GetIndex = [&sensor_images, &result]<typename ImageTrait>() {
    if (result.ok()) return;
    const auto fn = ImageGetterOfType<ImageTrait>();
    const auto it =
        std::find_if(sensor_images.begin(), sensor_images.end(),
                     [fn](const SensorImage& sensor_image) {
                       return std::invoke(fn, sensor_image).has_value();
                     });
    if (it != sensor_images.end()) {
      result = std::distance(sensor_images.begin(), it);
    }
  };
  (GetIndex.template operator()<ImageTraits>(), ...);
  if (result.ok()) return result;
  return absl::InvalidArgumentError("No corresponding image found.");
}

// Returns a pointer to the first sensor image in the capture result that
// contains an image of types ImageTraits, queried in the order of the types.
template <typename... ImageTraits>
absl::StatusOr<const SensorImage* absl_nonnull> GetFirstSensorImageOfType(
    const CaptureResult& capture_result) {
  INTR_ASSIGN_OR_RETURN(
      const size_t index,
      GetFirstSensorImageIndexOfType<ImageTraits...>(capture_result));
  return &capture_result.sensor_images[index];
}

// Returns the first image of type ImageTrait in the capture result.
template <typename ImageTrait>
absl::StatusOr<Image<ImageTrait>> GetFirstImageOfType(
    const CaptureResult& capture_result) {
  INTR_ASSIGN_OR_RETURN(
      const size_t index,
      GetFirstSensorImageIndexOfType<ImageTrait>(capture_result));
  const std::optional<Image<ImageTrait>>& image = std::invoke(
      ImageGetterOfType<ImageTrait>(), capture_result.sensor_images[index]);
  INTR_RET_CHECK(image.has_value());
  return image.value();
}

// Extracts or converts the image to type Rgb8u from the sensor image.
absl::StatusOr<Image<Rgb8u>> GetImageAsRgb8u(const SensorImage& sensor_image);

// Extracts or converts the image to type Gray8u from the sensor image.
absl::StatusOr<Image<Gray8u>> GetImageAsGray8u(const SensorImage& sensor_image);

// Returns a capture result with images maximally cropped to the ROI.
// Arguments:
//   capture_result: The capture result to adjust.
//   dimensions: The dimensions of the ROI.
//   camera_t_box: Pose of the center of the ROI in the reference frame.
//   ensure_same_size: If true, the crop rectangle is enlarged to the dimensions
//     of the ROI which may require padding.
absl::StatusOr<CaptureResult> AdjustCaptureForROI(
    const CaptureResult& capture_result, const Vector3f& dimensions,
    const Isometry3f& camera_t_box, bool ensure_same_size);

absl::Status SensorImagesHaveSameTypeAndSize(
    const std::vector<const SensorImage*>& images);

// Merges individual capture results into the frame of the first camera.
CaptureResult MergeCaptureResults(std::vector<CaptureResult> capture_results,
                                  const std::vector<Pose3d>& world_ts_camera);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_PUBLIC_CAPTURE_RESULT_HELPER_H_
