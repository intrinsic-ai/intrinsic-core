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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_ARAVIS_ARAVIS_UTILS_H_
#define INTRINSIC_PERCEPTION_CAMERAS_ARAVIS_ARAVIS_UTILS_H_

#include <arv.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/aravis/aravis_glib_utils.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/version.h"

namespace intrinsic::perception {

class BufferHelper {
 public:
  // Default constructor without chunk parser support.
  BufferHelper() = default;
  // Tries to enable chunk parsing with the desired features.
  explicit BufferHelper(ArvCamera* absl_nonnull camera,
                        const std::vector<std::string>& desired_features);

  // Retrieves the dimensions of the chunk buffer.
  //
  // The Aravis buffer has to be mutable because the signature of the underlying
  // Aravis methods takes a mutable pointer. Side effects are not expected.
  absl::StatusOr<Dimensions> GetDimensions(
      ArvBuffer* absl_nonnull buffer, guint part_id,
      Dimensions fallback_dimensions) const;

  // Retrieves the pixel format of the chunk buffer.
  //
  // The Aravis buffer has to be mutable because the signature of the underlying
  // Aravis methods takes a mutable pointer. Side effects are not expected.
  absl::StatusOr<ArvPixelFormat> GetPixelFormat(
      ArvBuffer* absl_nonnull buffer, guint part_id,
      ArvPixelFormat fallback_pixel_format) const;

  // Retrieves the exposure time of the chunk buffer.
  //
  // The Aravis buffer has to be mutable because the signature of the underlying
  // Aravis methods takes a mutable pointer. Side effects are not expected.
  std::optional<absl::Duration> GetExposureTime(
      ArvBuffer* absl_nonnull buffer) const;

  // Copies an Aravis buffer to a capture result containing sensor images.
  //
  // The Aravis buffer has to be mutable because the signature of the underlying
  // Aravis methods takes a mutable pointer. Side effects are not expected.
  absl::StatusOr<CaptureResult> ToCaptureResult(
      ArvBuffer* absl_nonnull buffer,
      const CameraParamsBySensorId& camera_params_by_sensor_id,
      const absl::flat_hash_map<int64_t, Pose>& camera_t_sensor_by_sensor_id,
      const absl::flat_hash_map<int64_t, Dimensions>&
          fallback_dimensions_by_sensor_id,
      const absl::flat_hash_map<int64_t, ArvPixelFormat>&
          fallback_pixel_format_by_sensor_id,
      double distance_scale);

 private:
  // Retrieves the supported chunk features. If `desired_features` is not empty,
  // only the intersection of `desired_features` and the supported features is
  // returned.
  std::vector<std::string> GetSupportedFeatures(
      const std::vector<std::string>& desired_features) const;

  // Enables the specified chunk features.
  absl::Status EnableFeatures(const std::vector<std::string>& features,
                              ArvCamera* absl_nonnull camera);

  // Retrieves the typed value of the chunk buffer.
  //
  // The Aravis buffer has to be mutable because the signature of the underlying
  // Aravis methods takes a mutable pointer. Side effects are not expected.
  template <typename T>
  absl::StatusOr<T> GetChunkValue(std::string_view feature_name,
                                  ArvBuffer* absl_nonnull chunk_buffer) const;

