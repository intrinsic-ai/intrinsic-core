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

#include "intrinsic/perception/cameras/genicam_image_source.h"

#include <arv.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/overload.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "aravis/aravis_utils.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/aravis/aravis_glib_utils.h"
#include "intrinsic/perception/cameras/aravis/aravis_ptp_utils.h"
#include "intrinsic/perception/cameras/aravis/aravis_software_trigger.h"
#include "intrinsic/perception/cameras/aravis/aravis_utils.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/camera_setting_utils.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/perception/cameras/image_source.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/cameras/sensor_pixel_size/sensor_pixel_size.h"
#include "intrinsic/perception/cameras/software_hdr.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/hdr_utils.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/perception/core/range_tools.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/sysinfo.h"
#include "intrinsic/util/time/deadline_timeout.h"
#include "intrinsic/util/version.h"

namespace intrinsic::perception {
namespace {

constexpr char kOff[] = "Off";
constexpr char kOnce[] = "Once";
constexpr char kContinuous[] = "Continuous";
inline constexpr auto kAutoNames = std::array<const absl::string_view, 3>(
    {genicam::kExposureAuto, genicam::kGainAuto, genicam::kBalanceWhiteAuto});

constexpr int kNumErrorRetries = 4;
constexpr absl::Duration kErrorRetryDelay = absl::Milliseconds(500);
constexpr char kIntensity[] = "Intensity";
constexpr char kPhotoneoColorCamera[] = "ColorCamera";
constexpr char kRange[] = "Range";
constexpr char kNormal[] = "Normal";
constexpr char kSelectorSuffix[] = "Selector";
constexpr char kAlliedVision[] = "Allied Vision";
constexpr char kAlvium[] = "Alvium";

const Version& AlliedVisionAlviumMaximumFirmwareVersion() {
  static const absl::NoDestructor<Version>
      kAlliedVisionAlviumMaximumFirmwareVersion("13.1.794391F9");
  return *kAlliedVisionAlviumMaximumFirmwareVersion;
}

// This is a rough estimate of the frame processing overhead per image
// acquisition. Its precision is not critical for a functioning implementation.
// The sole effect of imprecise estimates is fewer retries on the error path and
// potentially too early warning which might be triggered by the system.
constexpr absl::Duration kProcessingOverhead = absl::Milliseconds(10);
// Some algorithms for computing GainAuto/ExposureAuto/BalanceWhiteAuto are
// really slow, so we force them to stop after a certain amount of frames.
constexpr int kMaxNumFramesToConverge = 10;
constexpr absl::Duration kMinStreamAutoConvergenceTimeout = absl::Seconds(5);

constexpr unsigned int kMinimumPacketSize = 1500;
constexpr unsigned int kMinimumJumboPacketSize = 8192;
constexpr unsigned int kDesiredJumboPacketSize = 9000;
constexpr absl::Duration kExposureTimeTolerance = absl::Microseconds(10);
constexpr absl::Duration kDeviceResetTimeout = absl::Seconds(150);

// 1 buffer in active image processing / callback dispatch.
// 1 buffer actively receiving the current frame over the wire.
// 1–2 buffers for GVSP packet resend / out-of-order frame interleaving.
// 2–3 spare buffers in the input queue to absorb system scheduling jitter.
constexpr int kNumStreamBuffers = 6;

template <typename C>
bool Contains(const C& c, absl::string_view parameter_name) {
  return absl::c_any_of(c, [parameter_name](auto setting_name) {
    return setting_name == parameter_name;
  });
}

inline constexpr std::array<const absl::string_view, 10>
    kParametersNotRequiringAcquisitionRestart = {
        genicam::kExposureTime,    genicam::kExposureTimeAbs,
        genicam::kExposureTimeRaw, genicam::kExposureTimeBaseAbs,
        genicam::kExposureAuto,    genicam::kGain,
        genicam::kGainAbs,         genicam::kGainRaw,
        genicam::kGainAuto,        genicam::kBalanceWhiteAuto};

bool ParameterRequiresAcquisitionRestart(absl::string_view parameter_name) {
  return !Contains(kParametersNotRequiringAcquisitionRestart, parameter_name) &&
         !parameter_name.ends_with(kSelectorSuffix);
}

inline constexpr std::array<absl::string_view, 3> kFrameRateValues = {
    genicam::kAcquisitionFrameRate, genicam::kAcquisitionFrameRateAbs,
    genicam::kFPS};

bool ParameterControlsFrameRate(absl::string_view parameter_name) {
  return Contains(kFrameRateValues, parameter_name);
}

inline constexpr std::array<absl::string_view, 2> kFrameRateEnableValues = {
    genicam::kAcquisitionFrameRateEnable,
    genicam::kAcquisitionFrameRateEnabled};

bool ParameterControlsFrameRateEnable(absl::string_view parameter_name) {
  return Contains(kFrameRateEnableValues, parameter_name);
}

void LogCameraInfos(ArvCamera* arv_camera) {
  LOG(INFO) << "Successfully opened camera.";
  LOG(INFO) << "vendor name             = " << GetVendorName(arv_camera);
  LOG(INFO) << "model name              = " << GetModelName(arv_camera);
  LOG(INFO) << "device id               = "
            << arv_camera_get_device_id(arv_camera, /*error=*/nullptr);
  LOG(INFO) << "device firmware version = " << GetFirmwareVersion(arv_camera);

  if (!arv_camera_is_gv_device(arv_camera)) return;
  ArvGvDevice* gv_device = ARV_GV_DEVICE(arv_camera_get_device(arv_camera));
  if (gv_device == nullptr) return;
  GInetSocketAddress* device_inet_socket_address =
      G_INET_SOCKET_ADDRESS(arv_gv_device_get_device_address(gv_device));
  if (device_inet_socket_address == nullptr) return;
  gchar* device_ip = g_inet_address_to_string(
      g_inet_socket_address_get_address(device_inet_socket_address));
  LOG(INFO) << "device IP address       = " << device_ip;
  g_free(device_ip);
  GInetSocketAddress* interface_inet_socket_address =
      G_INET_SOCKET_ADDRESS(arv_gv_device_get_interface_address(gv_device));
  if (interface_inet_socket_address == nullptr) return;
  gchar* interface_ip = g_inet_address_to_string(
      g_inet_socket_address_get_address(interface_inet_socket_address));
  LOG(INFO) << "interface IP address    = " << interface_ip;
  g_free(interface_ip);
}

absl::StatusOr<int> SetPacketSizeToAutoValue(ArvCamera* arv_camera) {
  GError* error = nullptr;
  if (!arv_camera_is_gv_device(arv_camera)) {
    LOG(INFO) << "Camera is not a GV device, not setting packet size.";
    return 0;
  }
  guint current_packet_size =
      arv_camera_gv_auto_packet_size(arv_camera, &error);
  if (const absl::Status status =
          ValidateAndNullArvError(error, "Error during camera creation.");
      !status.ok()) {
    LOG(WARNING) << "Error during arv_camera_gv_auto_packet_size:" << status
                 << "\nPacket size couldn't be adjusted automatically.";

    current_packet_size = arv_camera_gv_get_packet_size(arv_camera, &error);
    INTR_RETURN_IF_ERROR(
        ValidateAndNullArvError(error, "Error during camera creation."));
  }
  if (current_packet_size < kMinimumPacketSize) {
    LOG(INFO) << "Setting packet size to minimum of " << kMinimumPacketSize;
    arv_camera_gv_set_packet_size(arv_camera, kMinimumPacketSize, &error);
    if (const absl::Status status =
            ValidateAndNullArvError(error, "Error during camera creation.");
        !status.ok()) {
      LOG(WARNING) << "Error during arv_camera_gv_set_packet_size:" << status
                   << "\nPacket size couldn't be adjusted.";
    } else {
      current_packet_size = kMinimumPacketSize;
    }
  }
  return current_packet_size;
}

absl::Status SetExposureTime(
    ArvCamera* arv_camera, absl::Duration exposure_time,
    const std::optional<CameraSettingProperties::Float>& exposure_time_props) {
  const double exposure_time_us = absl::ToDoubleMicroseconds(exposure_time);
  GError* error = nullptr;
  arv_camera_set_exposure_time(
      arv_camera,
      exposure_time_props.has_value()
          ? ClampAndRoundToIncrement(exposure_time_us, *exposure_time_props)
          : exposure_time_us,
      &error);
  return ValidateAndNullArvError(error);
}

absl::Status VerifyExposureTime(
    std::optional<absl::Duration> received_exposure_time,
    absl::Duration expected_exposure_time, int num_tries) {
  if (received_exposure_time.has_value() &&
      absl::AbsDuration(*received_exposure_time - expected_exposure_time) >
          kExposureTimeTolerance) {
    return intrinsic::FailedPreconditionErrorBuilder()
           << "Mismatch in exposure time (received=" << *received_exposure_time
           << ", want=" << expected_exposure_time << ").";
  }
  // For cameras lacking ChunkExposureTime support, discard the first frame to
  // flush any stale buffer exposed with previous settings (2-frame fallback).
  if (!received_exposure_time.has_value() && num_tries == 1) {
    return intrinsic::FailedPreconditionErrorBuilder()
           << "Missing exposure time in first attempt (want="
           << expected_exposure_time << ").";
  }
  return absl::OkStatus();
}

absl::StatusOr<CaptureResult> FuseLdrCaptureResults(
    const std::vector<CaptureResult>& ldr_capture_results,
    HdrOperator hdr_operator,
    absl::Duration timeout = absl::InfiniteDuration()) {
  if (timeout <= absl::ZeroDuration()) {
    return absl::DeadlineExceededError(
        "Failed to start HDR image fusion within timeout.");
  }

  if (ldr_capture_results.empty()) {
    return absl::InternalError("No capture results found for HDR fusion.");
  }

  CaptureResult fused_capture_result = ldr_capture_results.front();

  if (std::all_of(ldr_capture_results.begin(), ldr_capture_results.end(),
                  [](const CaptureResult& capture) {
                    return capture.sensor_images.front().gray8u().has_value();
                  })) {
    const std::vector<const Image<Gray8u>*> ldr_images =
        Transformed(ldr_capture_results, [](const CaptureResult& capture) {
          return &capture.sensor_images.front().gray8u().value();
        });
    INTR_ASSIGN_OR_RETURN(Image<Gray8u> hdr_image,
                          LdrToHdr(ldr_images, hdr_operator));
    fused_capture_result.sensor_images.front() = SensorImage(
        fused_capture_result.sensor_images.front(), std::move(hdr_image));
    return fused_capture_result;
  }
  if (std::all_of(ldr_capture_results.begin(), ldr_capture_results.end(),
                  [](const CaptureResult& capture) {
                    return capture.sensor_images.front().rgb8u().has_value();
                  })) {
    const std::vector<const Image<Rgb8u>*> ldr_images =
        Transformed(ldr_capture_results, [](const CaptureResult& capture) {
          return &capture.sensor_images.front().rgb8u().value();
        });
    INTR_ASSIGN_OR_RETURN(Image<Rgb8u> hdr_image,
                          LdrToHdr(ldr_images, hdr_operator));
    fused_capture_result.sensor_images.front() = SensorImage(
        fused_capture_result.sensor_images.front(), std::move(hdr_image));
    return fused_capture_result;
  }
  return absl::InternalError("Unsupported image format for HDR fusion.");
}

std::vector<PixelType> ToPixelTypes(std::string_view component_selector) {
  if (component_selector == kIntensity ||
      component_selector == kPhotoneoColorCamera) {
    return {PixelType::kIntensity};
  } else if (component_selector == kRange) {
    return {PixelType::kDepth, PixelType::kPoint};
  } else if (component_selector == kNormal) {
    return {PixelType::kNormal};
  }
  return {PixelType::kUnspecified};
}

}  // namespace

absl::Status GenicamImageSource::ConfigureComponents(
    GenicamImageSource& aravis_camera) {
  GError* error = nullptr;

  const absl::StatusOr<CameraSetting> current_component_selector =
      aravis_camera.ReadCameraSettingImpl(genicam::kComponentSelector);

  // Restore the component selector to its original value.
  const absl::Cleanup on_return = [&] {
    if (!current_component_selector.ok()) {
      return;
    }
    absl::StatusOr<std::string> enum_value =
        GetEnumerationValue(*current_component_selector);
    if (!enum_value.ok()) {
      LOG(ERROR) << enum_value.status();
      return;
    }
    if (const absl::Status status = aravis_camera.UpdateCameraSettingImpl(
            CreateCameraSetting(genicam::kComponentSelector, *enum_value));
        !status.ok()) {
      LOG(ERROR) << status;
    }
  };

  std::vector<std::optional<std::string>> component_selectors = {std::nullopt};
  if (current_component_selector.ok()) {
    INTR_ASSIGN_OR_RETURN(std::vector<std::string> selectors,
                          EnumerateParameters(genicam::kComponentSelector,
                                              aravis_camera.camera_.get()));
    component_selectors = {selectors.begin(), selectors.end()};
  }

  for (size_t i = 0; i < component_selectors.size(); ++i) {
    const std::optional<std::string>& component_selector =
        component_selectors[i];
    int64_t sensor_id = kFallbackSensorId + i;
    bool disabled = false;
    if (component_selector.has_value()) {
      INTR_RETURN_IF_ERROR(
          aravis_camera.UpdateCameraSettingImpl(CreateCameraSetting(
              genicam::kComponentSelector, component_selector.value())));
      if (const absl::StatusOr<CameraSetting> component_id_value =
              aravis_camera.ReadCameraSettingImpl(genicam::kComponentIDValue);
          component_id_value.ok()) {
        INTR_ASSIGN_OR_RETURN(sensor_id,
                              GetValue<int64_t>(*component_id_value));
      }
      if (const absl::StatusOr<CameraSetting> component_enable =
              aravis_camera.ReadCameraSettingImpl(genicam::kComponentEnable);
          component_enable.ok()) {
        INTR_ASSIGN_OR_RETURN(const bool enabled,
                              GetValue<bool>(*component_enable));
        disabled = !enabled;
      }
    }
    const absl::StatusOr<CameraSettingAccess> offset_x_access =
        aravis_camera.ReadCameraSettingAccess(genicam::kOffsetX);
    const absl::StatusOr<CameraSettingAccess> offset_y_access =
        aravis_camera.ReadCameraSettingAccess(genicam::kOffsetY);
    const absl::StatusOr<CameraSettingAccess> width_access =
        aravis_camera.ReadCameraSettingAccess(genicam::kWidth);
    const absl::StatusOr<CameraSettingAccess> height_access =
        aravis_camera.ReadCameraSettingAccess(genicam::kHeight);

    const bool offset_write_access =
        offset_x_access.ok() &&
        (offset_x_access->mode == CameraSettingAccess::Mode::kWrite ||
         offset_x_access->mode == CameraSettingAccess::Mode::kReadWrite) &&
        offset_y_access.ok() &&
        (offset_y_access->mode == CameraSettingAccess::Mode::kWrite ||
         offset_y_access->mode == CameraSettingAccess::Mode::kReadWrite);
    const bool dimensions_write_access =
        width_access.ok() &&
        (width_access->mode == CameraSettingAccess::Mode::kWrite ||
         width_access->mode == CameraSettingAccess::Mode::kReadWrite) &&
        height_access.ok() &&
        (height_access->mode == CameraSettingAccess::Mode::kWrite ||
         height_access->mode == CameraSettingAccess::Mode::kReadWrite);
    const bool dimensions_read_access =
        width_access.ok() &&
        (width_access->mode == CameraSettingAccess::Mode::kRead ||
         width_access->mode == CameraSettingAccess::Mode::kReadWrite) &&
        height_access.ok() &&
        (height_access->mode == CameraSettingAccess::Mode::kRead ||
         height_access->mode == CameraSettingAccess::Mode::kReadWrite);

    if (offset_write_access) {
      INTR_RETURN_IF_ERROR(aravis_camera.UpdateOffset({0, 0}));
    }

    if (!dimensions_read_access) {
      return UnavailableErrorBuilder()
             << "Camera doesn't support reading its dimensions.";
    }
    // Try to set the dimensions to the sensor sizes. Note: It is important that
    // the width/height properties are read after we successfully set OffsetX
    // and OffsetY.
    INTR_ASSIGN_OR_RETURN(
        auto width_props,
        aravis_camera.ReadCameraSettingPropertiesImpl(genicam::kWidth));
    INTR_ASSIGN_OR_RETURN(
        auto height_props,
        aravis_camera.ReadCameraSettingPropertiesImpl(genicam::kHeight));
    // Here we are implicitly casting from int64 to int32. This should never
    // overflow for width / height.
    if (dimensions_write_access) {
      const CameraSettingProperties::Integer* width_props_integer =
          std::get_if<CameraSettingProperties::Integer>(
              &width_props.properties);
      const CameraSettingProperties::Integer* height_props_integer =
          std::get_if<CameraSettingProperties::Integer>(
              &height_props.properties);
      if (width_props_integer != nullptr &&
          width_props_integer->range.has_value() &&
          height_props_integer != nullptr &&
          height_props_integer->range.has_value()) {
        INTR_RETURN_IF_ERROR(aravis_camera.UpdateDimensions(
            Dimensions(width_props_integer->range->maximum,
                       height_props_integer->range->maximum)));
      }
    }

    INTR_ASSIGN_OR_RETURN(
        const std::optional<CameraParams> factory_camera_params,
        GetFactoryCameraParams(aravis_camera.camera_.get()));
    INTR_ASSIGN_OR_RETURN(const std::optional<Pose> camera_t_sensor,
                          GetCameraTSensor(aravis_camera.camera_.get()));

    aravis_camera.all_sensor_ids_.insert(sensor_id);

    if (component_selector.has_value()) {
      aravis_camera.component_selector_by_sensor_id_.emplace(
          sensor_id, *component_selector);
    }
    if (factory_camera_params.has_value()) {
      aravis_camera.factory_camera_params_by_sensor_id_.emplace(
          sensor_id, *factory_camera_params);
    }
    if (camera_t_sensor.has_value()) {
      aravis_camera.camera_t_sensor_by_sensor_id_.emplace(sensor_id,
                                                          *camera_t_sensor);
    }

    INTR_ASSIGN_OR_RETURN(Dimensions dimensions,
                          aravis_camera.ReadDimensions());
    ArvPixelFormat pixel_format =
        arv_camera_get_pixel_format(aravis_camera.camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

    absl::MutexLock lock(aravis_camera.mutex_);
    aravis_camera.state_.dimensions_by_sensor_id[sensor_id] = dimensions;
    aravis_camera.state_.pixel_format_by_sensor_id[sensor_id] = pixel_format;
    aravis_camera.state_.disabled_by_sensor_id[sensor_id] = disabled;
  }
  return absl::OkStatus();
}

absl::Status GenicamImageSource::ConfigureCameraDefaults(
    GenicamImageSource& aravis_camera) {
  LogCameraInfos(aravis_camera.camera_.get());

  INTR_RETURN_IF_ERROR(ConfigureComponents(aravis_camera));

  if (const absl::StatusOr<CameraSetting> scan3d_distance_unit =
          aravis_camera.ReadCameraSettingImpl(genicam::kScan3dDistanceUnit);
      scan3d_distance_unit.ok()) {
    INTR_ASSIGN_OR_RETURN(const std::string distance_unit,
                          GetEnumerationValue(*scan3d_distance_unit));
    if (distance_unit == "Millimeter") {
      aravis_camera.distance_scale_ = 0.001;
    } else if (distance_unit == "Meter") {
      aravis_camera.distance_scale_ = 1.0;
    } else if (distance_unit == "Inch") {
      aravis_camera.distance_scale_ = 0.0254;
    } else {
      LOG(WARNING) << "Unsupported distance unit: " << distance_unit;
    }
  }

  INTR_ASSIGN_OR_RETURN(int packet_size,
                        SetPacketSizeToAutoValue(aravis_camera.camera_.get()));
  LOG(INFO) << "Packet size: " << packet_size;

  GError* error = nullptr;

  // Set the camera to continuous acquisition mode and configure software
  // triggering, which is our default.
  const absl::StatusOr<CameraSettingAccess> acquisition_mode_access =
      aravis_camera.ReadCameraSettingAccess(genicam::kAcquisitionMode);
  if (acquisition_mode_access.ok() &&
      (acquisition_mode_access->mode == CameraSettingAccess::Mode::kWrite ||
       acquisition_mode_access->mode ==
           CameraSettingAccess::Mode::kReadWrite)) {
    arv_camera_set_string(aravis_camera.camera_.get(),
                          genicam::kAcquisitionMode, kContinuous, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  } else {
    LOG(WARNING) << "Could not set " << genicam::kAcquisitionMode << " to "
                 << kContinuous;
  }
  const absl::Status set_software_trigger_status =
      SetSoftwareTrigger(aravis_camera.camera_.get());
  aravis_camera.use_software_trigger_ = set_software_trigger_status.ok();
  if (!aravis_camera.use_software_trigger_) {
    LOG(WARNING) << "Could not enable software triggering. Status: "
                 << set_software_trigger_status;
  }
  LOG(INFO) << "Software trigger: "
            << (aravis_camera.use_software_trigger_ ? "On" : "Off");

  aravis_camera.exposure_time_props_ =
      GetExposureTimeProperties(aravis_camera.camera_.get());
  aravis_camera.gain_props_ = GetGainProperties(aravis_camera.camera_.get());
  if (aravis_camera.exposure_time_props_.has_value() &&
      aravis_camera.exposure_time_props_->range.has_value()) {
    aravis_camera.software_hdr_ =
        SoftwareHdr(*aravis_camera.exposure_time_props_);
  } else {
    aravis_camera.software_hdr_ = std::nullopt;
  }

  // If available: Configure camera to send images as chunks with attached
  // exposure time information.
  aravis_camera.buffer_helper_ =
      BufferHelper(aravis_camera.camera_.get(),
                   {genicam::kWidth, genicam::kHeight, genicam::kPixelFormat,
                    genicam::kExposureTime});

  // Disable packet socket, as it leads to high latencies on docker/kubernetes,
  // see also:
  // https://github.com/AravisProject/aravis/issues/691#issuecomment-1194287752
  // https://github.com/AravisProject/aravis/issues/613#issuecomment-1054222737
  if (arv_camera_is_gv_device(aravis_camera.camera_.get())) {
    arv_camera_gv_set_stream_options(
        aravis_camera.camera_.get(),
        ARV_GV_STREAM_OPTION_PACKET_SOCKET_DISABLED);
  }

  aravis_camera.stream_ = GObjectPtr<ArvStream>(arv_camera_create_stream(
      aravis_camera.camera_.get(), /*callback=*/nullptr, /*user_data=*/nullptr,
      /*destroy=*/nullptr, &error));
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

  aravis_camera.acquisition_state_ = {.is_started = false, .payload_size = 0};

  // Try to avoid lost packets because of a full buffer under heavy CPU load,
  // see also
  // https://aravisproject.github.io/aravis/aravis-stable/ethernet.html#socket-buffer-size.
  if (arv_camera_is_gv_device(aravis_camera.camera_.get())) {
    g_object_set(aravis_camera.stream_.get(), "socket-buffer",
                 ARV_GV_STREAM_SOCKET_BUFFER_AUTO, "socket-buffer-size", 0,
                 nullptr);
  }

  if (arv_camera_is_gv_device(aravis_camera.camera_.get())) {
    // See
    // https://www.emva.org/wp-content/uploads/GenICam_SFNC_v2_7.pdf#page=448
    // for more details on multipart support.
    const bool is_multipart_supported = arv_camera_gv_is_multipart_supported(
        aravis_camera.camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    if (is_multipart_supported) {
      arv_camera_gv_set_multipart(aravis_camera.camera_.get(), true, &error);
      INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    }
  }

  absl::SleepFor(absl::Milliseconds(10));
  g_signal_connect_data(aravis_camera.stream_.get(), "new-buffer",
                        reinterpret_cast<GCallback>(OnNewBuffer),
                        &aravis_camera, nullptr, G_CONNECT_DEFAULT);
  arv_stream_set_emit_signals(aravis_camera.stream_.get(), 1);

  // Connect control lost callback.
  aravis_camera.lost_control_handler_id_ = g_signal_connect_data(
      arv_camera_get_device(aravis_camera.camera_.get()), "control-lost",
      reinterpret_cast<GCallback>(OnControlLost), &aravis_camera, nullptr,
      G_CONNECT_DEFAULT);

  absl::MutexLock lock(aravis_camera.mutex_);
  aravis_camera.state_.packet_size = packet_size;
  aravis_camera.state_.lost_control = false;

  return absl::OkStatus();
}

absl::Status GenicamImageSource::RetryOnPermissionDeniedOrTimeoutOrBusy(
    const std::function<absl::Status()>& fn) {
  absl::Status status;
  for (int i = 0; i < kNumErrorRetries; ++i) {
    status = fn();
    switch (status.code()) {
      case absl::StatusCode::kPermissionDenied: {
        LOG(WARNING) << "Permission denied, trying again after "
                     << kErrorRetryDelay << ". Status: " << status;
        absl::SleepFor(kErrorRetryDelay);
        // For GigE Vision devices, try re-acquiring the control channel in
        // place before falling back to full camera recreation (which
        // re-downloads and re-parses the camera's GenICam XML).
        if (camera_ != nullptr && arv_camera_is_gv_device(camera_.get())) {
          GError* error = nullptr;
          const bool took_control = arv_gv_device_take_control(
              ARV_GV_DEVICE(arv_camera_get_device(camera_.get())), &error);
          if (ValidateAndNullArvError(error).ok() && took_control) {
            break;
          }
        }
        LOG(WARNING) << "Rediscovering camera " << device_id_ << ".";
        camera_.reset();
        INTR_ASSIGN_OR_RETURN(camera_, DiscoverCamera(device_id_));
        break;
      }
      // This is mainly caused by a device such as Photoneo still being busy
      // while capturing for a long time.
      case absl::StatusCode::kDeadlineExceeded:
      case absl::StatusCode::kUnavailable: {
        LOG(WARNING) << "Device timeout or busy, trying again after "
                     << kErrorRetryDelay << ". Status: " << status;
        absl::SleepFor(kErrorRetryDelay);
        break;
      }
      default: {
        return status;
      }
    }
  }
  return status;
}

absl::Status GenicamImageSource::Init(bool requires_device_reset) {
  if (camera_ == nullptr) {
    INTR_ASSIGN_OR_RETURN(camera_, DiscoverCamera(device_id_));
  }

  // Immediately reset camera before applying other settings, if a reset command
  // was found. This way we can be 100% sure that the camera is in a clean state
  // before applying any default values.
  if (requires_device_reset) {
    INTR_RETURN_IF_ERROR(RetryOnPermissionDeniedOrTimeoutOrBusy([this] {
      if (lost_control_handler_id_ > 0) {
        g_signal_handler_disconnect(arv_camera_get_device(camera_.get()),
                                    lost_control_handler_id_);
        lost_control_handler_id_ = 0;
      }
      GError* error = nullptr;
      arv_camera_execute_command(camera_.get(), genicam::kDeviceReset, &error);
      return ValidateAndNullArvError(error);
    }));

    LOG(INFO) << "Camera was reset to its power up state and needs to be "
                 "rediscovered.";
    camera_.reset();
    INTR_ASSIGN_OR_RETURN(camera_,
                          DiscoverCamera(device_id_, kDeviceResetTimeout));
  }

  INTR_RETURN_IF_ERROR(ConfigureCameraDefaults(*this));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ImageSource>> GenicamImageSource::Create(
    const CameraIdentifier& camera_identifier) {
  const CameraIdentifier::GenICam* genicam =
      std::get_if<CameraIdentifier::GenICam>(&camera_identifier.driver);
  if (genicam == nullptr || genicam->device_id.empty()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "GenICam camera creation triggered without the "
              "necessary configuration 'genicam'.";
  }

  std::unique_ptr<GenicamImageSource> aravis_camera(
      new GenicamImageSource(genicam->device_id));
  INTR_RETURN_IF_ERROR(aravis_camera->Init(/*requires_device_reset=*/false));

  return aravis_camera;
}

absl::StatusOr<std::unique_ptr<ImageSource>> GenicamImageSource::CreateFake(
    const CameraIdentifier&) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<ImageSource> aravis_camera,
      Create({.driver = CameraIdentifier::GenICam{
                  .device_id = std::string(EnableFakeCamera())}}));

  // Set default value for ExposureAuto to 'Off'
  GError* error = nullptr;
  arv_device_write_register(
      arv_camera_get_device(
          dynamic_cast<GenicamImageSource&>(*aravis_camera).camera_.get()),
      0x132, 0, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

  return aravis_camera;
}

GenicamImageSource::GenicamImageSource(std::string_view device_id)
    : device_id_(device_id) {}

GenicamImageSource::~GenicamImageSource() {
  if (stream_ != nullptr) {
    arv_stream_set_emit_signals(stream_.get(), FALSE);
  }
  if (const absl::Status status = GetFaultsStatus(); !status.ok()) {
    LOG(ERROR) << status;
    return;
  }
  if (const absl::Status status = StopStream(); !status.ok()) {
    LOG(ERROR) << status;
  }
  if (const absl::Status status = StopAsyncAcquisition(); !status.ok()) {
    LOG(ERROR) << status;
  }
}

absl::Duration GenicamImageSource::MimimalAcquisitionTimeout(
    absl::Duration exposure_time) const {
  if (use_software_trigger_) {
    // In the software triggering case, the image exposure starts in the moment
    // in which the software trigger is activated, i.e. the minimal time one
    // needs to wait until an image is successfully acquired is the full
    // exposure time in addition to the general processing overhead.
    return exposure_time + kProcessingOverhead;
  }

  // We don't add the exposure time for the free-running mode since the working
  // assumption is that whenever a frame is requested, it has already been
  // acquired in the past. We will always get the last frame and don't need to
  // wait for the exposure to finish.
  return kProcessingOverhead;
}

absl::StatusOr<std::vector<SensorInformation>>
GenicamImageSource::DescribeCameraSensorsImpl() const {
  std::vector<int64_t> ids(all_sensor_ids_.begin(), all_sensor_ids_.end());
  std::sort(ids.begin(), ids.end());

  std::vector<SensorInformation> sensor_informations;
  sensor_informations.reserve(ids.size());

  absl::MutexLock lock(mutex_);
  for (int64_t id : ids) {
    const auto it_component_selector =
        component_selector_by_sensor_id_.find(id);
    const auto it_factory_camera_params =
        factory_camera_params_by_sensor_id_.find(id);

    const auto it_dimensions = state_.dimensions_by_sensor_id.find(id);
    INTR_RET_CHECK(it_dimensions != state_.dimensions_by_sensor_id.end());
    const auto it_disabled = state_.disabled_by_sensor_id.find(id);
    INTR_RET_CHECK(it_disabled != state_.disabled_by_sensor_id.end());

    std::string display_name =
        it_component_selector != component_selector_by_sensor_id_.end()
            ? it_component_selector->second
            : "color";
    std::optional<CameraParams> factory_camera_params =
        it_factory_camera_params != factory_camera_params_by_sensor_id_.end()
            ? std::make_optional(Resize(it_factory_camera_params->second,
                                        it_dimensions->second))
            : std::nullopt;
    std::optional<Pose> camera_t_sensor =
        OptionalCopyAt(camera_t_sensor_by_sensor_id_, id);
    std::vector<PixelType> supported_pixel_types =
        it_component_selector != component_selector_by_sensor_id_.end()
            ? ToPixelTypes(it_component_selector->second)
            : std::vector<PixelType>{PixelType::kIntensity};

    sensor_informations.emplace_back(
        id, display_name, factory_camera_params, camera_t_sensor,
        supported_pixel_types, it_dimensions->second, it_disabled->second);
  }

  return sensor_informations;
}

absl::StatusOr<CaptureResult> GenicamImageSource::CaptureImpl(
    absl::Duration timeout) {
  const stats::ScopedSpan span("GenicamImageSource::CaptureImpl");
  {
    // We do not allow streaming and capturing at the same time. This can be
    // problematic if the camera parameters are not the same. See the TDD for
    // more details: go/intrinsic-camera-streaming-design
    absl::MutexLock lock(mutex_);
    if (streaming_state_.has_value()) {
      return absl::FailedPreconditionError(
          "We do not allow Capture calls while there is a stream running.");
    }
  }

  const absl::Time start_capture = absl::Now();
  const absl::Time deadline = start_capture + timeout;

  INTR_RETURN_IF_ERROR(GetFaultsStatus());

  // Store any initial `Once` and `Continuous` auto settings when software
  // triggering or software HDR is active.
  std::vector<CameraSetting> auto_settings;
  if (use_software_trigger_ ||
      (software_hdr_.has_value() && software_hdr_->IsEnabled())) {
    const stats::ScopedSpan span("GenicamImageSource::CaptureImpl::StoreAuto");
    INTR_ASSIGN_OR_RETURN(auto_settings, GetActiveAutoSettings());
  }

  CaptureResult capture_result;
  std::vector<CaptureResult> ldr_capture_results;
  std::vector<CameraSetting> auto_settings_to_restore;
  {
    // Try to restore all modified auto settings after acquisition is done.
    const absl::Cleanup on_return = [&] {
      if (const absl::Status status =
              RestoreAutoSettings(auto_settings_to_restore);
          !status.ok()) {
        LOG(ERROR) << status;
      }
    };

    acquisition_measurements_per_exposure_time_.clear();
    if (!auto_settings.empty()) {
      INTR_ASSIGN_OR_RETURN(
          capture_result,
          ConvergeAutoSettings(auto_settings, ToTimeout(deadline),
                               auto_settings_to_restore));
    } else {
      INTR_ASSIGN_OR_RETURN(capture_result,
                            GetLdrCaptureResult(ToTimeout(deadline)));
    }

    if (software_hdr_.has_value() && software_hdr_->IsEnabled()) {
      INTR_ASSIGN_OR_RETURN(ldr_capture_results,
                            GetLdrCaptureResults(ToTimeout(deadline)));
    }
  }
  LOG(INFO)
      << "Acquisition: "
      << absl::StrJoin(
             acquisition_measurements_per_exposure_time_, ", ",
             absl::PairFormatter(
                 [](std::string* o, absl::Duration exposure_time) {
                   absl::StrAppend(o, absl::StrCat("exp ", exposure_time));
                 },
                 " took ",
                 [](std::string* o, const AcquisitionMeasurement& measurement) {
                   absl::StrAppend(
                       o, absl::StrCat(measurement.acquisition_time, " (",
                                       measurement.num_retries,
                                       measurement.num_retries > 1 ? " tries)"
                                                                   : " try)"));
                 }));

  if (!ldr_capture_results.empty()) {
    std::vector<CaptureResult> capture_results_to_fuse;
    capture_results_to_fuse.reserve(ldr_capture_results.size() + 1);
    capture_results_to_fuse.push_back(std::move(capture_result));
    capture_results_to_fuse.insert(
        capture_results_to_fuse.end(),
        std::make_move_iterator(ldr_capture_results.begin()),
        std::make_move_iterator(ldr_capture_results.end()));
    INTR_RET_CHECK(std::all_of(
        capture_results_to_fuse.begin(), capture_results_to_fuse.end(),
        [this](const CaptureResult& capture) {
          int64_t sensor_id = capture.sensor_images.front().sensor_id();
          const auto it = component_selector_by_sensor_id_.find(sensor_id);
          return sensor_id == kFallbackSensorId ||
                 (it != component_selector_by_sensor_id_.end() &&
                  (it->second == kIntensity ||
                   it->second == kPhotoneoColorCamera));
        }));
    CHECK(software_hdr_.has_value());
    INTR_ASSIGN_OR_RETURN(const HdrOperator hdr_operator,
                          software_hdr_->HdrOperator());
    INTR_ASSIGN_OR_RETURN(
        capture_result,
        FuseLdrCaptureResults(capture_results_to_fuse, hdr_operator,
                              ToTimeout(deadline)));
  }

  capture_result.capture_duration = capture_result.capture_at - start_capture;

  return capture_result;
}

absl::StatusOr<std::vector<CameraSetting>>
GenicamImageSource::GetActiveAutoSettings() const {
  std::vector<CameraSetting> settings;
  for (const absl::string_view auto_name : kAutoNames) {
    const absl::StatusOr<CameraSettingAccess> access =
        ReadCameraSettingAccess(auto_name);
    if (!access.ok() || access->mode != CameraSettingAccess::Mode::kReadWrite) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(const CameraSetting setting,
                          ReadCameraSetting(auto_name));
    INTR_ASSIGN_OR_RETURN(const std::string enum_value,
                          GetEnumerationValue(setting));
    if (enum_value != kOff) {
      settings.push_back(setting);
    }
  }
  return settings;
}

absl::StatusOr<CaptureResult> GenicamImageSource::ConvergeAutoSettings(
    absl::Span<const CameraSetting> active_auto_settings,
    absl::Duration timeout,
    std::vector<CameraSetting>& auto_settings_to_restore) {
  INTR_RET_CHECK(!active_auto_settings.empty());
  const absl::Time deadline = ToDeadline(timeout);

  // In case we trigger each frame manually by software, we can't be sure about
  // when the last frame was actually acquired, so any auto settings may be
  // outdated. Therefore switch `Continuous` ones to `Once`, so we can wait
  // until the camera driver is finished with auto processing and sets them to
  // `Off`. This way we can ensure to return the optimum frame.
  // Only `Continuous`, but not `Once` auto settings need to be restored, as the
  // camera driver sets them to `Off` in the loop below, and we don't want to do
  // `Once` a second time.
  // For HDR (both single-frame capture and streaming) we need the same
  // behavior, as the internal change of exposure times would otherwise conflict
  // with automatic adjustments.
  std::vector<absl::string_view> remaining_auto_settings_to_wait_for;
  {
    const stats::ScopedSpan span(
        "GenicamImageSource::ConvergeAutoSettings::SetOnce");
    remaining_auto_settings_to_wait_for.reserve(active_auto_settings.size());
    for (const CameraSetting& auto_setting : active_auto_settings) {
      remaining_auto_settings_to_wait_for.push_back(auto_setting.name);
      INTR_ASSIGN_OR_RETURN(const std::string enum_value,
                            GetEnumerationValue(auto_setting));
      if (enum_value == kContinuous) {
        INTR_RETURN_IF_ERROR(UpdateCameraSettingInternal(
            CreateEnumCameraSetting(auto_setting.name, kOnce)));
        auto_settings_to_restore.push_back(auto_setting);
      }
    }
  }

  // Wait until all active auto settings converge to `Off`. Local experiments
  // in the lab showed that for software triggering convergence is usually
  // reached after about 3 to 6 calculation cycles with a Basler acA1920-50gc
  // camera, depending on whether initial settings were close or far from
  // ideal ones. According to
  // https://docs.baslerweb.com/exposure-auto#enabling-or-disabling-exposure-auto
  // the maximum number of calculation cycles is 30. Other cameras of course
  // may differ. In any case, the loop will error out after the specified
  // timeout is reached.
  CaptureResult capture_result;
  int num_frames = 0;
  do {
    INTR_ASSIGN_OR_RETURN(
        capture_result, GetLdrCaptureResult(ToTimeout(deadline)),
        _ << "Failed to wait for ["
          << absl::StrJoin(remaining_auto_settings_to_wait_for, ",")
          << "] computations to finish within " << timeout << ".");
    for (auto it = remaining_auto_settings_to_wait_for.begin();
         it != remaining_auto_settings_to_wait_for.end();) {
      INTR_ASSIGN_OR_RETURN(const CameraSetting setting,
                            ReadCameraSetting(*it));
      INTR_ASSIGN_OR_RETURN(const std::string enum_value,
                            GetEnumerationValue(setting));
      if (enum_value == kOff) {
        it = remaining_auto_settings_to_wait_for.erase(it);
      } else {
        ++it;
      }
    }
    if (++num_frames >= kMaxNumFramesToConverge &&
        !remaining_auto_settings_to_wait_for.empty()) {
      LOG(WARNING) << "Reached the maximum number of "
                   << kMaxNumFramesToConverge
                   << " frames to converge, switching off any remaining auto "
                      "settings.";
      for (absl::string_view name : remaining_auto_settings_to_wait_for) {
        INTR_RETURN_IF_ERROR(
            UpdateCameraSettingInternal(CreateEnumCameraSetting(name, kOff)));
      }
      remaining_auto_settings_to_wait_for.clear();
    }
  } while (!remaining_auto_settings_to_wait_for.empty());

  return capture_result;
}

absl::Status GenicamImageSource::RestoreAutoSettings(
    absl::Span<const CameraSetting> auto_settings_to_restore) {
  absl::Status status = absl::OkStatus();
  for (const CameraSetting& auto_setting : auto_settings_to_restore) {
    status.Update(UpdateCameraSettingInternal(auto_setting));
  }
  return status;
}

absl::StatusOr<std::vector<absl::Duration>>
GenicamImageSource::GetHdrExposureTimes() const {
  if (!software_hdr_.has_value() || !software_hdr_->IsEnabled()) {
    return std::vector<absl::Duration>{};
  }
  INTR_ASSIGN_OR_RETURN(const CameraSetting init_exposure_time,
                        ReadCameraSetting(genicam::kExposureTime));
  INTR_ASSIGN_OR_RETURN(const double init_exposure_time_us,
                        GetValue<double>(init_exposure_time));
  return software_hdr_->HdrExposureTimes(
      absl::Microseconds(init_exposure_time_us));
}

absl::StatusOr<std::vector<CaptureResult>>
GenicamImageSource::GetLdrCaptureResults(absl::Duration timeout) {
  const stats::ScopedSpan span("GenicamImageSource::GetLdrCaptureResults");
  const absl::Time deadline = ToDeadline(timeout);

  INTR_ASSIGN_OR_RETURN(const std::vector<absl::Duration> exposure_times,
                        GetHdrExposureTimes());
  if (exposure_times.size() <= 1) {
    return std::vector<CaptureResult>{};
  }

  // In software-triggered mode, some rolling-shutter cameras (e.g., Allied
  // Vision Alvium) ignore a `TriggerSoftware` command sent immediately after
  // reconfiguring `ExposureTime`. This happens in two cases:
  // 1. When decreasing `ExposureTime`, the sensor's rolling-shutter reset timer
  //    remains active for up to `prev_exposure - next_exposure` after readout.
  // 2. After a very short exposure, frame transfer to the host can complete
  //    slightly before the sensor's vertical blanking interval (and minimum
  //    frame period `1 / AcquisitionFrameRateMax`) has elapsed.
  //
  // Calling `StopAsyncAcquisition` (`AcquisitionStop`, without deallocating
  // stream buffers) before `SetExposureTime` synchronously resets the camera's
  // acquisition state machine (`GetCaptureResultAsync` restarts acquisition
  // before triggering), while avoiding acquisition restarts in free-running
  // mode.
  absl::Duration current_exposure_time = exposure_times.front();
  const auto update_exposure_time =
      [this, &current_exposure_time](
          absl::Duration next_exposure_time) -> absl::Status {
    if (next_exposure_time == current_exposure_time) {
      return absl::OkStatus();
    }
    if (use_software_trigger_) {
      INTR_RETURN_IF_ERROR(StopAsyncAcquisition());
    }
    INTR_RETURN_IF_ERROR(SetExposureTime(camera_.get(), next_exposure_time,
                                         exposure_time_props_));
    current_exposure_time = next_exposure_time;
    return absl::OkStatus();
  };

  // In case anything goes wrong during HDR acquisition, try to restore the
  // original exposure time setting on exit.
  const absl::Cleanup restore_exposure_on_return = [&] {
    if (const absl::Status status =
            update_exposure_time(exposure_times.front());
        !status.ok()) {
      LOG(ERROR) << status;
    }
  };

  std::vector<CaptureResult> ldr_capture_results;
  ldr_capture_results.reserve(exposure_times.size() - 1);
  for (const absl::Duration exposure_time :
       absl::MakeSpan(exposure_times).subspan(1)) {
    INTR_RETURN_IF_ERROR(update_exposure_time(exposure_time));
    INTR_ASSIGN_OR_RETURN(
        CaptureResult ldr_capture_result,
        GetLdrCaptureResult(ToTimeout(deadline), exposure_time));
    ldr_capture_results.push_back(std::move(ldr_capture_result));
  }
  return ldr_capture_results;
}

absl::StatusOr<CaptureResult> GenicamImageSource::GetLdrCaptureResult(
    absl::Duration timeout,
    std::optional<absl::Duration> expected_exposure_time) {
  const stats::ScopedSpan span("GenicamImageSource::GetLdrCaptureResult");
  absl::Duration exposure_time;
  if (expected_exposure_time.has_value()) {
    exposure_time = *expected_exposure_time;
  } else {
    INTR_ASSIGN_OR_RETURN(const CameraSetting exposure_time_setting,
                          ReadCameraSetting(genicam::kExposureTime));
    INTR_ASSIGN_OR_RETURN(const double exposure_time_us,
                          GetValue<double>(exposure_time_setting));
    exposure_time = absl::Microseconds(exposure_time_us);
  }
  const absl::Duration minimal_timeout =
      MimimalAcquisitionTimeout(exposure_time);
  if (timeout < minimal_timeout) {
    LOG(WARNING)
        << "This warning can result in a potential error when the camera is "
           "configured for timed exposure acquisition. The user specified "
           "timeout of "
        << timeout
        << " is smaller than the exposure time + processing overhead = "
        << exposure_time << " + " << kProcessingOverhead
        << ". In case of errors, increase the timeout or decrease the camera's "
           "exposure time.";
  }

  const bool requires_exact_exposure_time =
      expected_exposure_time.has_value() && !use_software_trigger_;
  int num_tries = 0;
  absl::StatusOr<CaptureResultWithExposureTime> status_or_capture_result;
  const absl::Time start_time = absl::Now();
  const absl::Time deadline = start_time + timeout;
  do {
    ++num_tries;
    status_or_capture_result = GetCaptureResultAsync(deadline);
    if (!status_or_capture_result.ok()) continue;
    if (requires_exact_exposure_time) {
      if (absl::Status verify_status =
              VerifyExposureTime(status_or_capture_result->exposure_time,
                                 exposure_time, num_tries);
          !verify_status.ok()) {
        LOG(INFO) << "Retrying to acquire frame. " << verify_status.message();
        status_or_capture_result = std::move(verify_status);
        continue;
      }
    }
    acquisition_measurements_per_exposure_time_[exposure_time] = {
        .acquisition_time = absl::Now() - start_time, .num_retries = num_tries};
    VLOG(1) << absl::StrFormat(
        "Retrieved frame from camera %s after attempt number %d.", device_id_,
        num_tries);
    return std::move(status_or_capture_result->capture_result);
  } while (absl::Now() < deadline /* time left */);

  absl::Status status = status_or_capture_result.status();
  CHECK(!status.ok());

  if (absl::IsDeadlineExceeded(status)) {
    // Append additional information to the status message, so the user knows
    // what the currently used timeout is.
    std::string error_message(status.message());
    if (timeout < minimal_timeout) {
      absl::StrAppend(
          &error_message,
          absl::Substitute(" The most likely culprit in this case is a too "
                           "small timeout of $0 which is smaller than the "
                           "exposure time of $1. To fix this, either increase "
                           "the timeout or decrease the exposure time.",
                           absl::FormatDuration(timeout),
                           absl::FormatDuration(exposure_time)));
    }
    status = intrinsic::DeadlineExceededErrorBuilder() << error_message;
  }

  LOG(WARNING) << "Failed to acquire image within " << timeout << " after "
               << num_tries << " tries. Last error: " << status;
  return status;
}

absl::StatusOr<GenicamImageSource::CaptureResultWithExposureTime>
GenicamImageSource::GetCaptureResultAsync(absl::Time deadline) {
  {
    absl::MutexLock lock(mutex_);
    acquisition_span_.emplace("GenicamImageSource::GetCaptureResultAsync");
  }
  const absl::Cleanup span_cleanup = [this] {
    absl::MutexLock lock(mutex_);
    acquisition_span_.reset();
  };

  INTR_RETURN_IF_ERROR(StartAsyncAcquisition());

  {
    absl::MutexLock lock(mutex_);
    new_buffer_handled_ = false;
  }

  if (use_software_trigger_) {
    // We need to manually trigger the image acquisition.
    // This incurs potentially significant overhead (~15 ms for Basler USB
    // camera with VGA resolution) when acquiring images as compared to
    // asynchronous acquisition. It has the benefit of limiting bandwidth
    // access when multiple cameras are used.
    GError* error = nullptr;
    arv_camera_software_trigger(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    VLOG(1) << "Triggered.";
  }

  const absl::Time trigger_time = absl::Now();

  // Wait until the frame acquisition callback returns (new_buffer_handled_ ==
  // true) or control was lost and then acquire the mutex and save the results
  // before returning.
  const auto new_buffer_handled_or_lost_control = [this]() {
    mutex_.AssertReaderHeld();
    return new_buffer_handled_ || state_.lost_control;
  };
  if (mutex_.LockWhenWithDeadline(
          absl::Condition(&new_buffer_handled_or_lost_control), deadline)) {
    if (state_.lost_control) {
      mutex_.unlock();
      return absl::DataLossError("Camera control lost during acquisition.");
    }
    if (!status_.ok()) {
      // We have to create a copy since 'status_' must at all times be protected
      // by 'mutex_' and this also holds for returning it.
      absl::Status local_status = status_;
      mutex_.unlock();
      return std::move(local_status);
    }

    // We have to create a moved-copy since 'capture_result_' must at all times
    // be protected by 'mutex_' and this also holds for returning it.
    CaptureResultWithExposureTime local_capture = std::move(capture_result_);
    mutex_.unlock();

    absl::Time aravis_buffer_reception_time =
        local_capture.capture_result.sensor_images.front().acquisition_time();
    absl::Time buffer_finished_time = absl::Now();

    // Time between triggering and having the final image available.
    const auto total_acquisition_ms =
        absl::ToInt64Milliseconds(buffer_finished_time - trigger_time);

    // The time it took till the OnNewBuffer() callback was invoked after
    // entering GetSensorImageAsync(). In other words, the time it took for
    // absl::Condition(&new_buffer_handled_) (see above) to become true.
    const auto time_till_sensor_image_was_received =
        absl::ToInt64Milliseconds(buffer_callback_time_ - trigger_time);

    // The time between having received the image in Aravis (the image which is
    // stored in the sensor image) and the time at which the OnNewBuffer()
    // callback actually got invoked. This is Aravis internal processing time
    // 'post' the buffer receival.
    // Note: This value is only somewhat accurate in case of software
    // triggering.
    const auto aravis_internal_post_processing_time = absl::ToInt64Milliseconds(
        buffer_callback_time_ - aravis_buffer_reception_time);

    // The time between the invocation of the OnNewBuffer() callback and 'now'.
    // The total time is dominated by copying and converting the buffer data.
    // The rest is synchronization overhead (absl::Mutex, absl::Condition) and
    // logging.
    const auto buffer_conversion_and_decoding_time =
        absl::ToInt64Milliseconds(buffer_finished_time - buffer_callback_time_);

    if (use_software_trigger_) {
      VLOG(1) << absl::StrFormat(
          "Break-down of total observed image-retrieval time of %d ms:\n"
          "\tWait-time till a new buffer arrived..: %3d ms\n"
          "\t  Aravis internal processing time....: %3d ms\n"
          "\tBuffer conversion and decoding time..: %3d ms\n",
          total_acquisition_ms, time_till_sensor_image_was_received,
          aravis_internal_post_processing_time,
          buffer_conversion_and_decoding_time);
    } else {
      VLOG(1) << absl::StrFormat(
          "Break-down of total observed image-retrieval time of %d ms:\n"
          "\tWait-time till a new buffer arrived..: %3d ms\n"
          "\tBuffer conversion and decoding time..: %3d ms\n",
          total_acquisition_ms, time_till_sensor_image_was_received,
          buffer_conversion_and_decoding_time);
    }

    return std::move(local_capture);
  }
  mutex_.unlock();

  return absl::DeadlineExceededError(
      "Failed to acquire image within user specified deadline.");
}

absl::StatusOr<CameraSettingAccess>
GenicamImageSource::ReadCameraSettingAccessImpl(absl::string_view name) const {
  INTR_RETURN_IF_ERROR(GetFaultsStatus());

  // HDR parameters need special handling since they are private to this class
  // and are not propagated to Aravis.
  if (SoftwareHdr::CanHandleCameraSetting(name)) {
    if (!software_hdr_.has_value()) {
      return intrinsic::FailedPreconditionErrorBuilder()
             << "Software HDR is unavailable.";
    }
    return software_hdr_->ReadCameraSettingAccess(name);
  }

  absl::MutexLock lock(camera_mutex_);
  if ((name == genicam::kSensorPixelWidth ||
       name == genicam::kSensorPixelHeight) &&
      SensorPixelSize(GetVendorName(camera_.get()), GetModelName(camera_.get()))
          .has_value()) {
    return CreateCameraSettingAccess(name, CameraSettingAccess::Mode::kRead);
  }

  // Different camera vendors have different ways to get the exposure time and
  // gain, unfortunately aravis has no method for this, so we use custom logic.
  const auto access_mode_name = [this, name]() {
    if (name == genicam::kExposureTime)
      return GetExposureTimeName(camera_.get()).value_or(name);
    else if (name == genicam::kGain)
      return GetGainName(camera_.get()).value_or(name);
    return name;
  };
  INTR_ASSIGN_OR_RETURN(const ArvGcAccessMode access,
                        GetAccessMode(access_mode_name(), camera_.get()));
  switch (access) {
    case ArvGcAccessMode::ARV_GC_ACCESS_MODE_UNDEFINED:
      return CreateCameraSettingAccess(name,
                                       CameraSettingAccess::Mode::kUnsupported);
    case ArvGcAccessMode::ARV_GC_ACCESS_MODE_RO:
      return CreateCameraSettingAccess(name, CameraSettingAccess::Mode::kRead);
    case ArvGcAccessMode::ARV_GC_ACCESS_MODE_WO:
      return CreateCameraSettingAccess(name, CameraSettingAccess::Mode::kWrite);
    case ArvGcAccessMode::ARV_GC_ACCESS_MODE_RW:
      return CreateCameraSettingAccess(name,
                                       CameraSettingAccess::Mode::kReadWrite);
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Unknown ArvGcAccessMode: " << access;
  }
}

absl::StatusOr<CameraSettingProperties>
GenicamImageSource::ReadCameraSettingPropertiesImpl(
    absl::string_view name) const {
  INTR_RETURN_IF_ERROR(GetFaultsStatus());

  // HDR parameters need special handling since they are private to this class
  // and are not propagated to Aravis.
  if (SoftwareHdr::CanHandleCameraSetting(name)) {
    if (!software_hdr_.has_value()) {
      return intrinsic::FailedPreconditionErrorBuilder()
             << "Software HDR is unavailable.";
    }
    return software_hdr_->ReadCameraSettingProperties(name);
  }

  absl::MutexLock lock(camera_mutex_);
  GError* error = nullptr;

  // Different camera vendors have different ways to get the exposure time,
  // gain, and frame rate range, use aravis to handle this.
  if (name == genicam::kExposureTime) {
    return CameraSettingProperties{
        .name = std::string(name),
        .properties =
            exposure_time_props_.has_value()
                ? CameraSettingProperties::Properties{*exposure_time_props_}
                : CameraSettingProperties::Properties{std::monostate()}};
  } else if (name == genicam::kGain) {
    return CameraSettingProperties{
        .name = std::string(name),
        .properties =
            gain_props_.has_value()
                ? CameraSettingProperties::Properties{*gain_props_}
                : CameraSettingProperties::Properties{std::monostate()}};
  } else if (ParameterControlsFrameRate(name)) {
    double min_frame_rate = std::numeric_limits<double>::quiet_NaN();
    double max_frame_rate = std::numeric_limits<double>::quiet_NaN();
    arv_camera_get_frame_rate_bounds(camera_.get(), &min_frame_rate,
                                     &max_frame_rate, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    if (std::isnan(min_frame_rate) || std::isnan(max_frame_rate)) {
      return intrinsic::InternalErrorBuilder()
             << "Retrieving frame rate bounds failed.";
    }
    return CameraSettingProperties{
        .name = std::string(name),
        .properties = CameraSettingProperties::Float{
            .range = CameraSettingProperties::Float::Range{
                .minimum = min_frame_rate, .maximum = max_frame_rate}}};
  } else if (name == genicam::kSensorPixelWidth ||
             name == genicam::kSensorPixelHeight) {
    std::optional<double> sensor_pixel_size = SensorPixelSize(
        GetVendorName(camera_.get()), GetModelName(camera_.get()));
    if (sensor_pixel_size.has_value()) {
      return CameraSettingProperties{
          .name = std::string(name),
          .properties = CameraSettingProperties::Float{
              .range =
                  CameraSettingProperties::Float::Range{
                      .minimum = *sensor_pixel_size,
                      .maximum = *sensor_pixel_size},
              .unit = "μm"}};
    }
  }

  INTR_ASSIGN_OR_RETURN(ArvGcFeatureNode* const arv_feature_node,
                        GetFeatureNode(name, camera_.get()));
  // We need to check ARV_IS_GC_ENUMERATION before ARV_IS_GC_INTEGER since an
  // enum is actually an integer.
  if (ARV_IS_GC_ENUMERATION(arv_feature_node)) {
    guint num_enum_values = 0;
    const char** enum_values = arv_gc_enumeration_dup_available_string_values(
        reinterpret_cast<ArvGcEnumeration*>(arv_feature_node), &num_enum_values,
        &error);
    const absl::Cleanup on_return = [&] { g_free(enum_values); };
    if (absl::Status status = ValidateAndNullArvError(error); status.ok()) {
      std::vector<std::string> values;
      values.reserve(num_enum_values);
      for (guint i = 0; i < num_enum_values; ++i) {
        values.push_back(enum_values[i]);
      }
      return CameraSettingProperties{
          .name = std::string(name),
          .properties = CameraSettingProperties::Enumeration{
              .values = std::move(values)}};
    } else {
      LOG(WARNING)
          << absl::StrFormat(
                 "Failed to read enum values of feature '%s'. Status: ", name)
          << status;
    }
  } else if (ARV_IS_GC_INTEGER(arv_feature_node)) {
    absl::Status status = absl::OkStatus();
    auto* gc_integer = reinterpret_cast<ArvGcInteger*>(arv_feature_node);
    const int64_t min_value = arv_gc_integer_get_min(gc_integer, &error);
    status.Update(ValidateAndNullArvError(error));
    const int64_t max_value = arv_gc_integer_get_max(gc_integer, &error);
    status.Update(ValidateAndNullArvError(error));
    CameraSettingProperties::Integer integer_properties;
    if (status.ok()) {
      integer_properties.range = {.minimum = min_value, .maximum = max_value};
    } else {
      LOG(WARNING)
          << absl::StrFormat(
                 "Failed to read integer range of feature '%s'. Status: ", name)
          << status;
    }
    const int64_t integer_increment =
        arv_gc_integer_get_inc(gc_integer, &error);
    status.Update(ValidateAndNullArvError(error));
    if (status.ok()) {
      integer_properties.increment = integer_increment;
    }
    const char* unit = arv_gc_integer_get_unit(gc_integer);
    if (unit != nullptr) {
      integer_properties.unit = unit;
    } else {
      VLOG(1) << absl::StrFormat("Failed to read integer unit of feature '%s'.",
                                 name);
    }
    return CameraSettingProperties{.name = std::string(name),
                                   .properties = std::move(integer_properties)};
  } else if (ARV_IS_GC_FLOAT(arv_feature_node)) {
    absl::Status status = absl::OkStatus();
    auto* gc_float = reinterpret_cast<ArvGcFloat*>(arv_feature_node);
    const double min_value = arv_gc_float_get_min(gc_float, &error);
    status.Update(ValidateAndNullArvError(error));
    const double max_value = arv_gc_float_get_max(gc_float, &error);
    status.Update(ValidateAndNullArvError(error));
    CameraSettingProperties::Float float_properties;
    if (status.ok()) {
      float_properties.range = {.minimum = min_value, .maximum = max_value};
    } else {
      LOG(WARNING)
          << absl::StrFormat(
                 "Failed to read float range of feature '%s'. Status: ", name)
          << status;
    }
    const double float_increment = arv_gc_float_get_inc(gc_float, &error);
    status.Update(ValidateAndNullArvError(error));
    if (status.ok()) {
      float_properties.increment = float_increment;
    }
    const char* unit = arv_gc_float_get_unit(gc_float);
    if (unit != nullptr) {
      float_properties.unit = unit;
    } else {
      LOG(WARNING) << absl::StrFormat(
          "Failed to read float unit of feature '%s'.", name);
    }
    return CameraSettingProperties{.name = std::string(name),
                                   .properties = std::move(float_properties)};
  }
  return CameraSettingProperties{.name = std::string(name)};
}

absl::StatusOr<CameraSetting> GenicamImageSource::ReadCameraSettingImpl(
    absl::string_view name) const {
  INTR_RETURN_IF_ERROR(GetFaultsStatus());

  // HDR parameters need special handling since they are private to this class
  // and are not propagated to Aravis.
  if (SoftwareHdr::CanHandleCameraSetting(name)) {
    if (!software_hdr_.has_value()) {
      return intrinsic::FailedPreconditionErrorBuilder()
             << "Software HDR is unavailable.";
    }
    return software_hdr_->ReadCameraSetting(name);
  }

  absl::MutexLock lock(camera_mutex_);
  GError* error = nullptr;

  // Different camera vendors have different ways to get the exposure time,
  // gain, and frame rate, use aravis to handle this.
  if (name == genicam::kExposureTime) {
    const double exposure_time =
        arv_camera_get_exposure_time(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, exposure_time);
  } else if (name == genicam::kGain) {
    const double gain = arv_camera_get_gain(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, gain);
  } else if (ParameterControlsFrameRate(name)) {
    const double frame_rate = arv_camera_get_frame_rate(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, frame_rate);
  } else if (ParameterControlsFrameRateEnable(name)) {
    const bool frame_rate_enable =
        arv_camera_get_frame_rate_enable(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, frame_rate_enable);
  } else if (name == genicam::kGevSCPSPacketSize &&
             arv_camera_is_gv_device(camera_.get())) {
    const guint packet_size =
        arv_camera_gv_get_packet_size(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, packet_size);
  } else if (name == genicam::kSensorPixelWidth ||
             name == genicam::kSensorPixelHeight) {
    std::optional<double> sensor_pixel_size = SensorPixelSize(
        GetVendorName(camera_.get()), GetModelName(camera_.get()));
    if (sensor_pixel_size.has_value()) {
      return CreateCameraSetting(name, *sensor_pixel_size);
    }
  }

  INTR_ASSIGN_OR_RETURN(ArvGcFeatureNode * arv_feature_node,
                        GetFeatureNode(name, camera_.get()));
  // We need to check ARV_IS_GC_ENUMERATION before ARV_IS_GC_INTEGER since an
  // enum is actually an integer.
  if (ARV_IS_GC_ENUMERATION(arv_feature_node)) {
    const char* enum_value = arv_gc_enumeration_get_string_value(
        reinterpret_cast<ArvGcEnumeration*>(arv_feature_node), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateEnumCameraSetting(name, enum_value);
  } else if (ARV_IS_GC_INTEGER(arv_feature_node)) {
    auto* gc_integer = reinterpret_cast<ArvGcInteger*>(arv_feature_node);
    const int64_t integer_value = arv_gc_integer_get_value(gc_integer, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, integer_value);
  } else if (ARV_IS_GC_FLOAT(arv_feature_node)) {
    auto* gc_float = reinterpret_cast<ArvGcFloat*>(arv_feature_node);
    const double float_value = arv_gc_float_get_value(gc_float, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, float_value);
  } else if (ARV_IS_GC_BOOLEAN(arv_feature_node)) {
    const bool bool_value =
        (arv_gc_boolean_get_value(
             reinterpret_cast<ArvGcBoolean*>(arv_feature_node), &error) == 1);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, bool_value);
  } else if (ARV_IS_GC_STRING(arv_feature_node)) {
    const char* string_value = arv_gc_string_get_value(
        reinterpret_cast<ArvGcString*>(arv_feature_node), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    return CreateCameraSetting(name, string_value);
  } else if (ARV_IS_GC_COMMAND(arv_feature_node)) {
    return CreateCommandCameraSetting(name);
  }

  return ::intrinsic::InvalidArgumentErrorBuilder()
         << "Requested settings of an unsupported type.";
}

absl::Status GenicamImageSource::UpdateCameraSettingImpl(
    const CameraSetting& camera_setting) {
  {
    // We do not allow streaming and setting updates at the same time. This can
    // be problematic if the camera parameters are not the same. See the TDD for
    // more details: go/intrinsic-camera-streaming-design
    absl::MutexLock lock(mutex_);
    if (streaming_state_.has_value()) {
      return absl::FailedPreconditionError(
          "We do not allow updating the camera setting while there is a stream "
          "running.");
    }
  }

  return UpdateCameraSettingInternal(camera_setting);
}

absl::Status GenicamImageSource::UpdateCameraSettingInternal(
    const CameraSetting& camera_setting) {
  if (camera_setting.name.empty()) {
    return absl::InvalidArgumentError("Camera setting 'name' cannot be empty.");
  }
  INTR_RETURN_IF_ERROR(GetFaultsStatus());

  return RetryOnPermissionDeniedOrTimeoutOrBusy([this, &camera_setting] {
    return UpdateCameraSettingOnce(camera_setting);
  });
}

absl::Status GenicamImageSource::UpdateCameraSettingOnce(
    const CameraSetting& camera_setting) {
  VLOG(1) << "Setting camera parameter: " << camera_setting;

  // HDR parameters need special handling since they are private to this class
  // and are not propagated to Aravis.
  if (SoftwareHdr::CanHandleCameraSetting(camera_setting.name)) {
    if (!software_hdr_.has_value()) {
      return intrinsic::FailedPreconditionErrorBuilder()
             << "Software HDR is unavailable.";
    }
    return software_hdr_->UpdateCameraSetting(camera_setting);
  }

  if (ParameterRequiresAcquisitionRestart(camera_setting.name)) {
    // Stops the camera acquisition while keeping the current stream allocated.
    INTR_RETURN_IF_ERROR(StopAsyncAcquisition());
  }

  // Temporarily stop the thread responsible for streaming, so we can quickly
  // change our parameter.
  INTR_RETURN_IF_ERROR(PauseStream());
  absl::Status status = UpdateCameraSettingOnceWithPausedStream(camera_setting);
  status.Update(ResumeStream());
  return status;
}

absl::Status GenicamImageSource::UpdateCameraSettingOnceWithPausedStream(
    const CameraSetting& camera_setting) {
  GError* error = nullptr;
  INTR_RETURN_IF_ERROR(std::visit(
      absl::Overload{
          [&](const intrinsic::perception::CameraSetting::Integer& value)
              -> absl::Status {
            INTR_ASSIGN_OR_RETURN(const int64_t integer_value,
                                  GetValue<int64_t>(camera_setting));
            if (camera_setting.name == genicam::kGevSCPSPacketSize &&
                arv_camera_is_gv_device(camera_.get())) {
              arv_camera_gv_set_packet_size(camera_.get(), integer_value,
                                            &error);
            } else {
              arv_camera_set_integer(camera_.get(), camera_setting.name.c_str(),
                                     integer_value, &error);
            }
            return ValidateAndNullArvError(error);
          },
          [&](const intrinsic::perception::CameraSetting::Float& value)
              -> absl::Status {
            INTR_ASSIGN_OR_RETURN(const double float_value,
                                  GetValue<double>(camera_setting));
            // Different camera vendors have different ways to set the exposure
            // time, gain, and frame rate, use aravis to handle this.
            if (camera_setting.name == genicam::kExposureTime) {
              // Some cameras are very picky about how to apply the exposure
              // time. For example, the Basler acA1920-25gc allows to set any
              // exposure time, but only the ones that are multiples of the
              // increment are actually applied.
              return SetExposureTime(camera_.get(),
                                     absl::Microseconds(float_value),
                                     exposure_time_props_);
            } else if (camera_setting.name == genicam::kGain) {
              arv_camera_set_gain(
                  camera_.get(),
                  gain_props_.has_value()
                      ? ClampAndRoundToIncrement(float_value, *gain_props_)
                      : float_value,
                  &error);
            } else if (ParameterControlsFrameRate(camera_setting.name)) {
              if (GetVendorName(camera_.get()) == kAlliedVision &&
                  absl::StartsWith(GetModelName(camera_.get()), kAlvium) &&
                  GetFirmwareVersion(camera_.get()) >
                      AlliedVisionAlviumMaximumFirmwareVersion()) {
                return FailedPreconditionErrorBuilder()
                       << "The " << kAlliedVision << " " << kAlvium
                       << " camera in use has a firmware bug for versions "
                          "greater than "
                       << AlliedVisionAlviumMaximumFirmwareVersion()
                       << " when setting the frame rate. Please downgrade "
                          "your version.";
              }
              arv_camera_set_frame_rate(camera_.get(), float_value, &error);
            } else {
              arv_camera_set_float(camera_.get(), camera_setting.name.c_str(),
                                   float_value, &error);
            }
            return ValidateAndNullArvError(error);
          },
          [&](const intrinsic::perception::CameraSetting::Boolean& value)
              -> absl::Status {
            INTR_ASSIGN_OR_RETURN(const bool bool_value,
                                  GetValue<bool>(camera_setting));
            if (ParameterControlsFrameRateEnable(camera_setting.name)) {
              arv_camera_set_frame_rate_enable(camera_.get(), bool_value,
                                               &error);
            } else {
              arv_camera_set_boolean(camera_.get(), camera_setting.name.c_str(),
                                     bool_value ? 1 : 0, &error);
            }
            return ValidateAndNullArvError(error);
          },
          [&](const intrinsic::perception::CameraSetting::String& value)
              -> absl::Status {
            INTR_ASSIGN_OR_RETURN(const std::string string_value,
                                  GetValue<std::string>(camera_setting));
            arv_camera_set_string(camera_.get(), camera_setting.name.c_str(),
                                  string_value.c_str(), &error);
            return ValidateAndNullArvError(error);
          },
          [&](const intrinsic::perception::CameraSetting::Enumeration& value)
              -> absl::Status {
            INTR_ASSIGN_OR_RETURN(const std::string enum_value,
                                  GetEnumerationValue(camera_setting));
            if (camera_setting.name == genicam::kTriggerMode) {
              if (enum_value == "On") {
                // Make sure that the camera is in software trigger mode.
                // Otherwise the option "On" might not be available. See
                // b/341888014.
                return SetSoftwareTrigger(camera_.get());
              } else if (enum_value == "Off") {
                return SetFreeRunningTrigger(camera_.get());
              }
            }
            // In Aravis, enumerations are also set through
            // arv_camera_set_string()
            arv_camera_set_string(camera_.get(), camera_setting.name.c_str(),
                                  enum_value.c_str(), &error);
            return ValidateAndNullArvError(error);
          },
          [&](const intrinsic::perception::CameraSetting::Command&)
              -> absl::Status {
            // A reset requires special treatment, as the camera needs to be
            // rediscovered.
            if (camera_setting.name == genicam::kDeviceReset) {
              INTR_RETURN_IF_ERROR(Init(/*requires_device_reset=*/true));
            } else {
              arv_camera_execute_command(camera_.get(),
                                         camera_setting.name.c_str(), &error);
            }
            return ValidateAndNullArvError(error);
          },
          [&](const std::monostate&) -> absl::Status {
            return ::intrinsic::InvalidArgumentErrorBuilder()
                   << "Camera setting 'value' is not specified.";
          }},
      camera_setting.value));

  INTR_ASSIGN_OR_RETURN(
      bool use_software_trigger,
      intrinsic::perception::UsesSoftwareTrigger(this->camera_.get()));
  if (this->use_software_trigger_ != use_software_trigger) {
    this->use_software_trigger_ = use_software_trigger;
    LOG(INFO) << "Software trigger: " << (use_software_trigger ? "On" : "Off");
  }
  const absl::StatusOr<CameraSetting> component_id_value =
      ReadCameraSettingImpl(genicam::kComponentIDValue);
  int64_t sensor_id = kFallbackSensorId;
  if (component_id_value.ok()) {
    INTR_ASSIGN_OR_RETURN(sensor_id, GetValue<int64_t>(*component_id_value));
  }

  absl::MutexLock lock(mutex_);
  if (camera_setting.name == genicam::kGevSCPSPacketSize) {
    INTR_ASSIGN_OR_RETURN(this->state_.packet_size,
                          GetValue<int>(camera_setting));
    LOG(INFO) << "Packet size: " << this->state_.packet_size;
  } else if (camera_setting.name == genicam::kWidth) {
    INTR_ASSIGN_OR_RETURN(this->state_.dimensions_by_sensor_id[sensor_id].cols,
                          GetValue<int32_t>(camera_setting));
  } else if (camera_setting.name == genicam::kHeight) {
    INTR_ASSIGN_OR_RETURN(this->state_.dimensions_by_sensor_id[sensor_id].rows,
                          GetValue<int32_t>(camera_setting));
  } else if (camera_setting.name == genicam::kPixelFormat) {
    ArvPixelFormat pixel_format =
        arv_camera_get_pixel_format(camera_.get(), &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    this->state_.pixel_format_by_sensor_id[sensor_id] = pixel_format;
  } else if (camera_setting.name == genicam::kComponentEnable) {
    INTR_ASSIGN_OR_RETURN(const bool enable, GetValue<bool>(camera_setting));
    this->state_.disabled_by_sensor_id[sensor_id] = !enable;
  }
  return absl::OkStatus();
}

void GenicamImageSource::OnNewBuffer(ArvStream* arv_stream, void* data) {
  QCHECK_NE(arv_stream, nullptr);
  QCHECK_NE(data, nullptr);

  auto onb_start = absl::Now();

  // A handle to the instance of the camera which registered this callback is
  // stored in 'data'. We retrieve it here to be able to transfer the
  // resulting buffer to the original camera (which belongs to a separate
  // thread).
  auto* camera = static_cast<GenicamImageSource*>(data);

  std::shared_ptr<opentelemetry::trace::Span> parent_span;
  {
    absl::MutexLock lock(camera->mutex_);
    if (camera->acquisition_span_) {
      parent_span = camera->acquisition_span_->span();
    }
    // Mark the end of the acquisition span.
    camera->acquisition_span_.reset();
  }

  // These variables are pre-declared as they play a role during 'on_return'.
  absl::Status status = absl::OkStatus();
  ArvBuffer* buffer = nullptr;

  // This helper prevents that we ever return without some crucial cleanup
  // steps. These are:
  //   1. Ensure that buffers taken from the camera are given back.
  //   2. Ensure that status messages are reported to the caller.
  //   3. Notify the caller that the buffer handling has finished.
  const absl::Cleanup on_return = [&] {
    if (buffer != nullptr) {
      // We can now give back the buffer to the camera queue after copying all
      // image data from it.
      arv_stream_push_buffer(camera->stream_.get(), buffer);
    }
    absl::MutexLock lock(camera->mutex_);
    camera->status_ = status;
    camera->buffer_callback_time_ = onb_start;

    // Only if we are not currently streaming (or while `StartStream` is still
    // converging auto settings) do we notify the capture callback that we have
    // handled the buffer. Once streaming is active, we handle the processing
    // directly in this callback.
    //
    // Since we do not allow capture calls and streaming calls to be
    // interleaved, we can avoid an extra copy and consume the buffer directly.
    // See go/intrinsic-camera-streaming-design for more details.
    camera->new_buffer_handled_ = !camera->streaming_state_.has_value() ||
                                  !camera->streaming_state_->is_active;

    VLOG(1) << "Buffer conversion took: " << absl::Now() - onb_start;
    if (ABSL_VLOG_IS_ON(1) && camera->streaming_state_.has_value() &&
        !status.ok()) {
      LOG(ERROR) << "Error while streaming: " << status;
    }
  };

  buffer = arv_stream_pop_buffer(camera->stream_.get());
  VLOG(1) << "Retrieved buffer from stream.";
  if (buffer == nullptr) {
    status = absl::DataLossError("No buffer received.");
    return;
  }

  const auto buffer_status = arv_buffer_get_status(buffer);
  if (buffer_status == ARV_BUFFER_STATUS_SUCCESS) {
    VLOG(1) << "Buffer status is good. Retrieving frame.";
    const int64_t frame_id = arv_buffer_get_frame_id(buffer);
    VLOG(1) << "New frame received: " << frame_id << ", TID: " << GetTID();

    // Within this callback it is important to avoid concurrent unlocked access
    // to `ArvCamera` across threads, see also
    // https://aravisproject.github.io/aravis/aravis-stable/thread-safety.html.
    // Frame decoding uses cached fallback parameters rather than querying
    // `camera_`, and any `camera_` access during streaming (such as updating
    // `ExposureTime` in `ProcessStreamingHdrFrame` or reading camera settings)
    // is serialized under `camera_mutex_`.
    camera->mutex_.lock();
    const absl::flat_hash_map<int64_t, Dimensions> fallback_dimensions =
        camera->state_.dimensions_by_sensor_id;
    const absl::flat_hash_map<int64_t, ArvPixelFormat> fallback_pixel_format =
        camera->state_.pixel_format_by_sensor_id;
    const std::optional<CaptureArgs> streaming_params =
        (camera->streaming_state_.has_value() &&
         camera->streaming_state_->is_active)
            ? std::make_optional(camera->streaming_state_->params)
            : std::nullopt;
    const bool should_use_stream_callbacks = streaming_params.has_value();
    camera->mutex_.unlock();
    // Create a new capture result. This will be moved from the receiving thread
    // to the thread owning the camera and later from there to the client. This
    // is important as we will move the 'camera->capture_result_' to the client
    // to prevent an additional copy.
    absl::StatusOr<CaptureResult> capture_result;
    {
      const stats::ScopedSpan span("GenicamImageSource::ToCaptureResult",
                                   parent_span);
      capture_result = camera->buffer_helper_.ToCaptureResult(
          buffer, camera->factory_camera_params_by_sensor_id_,
          camera->camera_t_sensor_by_sensor_id_, fallback_dimensions,
          fallback_pixel_format, camera->distance_scale_);
    }
    status = capture_result.status();

    // If we failed to successfully copy the image into the buffer, we can
    // return early here.
    if (!status.ok()) {
      return;
    }

    // If we are in the middle of a streaming session, we need to notify the
    // client about the new frame. We also consume the buffer directly here to
    // prevent an additional copy as we know there is no capture callback
    // waiting for this result.
    if (should_use_stream_callbacks) {
      if (!streaming_params.has_value()) {
        LOG(ERROR) << "Skipping callbacks! No streaming params found.";
        return;
      }

      absl::StatusOr<std::optional<CaptureResult>> maybe_hdr_result =
          camera->ProcessStreamingHdrFrame(
              *std::move(capture_result),
              camera->buffer_helper_.GetExposureTime(buffer), frame_id);
      status = maybe_hdr_result.status();
      if (!status.ok()) {
        LOG(ERROR)
            << "Skipping callbacks! Failed to process streaming HDR frame: "
            << status;
        return;
      }
      if (!maybe_hdr_result->has_value()) {
        return;
      }

      // The post processing needs to be applied to the resulting images.
      auto callback_capture_result = camera->PostCaptureImplProcessing(
          std::move(maybe_hdr_result->value()), streaming_params.value());
      if (!callback_capture_result.ok()) {
        LOG(ERROR)
            << "Skipping callbacks! Failed to undistort image for callbacks: "
            << callback_capture_result.status();
      } else {
        absl::MutexLock lock(camera->callback_mutex_);
        for (auto& [_, callback] : camera->callbacks_) {
          if (absl::Status status = callback(*callback_capture_result);
              !status.ok()) {
            LOG(ERROR) << "Callback returned error status: " << status;
          }
        }
      }
    } else {
      // If we are not in the middle of a streaming session we can return the
      // capture result back to a waiting thread.
      absl::MutexLock lock(camera->mutex_);
      camera->capture_result_ = {
          .capture_result = *std::move(capture_result),
          .exposure_time = camera->buffer_helper_.GetExposureTime(buffer),
      };
    }
  } else {
    VLOG(1) << "Buffer status is bad. Collecting error information.";
    std::string network_issues;
    absl::MutexLock lock(camera->mutex_);
    const int packet_size = camera->state_.packet_size;
    if (packet_size > 0 && packet_size < kMinimumJumboPacketSize) {
      network_issues +=
          "The current packet size of " + std::to_string(packet_size) +
          " bytes may not be ideal for your camera. In order to support jumbo "
          "frames, please set the MTU of all involved network devices to " +
          std::to_string(kDesiredJumboPacketSize) + " (at least " +
          std::to_string(kMinimumJumboPacketSize) +
          ") bytes. If you keep seeing this message, manually configure your "
          "camera to use an identical packet size by setting the "
          "`GevSCPSPacketSize` accordingly.\n";
    }
    network_issues +=
        "To reduce network load you can try to change the `PixelFormat` of "
        "your camera to a compressed format such as `BayerRG8`, decrease its "
        "`AcquisitionFrameRate`, or decrease its `Width`/`Height`.\nFor USB "
        "cameras, try increasing the USBFS limit.";

    switch (buffer_status) {
      case ARV_BUFFER_STATUS_TIMEOUT:
        status = absl::DeadlineExceededError(absl::StrCat(
            "Failed to receive all packages before timeout. These timeouts "
            "typically indicate network issues.\n",
            network_issues));
        break;
      case ARV_BUFFER_STATUS_MISSING_PACKETS:
        status = absl::DataLossError(absl::StrCat(
            "Received incomplete buffer with missing packets. Missing packets "
            "typically indicate network issues.\n",
            network_issues));
        break;
      case ARV_BUFFER_STATUS_SIZE_MISMATCH:
        status = absl::InternalError(absl::StrCat(
            "Received buffer does not match pre-allocated buffer. This is "
            "either caused by a misconfigured buffer or due to package loss.\n",
            network_issues));
        break;
      case ARV_BUFFER_STATUS_PAYLOAD_NOT_SUPPORTED:
        status = absl::InternalError(
            absl::StrCat("The number of expected packets could not be computed "
                         "from the received buffer.\n",
                         network_issues));
        break;
      default:
        status = absl::InternalError(absl::StrCat(
            "Unknown error in buffer status (error ID ", buffer_status, ")."));
        break;
    }
  }
}

absl::StatusOr<std::optional<CaptureResult>>
GenicamImageSource::ProcessStreamingHdrFrame(
    CaptureResult capture_result,
    std::optional<absl::Duration> received_exposure, int64_t frame_id) {
  std::vector<CaptureResult> ldr_frames_to_fuse;
  HdrOperator hdr_operator = HdrOperator::kMertens;
  {
    absl::MutexLock lock(mutex_);
    if (!streaming_state_.has_value() || !streaming_state_->is_active) {
      return std::nullopt;
    }
    if (!streaming_state_->hdr.has_value()) {
      return std::move(capture_result);
    }
    StreamingHdrState& hdr = *streaming_state_->hdr;
    const int num_exposures = hdr.exposure_times.size();
    const int current_idx = hdr.ldr_frames.size();
    const absl::Duration expected_exposure = hdr.exposure_times[current_idx];
    ++hdr.num_tries;
    if (const absl::Status verify_status = VerifyExposureTime(
            received_exposure, expected_exposure, hdr.num_tries);
        !verify_status.ok()) {
      if (hdr.num_tries >= kMaxNumFramesToConverge) {
        LOG_EVERY_N_SEC(WARNING, 5)
            << "Repeated exposure mismatch during streaming HDR after "
            << hdr.num_tries << " frames (" << verify_status.message()
            << "); re-applying target exposure " << expected_exposure << ".";
        {
          absl::MutexLock camera_lock(camera_mutex_);
          INTR_RETURN_IF_ERROR(SetExposureTime(camera_.get(), expected_exposure,
                                               exposure_time_props_));
        }
        hdr.num_tries = 0;
      } else {
        VLOG(1) << "Skipping frame " << frame_id
                << " during streaming HDR: " << verify_status.message();
      }
      return std::nullopt;
    }
    const int next_idx = (current_idx + 1) % num_exposures;
    {
      absl::MutexLock camera_lock(camera_mutex_);
      INTR_RETURN_IF_ERROR(SetExposureTime(
          camera_.get(), hdr.exposure_times[next_idx], exposure_time_props_));
    }
    hdr.num_tries = 0;
    hdr.ldr_frames.push_back(std::move(capture_result));
    if (hdr.ldr_frames.size() < hdr.exposure_times.size()) {
      return std::nullopt;
    }
    ldr_frames_to_fuse = std::move(hdr.ldr_frames);
    hdr.ldr_frames.clear();
    hdr.ldr_frames.reserve(hdr.exposure_times.size());
    hdr_operator = hdr.hdr_operator;
  }
  return FuseLdrCaptureResults(ldr_frames_to_fuse, hdr_operator);
}

void GenicamImageSource::OnControlLost(ArvDevice* arv_device, void* data) {
  LOG(ERROR) << "Camera control lost.";
  // A handle to the instance of the camera which registered this callback is
  // stored in 'data'.
  auto* camera = static_cast<GenicamImageSource*>(data);
  absl::MutexLock lock(camera->mutex_);
  camera->state_.lost_control = true;
}

absl::Status GenicamImageSource::GetFaultsStatus() const {
  absl::MutexLock lock(mutex_);
  return state_.lost_control ? absl::DataLossError("Camera control lost.")
                             : absl::OkStatus();
}

absl::Status GenicamImageSource::ClearFaults() {
  {
    absl::MutexLock lock(mutex_);
    if (!state_.lost_control) return absl::OkStatus();
    // As the lost control flag is queried recursively during camera
    // initialization, we already need to disable it here.
    state_.lost_control = false;
  }
  camera_.reset();
  absl::Status status = Init(/*requires_device_reset=*/false);
  if (!status.ok()) {
    status = intrinsic::DataLossErrorBuilder()
             << "Camera control lost: " << status;
    absl::MutexLock lock(mutex_);
    state_.lost_control = true;
  }
  return status;
}

absl::Status GenicamImageSource::StartStream(const CaptureArgs& params) {
  {
    absl::MutexLock lock(mutex_);
    if (streaming_state_.has_value()) {
      return absl::FailedPreconditionError(
          "Stream is already running. Call StopStream() first.");
    }
    streaming_state_ = StreamingState{.params = params};
  }
  std::optional<StreamingHdrState> hdr_state;
  bool stream_started = false;
  const absl::Cleanup cleanup_on_error = [&] {
    if (!stream_started) {
      {
        absl::MutexLock lock(mutex_);
        if (!streaming_state_.has_value()) {
          streaming_state_ = StreamingState{.params = params};
        }
        if (!streaming_state_->hdr.has_value()) {
          streaming_state_->hdr = std::move(hdr_state);
        }
      }
      if (const absl::Status status = StopStream(); !status.ok()) {
        LOG(ERROR) << "Failed to clean up after stream start failure: "
                   << status;
      }
    }
  };

  // Apply all camera settings once before we start the capture loop.
  for (const CameraSetting& camera_setting :
       params.camera_config.camera_settings) {
    if (std::holds_alternative<CameraSetting::Command>(camera_setting.value)) {
      INTR_RETURN_IF_ERROR(UpdateCameraSettingInternal(camera_setting));
    } else if (const absl::StatusOr<CameraSetting> current_setting =
                   ReadCameraSetting(camera_setting.name);
               !current_setting.ok() || *current_setting != camera_setting) {
      INTR_RETURN_IF_ERROR(UpdateCameraSettingInternal(camera_setting));
    }
  }

  INTR_RETURN_IF_ERROR(GetFaultsStatus());
  INTR_RETURN_IF_ERROR(StopAsyncAcquisition());
  {
    absl::MutexLock lock(camera_mutex_);
    INTR_RETURN_IF_ERROR(ConfigurePtpSyncAndStreamingTrigger(camera_.get()));
  }

  use_software_trigger_ = false;

  if (software_hdr_.has_value() && software_hdr_->IsEnabled()) {
    hdr_state.emplace();
    // Converge any active auto settings (`Once` or `Continuous`) before
    // computing the HDR exposure brackets, so that:
    // 1. Any `Once` or `Continuous` auto settings settle on a converged base
    //    `ExposureTime`, `Gain`, and `BalanceRatio` before HDR exposure
    //    bracketing begins.
    // 2. All auto settings are in `Off` mode during HDR streaming so they do
    //    not conflict with alternating exposure times.
    // 3. Only `Continuous` auto settings are recorded in
    //    `auto_settings_to_restore` to be restored when `StopStream()` is
    //    called (`Once` settings were genuinely applied and converged to
    //    `Off`).
    INTR_ASSIGN_OR_RETURN(const std::vector<CameraSetting> active_auto_settings,
                          GetActiveAutoSettings());
    if (!active_auto_settings.empty()) {
      INTR_RETURN_IF_ERROR(
          ConvergeAutoSettings(
              active_auto_settings,
              std::max(params.timeout, kMinStreamAutoConvergenceTimeout),
              hdr_state->auto_settings_to_restore)
              .status());
    }

    INTR_ASSIGN_OR_RETURN(const std::vector<absl::Duration> hdr_exposure_times,
                          GetHdrExposureTimes());
    if (!hdr_exposure_times.empty()) {
      INTR_ASSIGN_OR_RETURN(hdr_state->hdr_operator,
                            software_hdr_->HdrOperator());
      {
        absl::MutexLock lock(camera_mutex_);
        INTR_RETURN_IF_ERROR(SetExposureTime(
            camera_.get(), hdr_exposure_times.front(), exposure_time_props_));
      }
      // Initialize `num_tries = 1` because `SetExposureTime` above has already
      // applied the first bracket exposure before streaming begins, so the very
      // first streamed frame already has that exposure time (unless an earlier
      // frame from `ConvergeAutoSettings` was already in flight, which will be
      // skipped if `ChunkExposureTime` is available). Subsequent bracket
      // transitions in `ProcessStreamingHdrFrame` reset `num_tries = 0` so
      // cameras without `ChunkExposureTime` skip 1 in-flight frame
      // (`num_tries == 1`) before accepting the next frame (`num_tries == 2`).
      hdr_state->exposure_times = hdr_exposure_times;
      hdr_state->num_tries = 1;
      hdr_state->ldr_frames.reserve(hdr_exposure_times.size());
    } else {
      if (!hdr_state->auto_settings_to_restore.empty()) {
        INTR_RETURN_IF_ERROR(
            RestoreAutoSettings(hdr_state->auto_settings_to_restore));
      }
      hdr_state = std::nullopt;
    }
  }

  {
    absl::MutexLock lock(mutex_);
    if (!streaming_state_.has_value()) {
      return absl::CancelledError(
          "Stream start was interrupted by StopStream.");
    }
    streaming_state_->is_active = true;
    streaming_state_->hdr = std::move(hdr_state);
    new_buffer_handled_ = false;
    capture_result_ = {};
  }
  INTR_RETURN_IF_ERROR(StartAsyncAcquisition());
  stream_started = true;
  return absl::OkStatus();
}

absl::Status GenicamImageSource::StopStream() {
  std::optional<absl::Duration> base_exposure_time;
  std::vector<CameraSetting> auto_settings_to_restore;
  {
    absl::MutexLock lock(mutex_);
    if (!streaming_state_.has_value()) {
      return absl::OkStatus();
    }
    streaming_state_->is_active = false;
    if (streaming_state_->hdr.has_value()) {
      if (!streaming_state_->hdr->exposure_times.empty()) {
        base_exposure_time = streaming_state_->hdr->exposure_times.front();
      }
      auto_settings_to_restore =
          std::move(streaming_state_->hdr->auto_settings_to_restore);
    }
  }

  absl::Status status = StopAsyncAcquisition();
  if (base_exposure_time.has_value()) {
    absl::MutexLock lock(camera_mutex_);
    status.Update(SetExposureTime(camera_.get(), *base_exposure_time,
                                  exposure_time_props_));
  }
  status.Update(RestoreAutoSettings(auto_settings_to_restore));
  {
    absl::MutexLock lock(camera_mutex_);
    if (const absl::Status trigger_status = SetSoftwareTrigger(camera_.get());
        trigger_status.ok()) {
      use_software_trigger_ = true;
    } else {
      status.Update(trigger_status);
    }
  }
  {
    absl::MutexLock lock(mutex_);
    streaming_state_ = std::nullopt;
    new_buffer_handled_ = false;
    capture_result_ = {};
  }
  return status;
}

absl::Status GenicamImageSource::StartAsyncAcquisition() {
  if (acquisition_state_.is_started) return absl::OkStatus();
  GError* error = nullptr;

  // Here, we are creating Aravis' buffer queue. It is retrieved during frame
  // acquisition and then repurposed once image data has been copied. The
  // buffers depend on the PayloadSize and hence need to be recreated whenever
  // acquisition is restarted and the PayloadSize changed.
  int payload_size = arv_camera_get_payload(camera_.get(), &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (acquisition_state_.payload_size != payload_size) {
    arv_stream_delete_buffers(stream_.get());
    arv_stream_create_buffers(stream_.get(), kNumStreamBuffers, nullptr,
                              nullptr, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    acquisition_state_.payload_size = payload_size;
  }

  arv_camera_start_acquisition(camera_.get(), &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  LOG(INFO) << absl::StrFormat(
      "Started streaming acquisition for camera with ID %s.", device_id_);
  acquisition_state_.is_started = true;
  return absl::OkStatus();
}

absl::Status GenicamImageSource::StopAsyncAcquisition() {
  if (!acquisition_state_.is_started) return absl::OkStatus();
  GError* error = nullptr;
  arv_camera_stop_acquisition(camera_.get(), &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  LOG(INFO) << absl::StrFormat(
      "Stopped streaming acquisition for camera with ID %s.", device_id_);
  acquisition_state_.is_started = false;
  return absl::OkStatus();
}

absl::Status GenicamImageSource::PauseStream() {
  if (!acquisition_state_.is_started) return absl::OkStatus();
  GError* error = nullptr;
  arv_stream_stop_acquisition(stream_.get(), &error);
  return ValidateAndNullArvError(error);
}

absl::Status GenicamImageSource::ResumeStream() {
  if (!acquisition_state_.is_started) return absl::OkStatus();
  GError* error = nullptr;
  arv_stream_start_acquisition(stream_.get(), &error);
  return ValidateAndNullArvError(error);
}

REGISTER_IMAGE_SOURCE(GenicamImageSource, "genicam",
                      GenicamImageSource::Create);
REGISTER_IMAGE_SOURCE(FakeGenicamImageSource, "fake_genicam",
                      GenicamImageSource::CreateFake);

}  // namespace intrinsic::perception