  GObjectPtr<ArvChunkParser> parser_;
  absl::flat_hash_set<std::string> supported_features_;
};

// Extracts the bits per pixel for a specific pixel format.
absl::StatusOr<int> BitsPerPixel(ArvPixelFormat pixel_format);

// Extracts the number of channels for a specific pixel format.
absl::StatusOr<int> NumChannels(ArvPixelFormat pixel_format);

// Extracts the OpenCV type for a specific pixel format.
absl::StatusOr<int> CvType(ArvPixelFormat pixel_format);

// Turns GError into a status. The function releases the GError if it is not
// null and sets the input value back to a nullptr.
absl::Status ValidateAndNullArvError(
    GError*& error, std::string_view error_message = {},
    intrinsic::SourceLocation location = intrinsic::SourceLocation::current());

// Reads all supported enum values of the specified feature and returns them as
// a vector of strings.
//
// The Aravis camera has to be mutable because the signature of the underlying
// Aravis methods takes a mutable pointer. Side effects are not expected.
absl::StatusOr<std::vector<std::string>> EnumerateParameters(
    std::string_view feature, ArvCamera* absl_nonnull camera);

// Retrieves the node for a certain feature.
//
// The Aravis camera has to be mutable because the signature of the underlying
// Aravis methods takes a mutable pointer. Side effects are not expected.
absl::StatusOr<ArvGcFeatureNode*> GetFeatureNode(
    std::string_view name, ArvCamera* absl_nonnull camera);

// Retrieves the access mode for a certain feature.
//
// The Aravis camera has to be mutable because the signature of the underlying
// Aravis methods takes a mutable pointer. Side effects are not expected.
absl::StatusOr<ArvGcAccessMode> GetAccessMode(std::string_view name,
                                              ArvCamera* absl_nonnull camera);

template <typename T>
absl::StatusOr<std::vector<T>> ReadRegister(std::string_view name,
                                            ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  GError* error = nullptr;

  ArvGcNode* gc_node =
      arv_device_get_feature(arv_camera_get_device(camera), name.data());
  if (gc_node == nullptr || !ARV_IS_GC_REGISTER(gc_node)) {
    return intrinsic::FailedPreconditionErrorBuilder()
           << "Failed to get register for feature: " << name;
  }

  ArvGcRegister* gc_register = reinterpret_cast<ArvGcRegister*>(gc_node);
  const guint64 length = arv_gc_register_get_length(gc_register, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  INTR_RET_CHECK_EQ(length % sizeof(T), 0);
  std::vector<T> values(length / sizeof(T));
  arv_gc_register_get(gc_register, values.data(), length, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  return values;
}

// Updates the Aravis device list and returns the IDs of all available devices.
std::vector<std::string> ListAvailableDeviceIds();

// Discovers a camera by its device id.
absl::StatusOr<GObjectPtr<ArvCamera>> DiscoverCamera(
    std::string_view device_id, absl::Duration timeout = absl::Seconds(0));

// Enables the fake camera interface and returns its device id.
std::string_view EnableFakeCamera();

// Creates a fake camera.
absl::StatusOr<GObjectPtr<ArvCamera>> CreateFakeCamera();

// Returns the factory calibrated camera params, if available.
absl::StatusOr<std::optional<CameraParams>> GetFactoryCameraParams(
    ArvCamera* absl_nonnull camera);

// Returns the camera to sensor transform, if available.
absl::StatusOr<std::optional<Pose>> GetCameraTSensor(
    ArvCamera* absl_nonnull camera);

// Returns the vendor name for a given camera.
std::string GetVendorName(ArvCamera* absl_nonnull arv_camera);

// Returns the model name for a given camera.
std::string GetModelName(ArvCamera* absl_nonnull arv_camera);

// Returns the firmware version for a given camera.
Version GetFirmwareVersion(ArvCamera* absl_nonnull arv_camera);

// Returns the respective exposure time name for a given camera.
std::optional<std::string_view> GetExposureTimeName(
    ArvCamera* absl_nonnull arv_camera);

// Returns the respective gain name for a given camera.
std::optional<std::string_view> GetGainName(ArvCamera* absl_nonnull arv_camera);

// Returns the exposure time range for a given camera, if available.
std::optional<CameraSettingProperties::Float::Range> GetExposureTimeRange(
    ArvCamera* absl_nonnull arv_camera);

// Returns the gain range for a given camera, if available.
std::optional<CameraSettingProperties::Float::Range> GetGainRange(
    ArvCamera* absl_nonnull arv_camera);

// Returns the exposure time increment for a given camera, if available.
std::optional<double> GetExposureTimeIncrement(
    ArvCamera* absl_nonnull arv_camera);

// Returns the gain increment for a given camera, if available.
std::optional<double> GetGainIncrement(ArvCamera* absl_nonnull arv_camera);

// Returns the exposure time properties for a given camera.
std::optional<CameraSettingProperties::Float> GetExposureTimeProperties(
    ArvCamera* absl_nonnull arv_camera);

// Returns the gain properties for a given camera.
std::optional<CameraSettingProperties::Float> GetGainProperties(
    ArvCamera* absl_nonnull arv_camera);

template <typename T>
absl::StatusOr<T> BufferHelper::GetChunkValue(
    std::string_view feature_name, ArvBuffer* absl_nonnull chunk_buffer) const {
  static constexpr char kChunkPrefix[] = "Chunk";

  if (!supported_features_.contains(feature_name)) {
    return ::intrinsic::NotFoundErrorBuilder()
           << "Parser doesn't support " << feature_name;
  }

  const std::string chunk_name = absl::StrCat(kChunkPrefix, feature_name);
  GError* error = nullptr;
  T value;
  if constexpr (std::is_same_v<T, bool>) {
    value = arv_chunk_parser_get_boolean_value(parser_.get(), chunk_buffer,
                                               chunk_name.data(), &error);
  } else if constexpr (std::is_integral<T>::value) {
    value = arv_chunk_parser_get_integer_value(parser_.get(), chunk_buffer,
                                               chunk_name.data(), &error);
  } else if constexpr (std::is_floating_point<T>::value) {
    value = arv_chunk_parser_get_float_value(parser_.get(), chunk_buffer,
                                             chunk_name.data(), &error);
  } else if constexpr (std::is_same_v<T, std::string>) {
    value = arv_chunk_parser_get_string_value(parser_.get(), chunk_buffer,
                                              chunk_name.data(), &error);
  } else {
    return absl::UnimplementedError("Unsupported type.");
  }
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  return value;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_ARAVIS_ARAVIS_UTILS_H_
