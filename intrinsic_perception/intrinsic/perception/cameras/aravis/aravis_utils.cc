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

#include "intrinsic/perception/cameras/aravis/aravis_utils.h"

#include <arv.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/call_once.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/aravis/aravis_glib_utils.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/core/range_tools.h"
#include "intrinsic/util/status/annotate.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/deadline_timeout.h"
#include "intrinsic/util/version.h"
#include "magic_enum/magic_enum.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/traits.hpp"
#include "opencv2/imgproc.hpp"

extern "C" void arv_fake_camera_register_resource(void);

namespace intrinsic::perception {
namespace {
// Pixel formats not yet in Aravis.
// Used by Photoneo.
constexpr ArvPixelFormat kPixelFormatConfidence8 = 0x010800c6;
constexpr ArvPixelFormat kPixelFormatCoord3D_A32f = 0x012000bd;
constexpr ArvPixelFormat kPixelFormatCoord3D_B32f = 0x012000be;
constexpr ArvPixelFormat kPixelFormatCoord3D_C32f = 0x012000bf;
// Used e.g. by the Basler a2A1920-51gcPRO.
constexpr ArvPixelFormat kPixelFormatYCbCr422_8 = 0x0210003bu;

const guint32 kChunkParserError = arv_chunk_parser_error_quark();
const guint32 kDeviceError = arv_device_error_quark();
const guint32 kGcError = arv_gc_error_quark();
const guint32 kXmlSchemaError = arv_xml_schema_error_quark();
constexpr std::string_view kFakeCameraDeviceId = "Fake_1";
constexpr std::string_view kFakeCameraInterfaceId = "Fake";

constexpr char kPhotoneo[] = "Photoneo";
constexpr char kPhotoneoDistortionCoefficients[] =
    "CurrentCamera_DistortionCoefficients";
constexpr char kPhotoneoWorldToCameraRotationMatrix[] =
    "CurrentCamera_WorldToCameraRotationMatrix";
constexpr char kPhotoneoWorldToCameraTranslationVector[] =
    "CurrentCamera_WorldToCameraTranslationVector";
constexpr char kPhotoneoOperationMode[] = "OperationMode";
constexpr char kPhotoneoOperationModeScanner[] = "Scanner";

constinit absl::Mutex device_list_mutex(absl::kConstInit);

const Version& PhotoneoMinimumFirmwareVersion() {
  static const absl::NoDestructor<Version> kPhotoneoMinimumFirmwareVersion(
      "1.14.0-alpha13");
  return *kPhotoneoMinimumFirmwareVersion;
}

absl::StatusOr<cv::Mat> OpenCvBufferView(const Dimensions& dimensions,
                                         const void* buffer,
                                         ArvPixelFormat pixel_format) {
  INTR_ASSIGN_OR_RETURN(const int cv_type, CvType(pixel_format));
  return cv::Mat(dimensions.rows, dimensions.cols, cv_type,
                 const_cast<void*>(buffer));
}

bool IsAccessDeniedError(const GError& error) {
  const std::string message = absl::AsciiStrToLower(error.message);
  return absl::StrContains(message, "access-denied") ||
         absl::StrContains(message, "invalid-access");
}

template <typename E>
std::string MessageFromError(const GError& error) {
  return absl::StrCat("Aravis error [",
                      magic_enum::enum_name(static_cast<E>(error.code)),
                      "]: ", error.message);
}

absl::Status StatusFromChunkParserError(const GError& error) {
  std::string msg = MessageFromError<ArvChunkParserError>(error);
  switch (error.code) {
    case ARV_CHUNK_PARSER_ERROR_INVALID_FEATURE_TYPE:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_CHUNK_PARSER_ERROR_BUFFER_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    case ARV_CHUNK_PARSER_ERROR_CHUNK_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    default:
      if (IsAccessDeniedError(error)) {
        return {absl::StatusCode::kPermissionDenied, msg};
      }
      return {absl::StatusCode::kUnimplemented, msg};
  }
}

absl::Status StatusFromDeviceError(const GError& error) {
  std::string msg = MessageFromError<ArvDeviceError>(error);
  switch (error.code) {
    case ARV_DEVICE_ERROR_WRONG_FEATURE:
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_FEATURE_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    case ARV_DEVICE_ERROR_NOT_CONNECTED:
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR:
      if (IsAccessDeniedError(error)) {
        return {absl::StatusCode::kPermissionDenied, msg};
      }
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_TRANSFER_ERROR:
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_TIMEOUT:
      return {absl::StatusCode::kDeadlineExceeded, msg};
    case ARV_DEVICE_ERROR_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    case ARV_DEVICE_ERROR_INVALID_PARAMETER:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_DEVICE_ERROR_GENICAM_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    case ARV_DEVICE_ERROR_NO_STREAM_CHANNEL:
    case ARV_DEVICE_ERROR_STREAM_ERROR:
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_NOT_CONTROLLER:
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_UNKNOWN:
      return {absl::StatusCode::kUnknown, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_NOT_IMPLEMENTED:
      return {absl::StatusCode::kUnimplemented, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_INVALID_PARAMETER:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_INVALID_ADDRESS:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_WRITE_PROTECT:
      return {absl::StatusCode::kPermissionDenied, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_BAD_ALIGNMENT:
      return {absl::StatusCode::kInternal, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_ACCESS_DENIED:
      return {absl::StatusCode::kPermissionDenied, msg};
    case ARV_DEVICE_ERROR_PROTOCOL_ERROR_BUSY:
      return {absl::StatusCode::kUnavailable, msg};
    default:
      if (IsAccessDeniedError(error)) {
        return {absl::StatusCode::kPermissionDenied, msg};
      }
      return {absl::StatusCode::kUnimplemented, msg};
  }
}

absl::Status StatusFromGcError(const GError& error) {
  std::string msg = MessageFromError<ArvGcError>(error);
  switch (error.code) {
    case ARV_GC_ERROR_PROPERTY_NOT_DEFINED:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_PVALUE_NOT_DEFINED:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_INVALID_PVALUE:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_GC_ERROR_EMPTY_ENUMERATION:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_OUT_OF_RANGE:
      return {absl::StatusCode::kOutOfRange, msg};
    case ARV_GC_ERROR_NO_DEVICE_SET:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_NO_EVENT_IMPLEMENTATION:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_NODE_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    case ARV_GC_ERROR_ENUM_ENTRY_NOT_FOUND:
      return {absl::StatusCode::kNotFound, msg};
    case ARV_GC_ERROR_INVALID_LENGTH:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_GC_ERROR_READ_ONLY:
      return {absl::StatusCode::kPermissionDenied, msg};
    case ARV_GC_ERROR_SET_FROM_STRING_UNDEFINED:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_GET_AS_STRING_UNDEFINED:
      return {absl::StatusCode::kInternal, msg};
    case ARV_GC_ERROR_INVALID_BIT_RANGE:
      return {absl::StatusCode::kInvalidArgument, msg};
    case ARV_GC_ERROR_INVALID_SYNTAX:
      return {absl::StatusCode::kInvalidArgument, msg};
    default:
      return {absl::StatusCode::kUnimplemented, msg};
  }
}

absl::Status StatusFromXmlSchemaError(const GError& error) {
  std::string msg = MessageFromError<ArvXmlSchemaError>(error);
  switch (error.code) {
    case ARV_XML_SCHEMA_ERROR_INVALID_STRUCTURE:
      return {absl::StatusCode::kInvalidArgument, msg};
    default:
      return {absl::StatusCode::kUnimplemented, msg};
  }
}

absl::Status StatusFromError(const GError& error) {
  if (error.domain == kChunkParserError) {
    return StatusFromChunkParserError(error);
  } else if (error.domain == kDeviceError) {
    return StatusFromDeviceError(error);
  } else if (error.domain == kGcError) {
    return StatusFromGcError(error);
  } else if (error.domain == kXmlSchemaError) {
    return StatusFromXmlSchemaError(error);
  }
  return {absl::StatusCode::kUnimplemented,
          absl::StrCat("Aravis error: ", error.message)};
}

void Unpack12BitTo8Bit(const uint8_t* src, uint8_t* dst, int num_pixels,
                       bool is_p_format) {
  const int num_pairs = num_pixels / 2;
  if (is_p_format) {
    // GenICam PFNC 12-bit packed ("p") format layout (e.g. Mono12p, Bayer12p):
    // Byte 0: P0[7:0]
    // Byte 1: P1[3:0] | P0[11:8]
    // Byte 2: P1[11:4]
    for (int i = 0; i < num_pairs; ++i) {
      const uint8_t b0 = src[3 * i];
      const uint8_t b1 = src[3 * i + 1];
      const uint8_t b2 = src[3 * i + 2];

      dst[2 * i] = (b0 >> 4) | ((b1 & 0x0F) << 4);
      dst[2 * i + 1] = b2;
    }
  } else {
    // GigE Vision / GenICam 12-bit packed format layout (e.g. Mono12Packed,
    // Bayer12Packed): Byte 0: P0[11:4] Byte 1: P0[3:0] | P1[3:0] Byte 2:
    // P1[11:4]
    for (int i = 0; i < num_pairs; ++i) {
      const uint8_t b0 = src[3 * i];
      const uint8_t b2 = src[3 * i + 2];

      dst[2 * i] = b0;
      dst[2 * i + 1] = b2;
    }
  }
}

absl::StatusOr<SensorImage> ToSensorImageImpl(
    const void* src_buffer, int64_t sensor_id,
    std::optional<CameraParams> camera_params,
    std::optional<Pose> camera_t_sensor, Dimensions dimensions,
    ArvPixelFormat pixel_format, absl::Time acquisition_time,
    double distance_scale) {
  cv::Mat src_image;
  if (pixel_format == ARV_PIXEL_FORMAT_BAYER_RG_12_PACKED ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_RG_12P ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_GR_12_PACKED ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_GR_12P ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_GB_12_PACKED ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_GB_12P ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_BG_12_PACKED ||
      pixel_format == ARV_PIXEL_FORMAT_BAYER_BG_12P) {
    src_image = cv::Mat(dimensions.rows, dimensions.cols, CV_8UC1);
    Unpack12BitTo8Bit(
        static_cast<const uint8_t*>(src_buffer), src_image.data,
        dimensions.rows * dimensions.cols,
        /*is_p_format=*/pixel_format == ARV_PIXEL_FORMAT_BAYER_RG_12P ||
            pixel_format == ARV_PIXEL_FORMAT_BAYER_GR_12P ||
            pixel_format == ARV_PIXEL_FORMAT_BAYER_GB_12P ||
            pixel_format == ARV_PIXEL_FORMAT_BAYER_BG_12P);
  } else {
    INTR_ASSIGN_OR_RETURN(
        src_image, OpenCvBufferView(dimensions, src_buffer, pixel_format));
  }

  auto to_sensor_image = [&]<typename Traits>(Image<Traits>&& image) {
    return SensorImage(sensor_id, acquisition_time, camera_params,
                       camera_t_sensor, std::move(image));
  };

  switch (pixel_format) {
    case ARV_PIXEL_FORMAT_MONO_8:
    case kPixelFormatConfidence8: {
      // GRAY 8u -> GRAY 8u.
      VLOG(1) << "Gray8|Confidence8, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Gray8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.copyTo(dst_image);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_MONO_10: {
      // GRAY 10u -> GRAY 8u.
      VLOG(1) << "Gray10, " << dimensions.cols << " x " << dimensions.rows;
      Image<Gray8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.convertTo(dst_image, CV_8UC1, 255.0 / 1023.0);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_MONO_12: {
      // GRAY 12u -> GRAY 8u.
      VLOG(1) << "Gray12, " << dimensions.cols << " x " << dimensions.rows;
      Image<Gray8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.convertTo(dst_image, CV_8UC1, 255.0 / 4095.0);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_MONO_16: {
      // GRAY 16u -> GRAY 8u.
      VLOG(1) << "Gray16, " << dimensions.cols << " x " << dimensions.rows;
      Image<Gray8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.convertTo(dst_image, CV_8UC1, 255.0 / 65535.0);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_RGB_8_PACKED: {
      // RGB 8u -> RGB 8u.
      VLOG(1) << "RGB8, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.copyTo(dst_image);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_RG_8: {
      // BAYER_RG 8u -> BGR 8u (we need to convert to BGR as it is OpenCV's
      // default color convention).
      VLOG(1) << "BayerRG8, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerRG2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_RG_12: {
      // BAYER_RG 12u -> BGR 8u.
      VLOG(1) << "BayerRG12, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::Mat src_8u;
      src_image.convertTo(src_8u, CV_8UC1, 255.0 / 4095.0);
      cv::cvtColor(src_8u, dst_image, cv::COLOR_BayerRG2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_RG_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_RG_12P: {
      // BayerRG12Packed/BayerRG12P -> BGR 8u.
      // The src_image has already been unpacked to CV_8UC1.
      VLOG(1) << "BayerRG12Packed|BayerRG12P, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerRG2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_GR_8: {
      // BAYER_GR 8u -> BGR 8u (we need to convert to BGR as it is OpenCV's
      // default color convention).
      VLOG(1) << "BayerGR8, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerGR2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_GR_12: {
      // BAYER_GR 12u -> BGR 8u.
      VLOG(1) << "BayerGR12, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::Mat src_8u;
      src_image.convertTo(src_8u, CV_8UC1, 255.0 / 4095.0);
      cv::cvtColor(src_8u, dst_image, cv::COLOR_BayerGR2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_GR_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_GR_12P: {
      // BayerGR12Packed/BayerGR12P -> BGR 8u.
      // The src_image has already been unpacked to CV_8UC1.
      VLOG(1) << "BayerGR12Packed|BayerGR12P, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerGR2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_GB_8: {
      // BAYER_GB 8u -> BGR 8u (we need to convert to BGR as it is OpenCV's
      // default color convention).
      VLOG(1) << "BayerGB8, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerGB2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_GB_12: {
      // BAYER_GB 12u -> BGR 8u.
      VLOG(1) << "BayerGB12, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::Mat src_8u;
      src_image.convertTo(src_8u, CV_8UC1, 255.0 / 4095.0);
      cv::cvtColor(src_8u, dst_image, cv::COLOR_BayerGB2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_GB_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_GB_12P: {
      // BayerGB12Packed/BayerGB12P -> BGR 8u.
      // The src_image has already been unpacked to CV_8UC1.
      VLOG(1) << "BayerGB12Packed|BayerGB12P, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerGB2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_BG_8: {
      // BAYER_BG 8u -> BGR 8u (we need to convert to BGR as it is OpenCV's
      // default color convention).
      VLOG(1) << "BayerBG8, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerBG2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_BG_12: {
      // BAYER_BG 12u -> BGR 8u.
      VLOG(1) << "BayerBG12, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::Mat src_8u;
      src_image.convertTo(src_8u, CV_8UC1, 255.0 / 4095.0);
      cv::cvtColor(src_8u, dst_image, cv::COLOR_BayerBG2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_BAYER_BG_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_BG_12P: {
      // BayerBG12Packed/BayerBG12P -> BGR 8u.
      // The src_image has already been unpacked to CV_8UC1.
      VLOG(1) << "BayerBG12Packed|BayerBG12P, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_BayerBG2BGR);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_YUV_422_PACKED: {
      VLOG(1) << "YUV422Packed, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_YUV2RGB_Y422);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_YUV_422_YUYV_PACKED: {
      VLOG(1) << "YUV422_YUYV_Packed, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_YUV2RGB_YUYV);
      return to_sensor_image(std::move(image));
    }
    case kPixelFormatYCbCr422_8: {
      // "Typically the terms YCbCr and YUV are used interchangeably."
      // (see https://en.wikipedia.org/wiki/YCbCr#YCbCr)
      VLOG(1) << "YCbCr422_8, " << dimensions.cols << " x " << dimensions.rows;
      Image<Rgb8u> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      cv::cvtColor(src_image, dst_image, cv::COLOR_YUV2BGR_YVYU);
      return to_sensor_image(std::move(image));
    }
    case kPixelFormatCoord3D_A32f:
    case kPixelFormatCoord3D_B32f: {
      VLOG(1) << "Coord3D_[A|B]32f, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Gray32f> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.convertTo(dst_image, CV_32FC1, distance_scale);
      return to_sensor_image(std::move(image));
    }
    case kPixelFormatCoord3D_C32f: {
      VLOG(1) << "Coord3D_C32f, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Depth32f> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.convertTo(dst_image, CV_32FC1, distance_scale);
      return to_sensor_image(std::move(image));
    }
    case ARV_PIXEL_FORMAT_COORD3D_ABC_32F: {
      VLOG(1) << "Coord3D_ABC32f, " << dimensions.cols << " x "
              << dimensions.rows;
      Image<Point32f> image(dimensions);
      cv::Mat dst_image = intrinsic::perception::ToCvMat(image);
      src_image.convertTo(dst_image, CV_32FC3, distance_scale);
      return to_sensor_image(std::move(image));
    }
    default:
      return intrinsic::InternalErrorBuilder()
             << "Unsupported pixel format: 0x" << std::hex << pixel_format;
  }
}

void UpdateInterfaces(std::string_view device_id) {
  const bool fake_device = device_id == kFakeCameraDeviceId;
  for (unsigned int i = 0; i < arv_get_n_interfaces(); ++i) {
    const std::string_view interface_id = arv_get_interface_id(i);
    const bool fake_interface = interface_id == kFakeCameraInterfaceId;
    if ((fake_device && fake_interface) || (!fake_device && !fake_interface)) {
      arv_enable_interface(interface_id.data());
    } else {
      arv_disable_interface(interface_id.data());
    }
  }
}

absl::StatusOr<IntrinsicParams> GetFactoryIntrinsicParams(
    ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  GError* error = nullptr;

  const gint64 width = arv_camera_get_integer(camera, genicam::kWidth, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  const gint64 height =
      arv_camera_get_integer(camera, genicam::kHeight, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

  const double focal_length =
      arv_camera_get_float(camera, genicam::kScan3dFocalLength, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  double aspect_ratio =
      arv_camera_get_float(camera, genicam::kScan3dAspectRatio, &error);
  if (!ValidateAndNullArvError(error).ok()) {
    aspect_ratio = 1.0;
  }

  const double principal_point_u =
      arv_camera_get_float(camera, genicam::kScan3dPrincipalPointU, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  const double principal_point_v =
      arv_camera_get_float(camera, genicam::kScan3dPrincipalPointV, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

  return IntrinsicParams(Dimensions(width, height), focal_length,
                         focal_length * aspect_ratio, principal_point_u,
                         principal_point_v);
}

absl::StatusOr<DistortionParams> GetFactoryDistortionParams(
    ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  const std::string vendor_name = GetVendorName(camera);

  if (vendor_name != kPhotoneo) {
    return intrinsic::NotFoundErrorBuilder()
           << "Retrieving distortion parameters is only supported for Photoneo "
              "cameras.";
  }
  INTR_ASSIGN_OR_RETURN(
      const std::vector<double> params,
      ReadRegister<double>(kPhotoneoDistortionCoefficients, camera));
  INTR_RET_CHECK_GE(params.size(), 4);

  const double k1 = params[0];
  const double k2 = params[1];
  const double p1 = params[2];
  const double p2 = params[3];
  const double k3 = (params.size() >= 5) ? params[4] : 0.0;
  const double k4 = (params.size() >= 6) ? params[5] : 0.0;
  const double k5 = (params.size() >= 7) ? params[6] : 0.0;
  const double k6 = (params.size() >= 8) ? params[7] : 0.0;
  const double s1 = (params.size() >= 9) ? params[8] : 0.0;
  const double s2 = (params.size() >= 10) ? params[9] : 0.0;
  const double s3 = (params.size() >= 11) ? params[10] : 0.0;
  const double s4 = (params.size() >= 12) ? params[11] : 0.0;
  const double tx = (params.size() >= 13) ? params[12] : 0.0;
  const double ty = (params.size() >= 14) ? params[13] : 0.0;
  return DistortionParams{.k1 = k1,
                          .k2 = k2,
                          .p1 = p1,
                          .p2 = p2,
                          .k3 = k3,
                          .k4 = k4,
                          .k5 = k5,
                          .k6 = k6,
                          .s1 = s1,
                          .s2 = s2,
                          .s3 = s3,
                          .s4 = s4,
                          .tx = tx,
                          .ty = ty};
}

}  // namespace

absl::StatusOr<int> BitsPerPixel(ArvPixelFormat pixel_format) {
  // Taken from
  // https://aravisproject.github.io/docs/aravis-0.8/ArvBuffer.html#ARV-PIXEL-FORMAT-BIT-PER-PIXEL:CAPS.
  const int bits_per_pixel = (pixel_format >> 16) & 0xff;
  if (bits_per_pixel == 0 ||
      (bits_per_pixel % 8 != 0 && bits_per_pixel != 12)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot handle " << bits_per_pixel << " bits / pixel.";
  }
  return bits_per_pixel;
}

absl::StatusOr<int> NumChannels(ArvPixelFormat pixel_format) {
  INTR_ASSIGN_OR_RETURN(const int bits_per_pixel, BitsPerPixel(pixel_format));
  switch (pixel_format) {
    case ARV_PIXEL_FORMAT_RGB_8_PACKED:
    case ARV_PIXEL_FORMAT_MONO_8:
    case ARV_PIXEL_FORMAT_BAYER_RG_8:
    case ARV_PIXEL_FORMAT_BAYER_GR_8:
    case ARV_PIXEL_FORMAT_BAYER_GB_8:
    case ARV_PIXEL_FORMAT_BAYER_BG_8:
    case ARV_PIXEL_FORMAT_YUV_422_PACKED:
    case ARV_PIXEL_FORMAT_YUV_422_YUYV_PACKED:
    case kPixelFormatYCbCr422_8:
    case kPixelFormatConfidence8:
      return bits_per_pixel / 8;
    case ARV_PIXEL_FORMAT_MONO_10:
    case ARV_PIXEL_FORMAT_MONO_12:
    case ARV_PIXEL_FORMAT_MONO_16:
    case ARV_PIXEL_FORMAT_BAYER_RG_12:
    case ARV_PIXEL_FORMAT_BAYER_GR_12:
    case ARV_PIXEL_FORMAT_BAYER_GB_12:
    case ARV_PIXEL_FORMAT_BAYER_BG_12:
      return bits_per_pixel / 16;
    case ARV_PIXEL_FORMAT_BAYER_RG_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_RG_12P:
    case ARV_PIXEL_FORMAT_BAYER_GR_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_GR_12P:
    case ARV_PIXEL_FORMAT_BAYER_GB_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_GB_12P:
    case ARV_PIXEL_FORMAT_BAYER_BG_12_PACKED:
    case ARV_PIXEL_FORMAT_BAYER_BG_12P:
      return bits_per_pixel / 12;
    case ARV_PIXEL_FORMAT_COORD3D_ABC_32F:
    case kPixelFormatCoord3D_A32f:
    case kPixelFormatCoord3D_B32f:
    case kPixelFormatCoord3D_C32f:
      return bits_per_pixel / 32;
    default:
      return intrinsic::InternalErrorBuilder()
             << "Unsupported pixel format: 0x" << std::hex << pixel_format;
  }
}

absl::StatusOr<int> CvType(ArvPixelFormat pixel_format) {
  INTR_ASSIGN_OR_RETURN(const int bits_per_pixel, BitsPerPixel(pixel_format));
  if (bits_per_pixel % 8 != 0) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot represent fractional bits per pixel: " << bits_per_pixel;
  }
  INTR_ASSIGN_OR_RETURN(const int num_channels, NumChannels(pixel_format));
  const int bytes_per_channel = bits_per_pixel / num_channels / 8;
  if (bytes_per_channel == 1) {
    return CV_MAKETYPE(cv::DataType<uint8_t>::depth, num_channels);
  } else if (bytes_per_channel == 2) {
    return CV_MAKETYPE(cv::DataType<uint16_t>::depth, num_channels);
  } else if (bytes_per_channel == 4) {
    return CV_MAKETYPE(cv::DataType<float>::depth, num_channels);
  }
  return ::intrinsic::InvalidArgumentErrorBuilder()
         << "Cannot handle " << bytes_per_channel << " bytes / channel.";
}

absl::Status ValidateAndNullArvError(GError*& error,
                                     std::string_view error_message,
                                     intrinsic::SourceLocation location) {
  if (error == nullptr) return absl::OkStatus();
  const absl::Status status = StatusFromError(*error);
  g_error_free(error);
  error = nullptr;

  intrinsic::StatusBuilder status_builder(status, location);
  if (error_message.empty()) {
    if (status.code() == absl::StatusCode::kPermissionDenied) {
      status_builder << "Aravis has triggered an access-denied error. It is "
                        "very likely that the camera is already in use or, in "
                        "rare cases, has a hardware or cable fault.\n";
    } else if (status.code() == absl::StatusCode::kNotFound &&
               absl::StrContains(status.message(), "Genicam data")) {
      status_builder << "No Genicam data has been found. In some cases, this "
                        "can be resolved by power-cycling the device.";
    } else {
      status_builder << "Aravis has triggered an error.\n";
    }
  } else {
    status_builder << error_message;
  }
  return status_builder;
}

absl::StatusOr<std::vector<std::string>> EnumerateParameters(
    std::string_view feature, ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  GError* error = nullptr;
  uint32_t num_values = 0;
  const char** raw_params = arv_camera_dup_available_enumerations_as_strings(
      camera, feature.data(), &num_values, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (num_values == 0) {
    CHECK(raw_params == nullptr);
    return intrinsic::NotFoundErrorBuilder()
           << "The feature '" << feature << "' is not supported.";
  }
  DCHECK(raw_params != nullptr);
  std::vector<std::string> parameters;
  parameters.reserve(num_values);
  for (uint32_t i = 0; i < num_values; ++i) {
    parameters.emplace_back(raw_params[i]);
  }
  g_free(raw_params);
  return parameters;
}

absl::StatusOr<ArvGcFeatureNode*> GetFeatureNode(
    std::string_view name, ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  // Aravis is not implemented const correct. We need pointers to mutable
  // objects.
  ArvDevice* device = arv_camera_get_device(camera);
  if (device == nullptr) {
    // This should technically never happen.
    return intrinsic::InternalErrorBuilder()
           << "Could not query ArvDevice of camera.";
  }
  ArvGcNode* arv_node = arv_device_get_feature(device, name.data());
  if (arv_node == nullptr) {
    return intrinsic::NotFoundErrorBuilder()
           << "The feature '" << name << "' is not supported by the camera.";
  }
  if (!ARV_IS_GC_FEATURE_NODE(arv_node)) {
    return intrinsic::NotFoundErrorBuilder()
           << "The feature '" << name << "' is not supported by the camera.";
  }
  auto* arv_feature_node = reinterpret_cast<ArvGcFeatureNode*>(arv_node);
  if (arv_feature_node == nullptr) {
    // This should technically never happen.
    return intrinsic::InternalErrorBuilder()
           << "Failed to convert node to feature node.";
  }
  return arv_feature_node;
}

absl::StatusOr<ArvGcAccessMode> GetAccessMode(std::string_view name,
                                              ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  GError* error = nullptr;
  const bool is_feature_implemented =
      arv_camera_is_feature_implemented(camera, name.data(), &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (!is_feature_implemented) {
    return intrinsic::NotFoundErrorBuilder()
           << "The feature '" << name << "' is not implemented.";
  }
  const bool is_feature_available =
      arv_camera_is_feature_available(camera, name.data(), &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (!is_feature_available) {
    return intrinsic::NotFoundErrorBuilder()
           << "The feature '" << name << "' is not available.";
  }
  ArvDevice* device = arv_camera_get_device(camera);
  return arv_device_get_feature_access_mode(device, name.data());
}

std::vector<std::string> ListAvailableDeviceIds() {
  absl::MutexLock lock(device_list_mutex);
  arv_update_device_list();
  const unsigned int num_devices = arv_get_n_devices();
  std::vector<std::string> device_ids;
  device_ids.reserve(num_devices);
  for (unsigned int device_idx = 0; device_idx < num_devices; ++device_idx) {
    if (const char* device_id = arv_get_device_id(device_idx);
        device_id != nullptr) {
      device_ids.emplace_back(device_id);
    }
  }
  return device_ids;
}

absl::StatusOr<GObjectPtr<ArvCamera>> DiscoverCamera(std::string_view device_id,
                                                     absl::Duration timeout) {
  GError* error = nullptr;
  absl::Status status;
  const absl::Time deadline = ToDeadline(timeout);
  while (true) {
    {
      absl::MutexLock lock(device_list_mutex);
      // Update global Aravis interface availability under `device_list_mutex`
      // on each attempt in case another thread toggled interfaces while we
      // slept.
      UpdateInterfaces(device_id);
      // `arv_camera_new` connects directly if `device_id` is already cached in
      // the interface's device map, or performs a targeted discovery for
      // `device_id` without waiting for a full broadcast scan timeout.
      GObjectPtr<ArvCamera> camera(arv_camera_new(device_id.data(), &error));
      status = ValidateAndNullArvError(error, "Error during camera creation.");
      if (status.ok()) {
        LOG(INFO) << "Camera discovered.";
        return camera;
      }
      // If `device_id` was cached with stale connection info, `arv_camera_new`
      // fails without re-discovering; refresh the device list for the next
      // retry.
      if (!absl::IsNotFound(status) && !absl::IsPermissionDenied(status)) {
        arv_update_device_list();
      }
    }

    const absl::Duration wait_duration =
        std::min(deadline - absl::Now(), absl::Seconds(1));
    if (wait_duration <= absl::ZeroDuration()) {
      break;
    }

    LOG_EVERY_N_SEC(INFO, 5.0) << "Waiting " << wait_duration << " for camera "
                               << device_id << " to be discovered.";
    absl::SleepFor(wait_duration);
  }

  return AnnotateError(status,
                       absl::StrCat("Camera discovery timed out after ",
                                    absl::FormatDuration(timeout), "."));
}

std::string_view EnableFakeCamera() {
  static absl::once_flag flag;
  absl::call_once(flag, arv_fake_camera_register_resource);
  return kFakeCameraDeviceId;
}

absl::StatusOr<GObjectPtr<ArvCamera>> CreateFakeCamera() {
  return DiscoverCamera(EnableFakeCamera());
}

absl::StatusOr<std::optional<CameraParams>> GetFactoryCameraParams(
    ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  const std::string vendor_name = GetVendorName(camera);

  std::function<void()> reset_operation_mode = [] {};
  // Temporarily switch the Photoneo operation mode to scanner, so we retrieve
  // the camera params with the maximum resolution.
  if (vendor_name == kPhotoneo) {
    if (const absl::StatusOr<ArvGcAccessMode> operation_mode_access_mode =
            GetAccessMode(kPhotoneoOperationMode, camera);
        operation_mode_access_mode.ok() &&
        *operation_mode_access_mode == ArvGcAccessMode::ARV_GC_ACCESS_MODE_RW) {
      GError* error = nullptr;
      const std::string current_operation_mode =
          arv_camera_get_string(camera, kPhotoneoOperationMode, &error);
      INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
      if (current_operation_mode != kPhotoneoOperationModeScanner) {
        arv_camera_set_string(camera, kPhotoneoOperationMode,
                              kPhotoneoOperationModeScanner, &error);
        INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
        reset_operation_mode = [camera, current_operation_mode] {
          GError* error = nullptr;
          arv_camera_set_string(camera, kPhotoneoOperationMode,
                                current_operation_mode.data(), &error);
          if (const absl::Status status = ValidateAndNullArvError(error);
              !status.ok()) {
            LOG(ERROR) << status;
          }
        };
      }
    }
  }
  const absl::Cleanup on_return = std::move(reset_operation_mode);

  const absl::StatusOr<IntrinsicParams> intrinsic_params =
      GetFactoryIntrinsicParams(camera);
  if (intrinsic_params.status().code() == absl::StatusCode::kNotFound) {
    return std::nullopt;
  } else if (!intrinsic_params.ok()) {
    return intrinsic_params.status();
  }
  const absl::StatusOr<DistortionParams> distortion_params =
      GetFactoryDistortionParams(camera);
  if (distortion_params.status().code() == absl::StatusCode::kNotFound) {
    return CameraParams(*intrinsic_params);
  } else if (!distortion_params.ok()) {
    return distortion_params.status();
  }
  return CameraParams(*intrinsic_params, *distortion_params);
}

absl::StatusOr<std::optional<Pose>> GetCameraTSensor(
    ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  const std::string vendor_name = GetVendorName(camera);
  if (vendor_name != kPhotoneo) {
    LOG(WARNING) << "Retrieving cameraTSensor transformation is not supported "
                    "for: "
                 << vendor_name;
    return std::nullopt;
  }

  const Version firmware_version = GetFirmwareVersion(camera);
  if (firmware_version < PhotoneoMinimumFirmwareVersion()) {
    LOG(ERROR) << "Photoneo firmware version " << firmware_version
               << " does not support cameraTSensor, you need at least version "
               << PhotoneoMinimumFirmwareVersion();
    return std::nullopt;
  }

  INTR_ASSIGN_OR_RETURN(
      const std::vector<double> rotation,
      ReadRegister<double>(kPhotoneoWorldToCameraRotationMatrix, camera));
  INTR_RET_CHECK_EQ(rotation.size(), 9);
  INTR_ASSIGN_OR_RETURN(
      const std::vector<double> translation,
      ReadRegister<double>(kPhotoneoWorldToCameraTranslationVector, camera));
  INTR_RET_CHECK_EQ(translation.size(), 3);

  return Pose{Eigen::Map<const Matrix3d>(rotation.data()),
              Eigen::Map<const Vector3d>(translation.data()) / 1000.0}
      .inverse();
}

std::string GetVendorName(ArvCamera* absl_nonnull arv_camera) {
  if (arv_camera == nullptr) return {};
  const char* vendor_name = arv_camera_get_vendor_name(arv_camera, nullptr);
  if (vendor_name == nullptr) return {};
  return vendor_name;
}

std::string GetModelName(ArvCamera* absl_nonnull arv_camera) {
  if (arv_camera == nullptr) return {};
  const char* model_name = arv_camera_get_model_name(arv_camera, nullptr);
  if (model_name == nullptr) return {};
  return model_name;
}

Version GetFirmwareVersion(ArvCamera* absl_nonnull arv_camera) {
  if (arv_camera == nullptr) return Version({});
  const char* firmware_version = arv_camera_get_string(
      arv_camera, genicam::kDeviceFirmwareVersion, nullptr);
  if (firmware_version == nullptr) return Version({});
  return Version(firmware_version);
}

std::optional<std::string_view> GetExposureTimeName(
    ArvCamera* absl_nonnull arv_camera) {
  const std::string vendor_name = GetVendorName(arv_camera);
  const std::string model_name = GetModelName(arv_camera);

  const auto has = [arv_camera](std::string_view name,
                                gboolean (*arv_is_gc_type_fn)(gpointer)) {
    const absl::StatusOr<ArvGcFeatureNode*> node =
        GetFeatureNode(name, arv_camera);
    return node.ok() && arv_is_gc_type_fn(*node);
  };

  // The logic is based on
  // https://github.com/AravisProject/aravis/blob/3a62ba0de5464e5a36a3d9e283ea4262c28ba3a7/src/arvcamera.c#L1699-L1746
  if (vendor_name == "Basler" && model_name.starts_with("scA")) {  // scout
    if (has(genicam::kExposureTime, ARV_IS_GC_FLOAT))
      return genicam::kExposureTime;
    else if (has(genicam::kExposureTimeBaseAbs, ARV_IS_GC_FLOAT))
      return genicam::kExposureTimeBaseAbs;
  } else if (vendor_name == "Basler" && model_name.starts_with("acA")) {  // ace
    if (has(genicam::kExposureTime, ARV_IS_GC_FLOAT))
      return genicam::kExposureTime;
    else if (has(genicam::kExposureTimeRaw, ARV_IS_GC_INTEGER))
      return genicam::kExposureTimeRaw;
  } else if (vendor_name == "XIMEA GmbH") {
    if (has(genicam::kExposureTime, ARV_IS_GC_INTEGER))
      return genicam::kExposureTime;
  } else if (vendor_name == "Ricoh Company, Ltd.") {
    if (has(genicam::kExposureTimeRaw, ARV_IS_GC_INTEGER))
      return genicam::kExposureTimeRaw;
  } else {
    if (has(genicam::kExposureTime, ARV_IS_GC_FLOAT))
      return genicam::kExposureTime;
    else if (has(genicam::kExposureTimeAbs, ARV_IS_GC_FLOAT))
      return genicam::kExposureTimeAbs;
  }
  return std::nullopt;
}

std::optional<std::string_view> GetGainName(
    ArvCamera* absl_nonnull arv_camera) {
  const auto has = [arv_camera](std::string_view name,
                                gboolean (*arv_is_gc_type_fn)(gpointer)) {
    const absl::StatusOr<ArvGcFeatureNode*> node =
        GetFeatureNode(name, arv_camera);
    return node.ok() && arv_is_gc_type_fn(*node);
  };

  // The logic is based on
  // https://github.com/AravisProject/aravis/blob/3a62ba0de5464e5a36a3d9e283ea4262c28ba3a7/src/arvcamera.c#L1862-L1895
  if (has(genicam::kGain, ARV_IS_GC_FLOAT))
    return genicam::kGain;
  else if (has(genicam::kGainRaw, ARV_IS_GC_FLOAT))
    return genicam::kGainRaw;
  else if (has(genicam::kGainAbs, ARV_IS_GC_FLOAT))
    return genicam::kGainAbs;
  else if (has(genicam::kGainRaw, ARV_IS_GC_INTEGER))
    return genicam::kGainRaw;
  return std::nullopt;
}

std::optional<CameraSettingProperties::Float::Range> GetExposureTimeRange(
    ArvCamera* absl_nonnull arv_camera) {
  GError* error = nullptr;
  double minimum;
  double maximum;
  arv_camera_get_exposure_time_bounds(arv_camera, &minimum, &maximum, &error);
  if (!ValidateAndNullArvError(error).ok()) {
    return std::nullopt;
  }
  return CameraSettingProperties::Float::Range{.minimum = minimum,
                                               .maximum = maximum};
}

std::optional<CameraSettingProperties::Float::Range> GetGainRange(
    ArvCamera* absl_nonnull arv_camera) {
  GError* error = nullptr;
  double minimum;
  double maximum;
  arv_camera_get_gain_bounds(arv_camera, &minimum, &maximum, &error);
  if (!ValidateAndNullArvError(error).ok()) {
    return std::nullopt;
  }
  return CameraSettingProperties::Float::Range{.minimum = minimum,
                                               .maximum = maximum};
}

std::optional<double> GetExposureTimeIncrement(
    ArvCamera* absl_nonnull arv_camera) {
  GError* error = nullptr;
  double increment = arv_camera_get_exposure_time_increment(arv_camera, &error);
  if (!ValidateAndNullArvError(error).ok()) {
    return std::nullopt;
  }
  return increment;
}

std::optional<double> GetGainIncrement(ArvCamera* absl_nonnull arv_camera) {
  GError* error = nullptr;
  double increment = arv_camera_get_gain_increment(arv_camera, &error);
  if (!ValidateAndNullArvError(error).ok()) {
    return std::nullopt;
  }
  return increment;
}

std::optional<CameraSettingProperties::Float> GetExposureTimeProperties(
    ArvCamera* absl_nonnull arv_camera) {
  const std::optional<CameraSettingProperties::Float::Range> range =
      GetExposureTimeRange(arv_camera);
  const std::optional<double> increment = GetExposureTimeIncrement(arv_camera);
  if (!range.has_value() && !increment.has_value()) {
    return std::nullopt;
  }
  return CameraSettingProperties::Float{
      .range = range, .increment = increment.value_or(0.0), .unit = "μs"};
}

std::optional<CameraSettingProperties::Float> GetGainProperties(
    ArvCamera* absl_nonnull arv_camera) {
  const std::optional<CameraSettingProperties::Float::Range> range =
      GetGainRange(arv_camera);
  const std::optional<double> increment = GetGainIncrement(arv_camera);
  if (!range.has_value() && !increment.has_value()) {
    return std::nullopt;
  }
  return CameraSettingProperties::Float{
      .range = range, .increment = increment.value_or(0.0), .unit = "dB"};
}

BufferHelper::BufferHelper(ArvCamera* absl_nonnull camera,
                           const std::vector<std::string>& desired_features) {
  GError* error = nullptr;
  const bool chunks_available = arv_camera_are_chunks_available(camera, &error);
  if (!ValidateAndNullArvError(error).ok() || !chunks_available) {
    LOG(INFO) << "No chunks available.";
    return;
  }

  absl::StatusOr<std::vector<std::string>> chunk_selectors =
      EnumerateParameters(genicam::kChunkSelector, camera);
  if (!chunk_selectors.ok() || chunk_selectors->empty()) {
    LOG(INFO) << "No chunk selectors available.";
    return;
  }

  GObjectPtr<ArvChunkParser> parser =
      GObjectPtr<ArvChunkParser>(arv_camera_create_chunk_parser(camera));
  if (parser == nullptr) {
    LOG(INFO) << "No chunk parser available.";
    return;
  }

  parser_ = std::move(parser);
  supported_features_ = {chunk_selectors->begin(), chunk_selectors->end()};

  const std::vector<std::string> all_features = GetSupportedFeatures({});
  LOG(INFO) << "Available chunks: "
            << absl::StrJoin(all_features.begin(), all_features.end(), ", ");
  const std::vector<std::string> enable_features =
      GetSupportedFeatures(desired_features);
  if (const absl::Status status = EnableFeatures(enable_features, camera);
      status.ok()) {
    LOG(INFO) << "Enabled chunks: "
              << (enable_features.empty()
                      ? "None"
                      : absl::StrJoin(enable_features.begin(),
                                      enable_features.end(), ", "));
  } else {
    LOG(WARNING) << "Could not enable chunks. Status: " << status;
  }
}

std::vector<std::string> BufferHelper::GetSupportedFeatures(
    const std::vector<std::string>& desired_features) const {
  std::vector<std::string> sorted_supported_features(
      supported_features_.begin(), supported_features_.end());
  std::sort(sorted_supported_features.begin(), sorted_supported_features.end());
  if (desired_features.empty()) {
    return sorted_supported_features;
  }

  std::vector<std::string> sorted_desired_features(desired_features.begin(),
                                                   desired_features.end());
  std::sort(sorted_desired_features.begin(), sorted_desired_features.end());
  std::vector<std::string> supported_features;
  absl::c_set_intersection(sorted_desired_features, sorted_supported_features,
                           std::back_inserter(supported_features));
  return supported_features;
}

absl::Status BufferHelper::EnableFeatures(
    const std::vector<std::string>& features, ArvCamera* absl_nonnull camera) {
  INTR_RET_CHECK_NE(camera, nullptr);

  if (features.empty()) {
    return absl::OkStatus();
  }
  GError* error = nullptr;
  arv_camera_set_chunks(camera, absl::StrJoin(features, ",").data(), &error);
  const absl::Status set_chunks_status =
      ValidateAndNullArvError(error, "Error during set_chunks.");
  if (!set_chunks_status.ok()) {
    // Try to undo any changes.
    arv_camera_set_chunks(camera, nullptr, nullptr);
  }
  return set_chunks_status;
}

absl::StatusOr<Dimensions> BufferHelper::GetDimensions(
    ArvBuffer* absl_nonnull buffer, guint part_id,
    Dimensions fallback_dimensions) const {
  INTR_RET_CHECK_NE(buffer, nullptr);

  if (!arv_buffer_has_chunks(buffer)) {
    return Dimensions(arv_buffer_get_part_width(buffer, part_id),
                      arv_buffer_get_part_height(buffer, part_id));
  }

  absl::StatusOr<gint64> chunk_width =
      GetChunkValue<gint64>(genicam::kWidth, buffer);
  absl::StatusOr<gint64> chunk_height =
      GetChunkValue<gint64>(genicam::kHeight, buffer);
  if (chunk_width.ok() && chunk_height.ok()) {
    return Dimensions(*chunk_width, *chunk_height);
  }

  return fallback_dimensions;
}

absl::StatusOr<ArvPixelFormat> BufferHelper::GetPixelFormat(
    ArvBuffer* absl_nonnull buffer, guint part_id,
    ArvPixelFormat fallback_pixel_format) const {
  INTR_RET_CHECK_NE(buffer, nullptr);

  if (!arv_buffer_has_chunks(buffer)) {
    return arv_buffer_get_part_pixel_format(buffer, part_id);
  }

  absl::StatusOr<gint64> chunk_pixel_format =
      GetChunkValue<gint64>(genicam::kPixelFormat, buffer);
  if (chunk_pixel_format.ok()) {
    return static_cast<ArvPixelFormat>(*chunk_pixel_format);
  }

  return fallback_pixel_format;
}

std::optional<absl::Duration> BufferHelper::GetExposureTime(
    ArvBuffer* absl_nonnull buffer) const {
  if (buffer == nullptr || !arv_buffer_has_chunks(buffer)) {
    return std::nullopt;
  }

  const auto chunk_exposure_time =
      GetChunkValue<double>(genicam::kExposureTime, buffer);
  if (!chunk_exposure_time.ok()) {
    return std::nullopt;
  }
  return absl::Microseconds(*chunk_exposure_time);
}

absl::StatusOr<CaptureResult> BufferHelper::ToCaptureResult(
    ArvBuffer* absl_nonnull buffer,
    const CameraParamsBySensorId& camera_params_by_sensor_id,
    const absl::flat_hash_map<int64_t, Pose>& camera_t_sensor_by_sensor_id,
    const absl::flat_hash_map<int64_t, Dimensions>&
        fallback_dimensions_by_sensor_id,
    const absl::flat_hash_map<int64_t, ArvPixelFormat>&
        fallback_pixel_format_by_sensor_id,
    double distance_scale) {
  INTR_RET_CHECK_NE(buffer, nullptr);

  const auto buffer_status = arv_buffer_get_status(buffer);
  if (buffer_status != ARV_BUFFER_STATUS_SUCCESS) {
    return intrinsic::InternalErrorBuilder()
           << "Unhealthy buffer status. Status code: 0x" << std::hex
           << buffer_status;
  }
  // The function arv_buffer_get_timestamp does not deliver reliable data and
  // thus we have to use the system time as provided by Aravis.
  const int64_t system_time_ns = arv_buffer_get_system_timestamp(buffer);
  const absl::Time system_acquisition_time =
      absl::UnixEpoch() + absl::Nanoseconds(system_time_ns);

  const guint num_parts = arv_buffer_get_n_parts(buffer);
  const bool is_multipart = num_parts != 0;
  std::vector<SensorImage> sensor_images;
  sensor_images.reserve(is_multipart ? num_parts : 1);
  for (guint i = 0; i < (is_multipart ? num_parts : 1); ++i) {
    // If chunking and multi-parts are enabled, Photoneo puts the chunk data
    // into a separate part, leading to n+1 parts for n sensors.
    if (is_multipart && arv_buffer_get_part_data_type(buffer, i) ==
                            ARV_BUFFER_PART_DATA_TYPE_CHUNK_DATA) {
      continue;
    }
    const guint component_id = is_multipart
                                   ? arv_buffer_get_part_component_id(buffer, i)
                                   : ImageSourceInterface::kFallbackSensorId;
    std::optional<CameraParams> optional_camera_params =
        OptionalCopyAt(camera_params_by_sensor_id, component_id);

    INTR_RET_CHECK(fallback_dimensions_by_sensor_id.contains(component_id));
    INTR_ASSIGN_OR_RETURN(
        const Dimensions dimensions,
        GetDimensions(buffer, i,
                      fallback_dimensions_by_sensor_id.at(component_id)));
    if (optional_camera_params.has_value()) {
      if (dimensions != optional_camera_params->intrinsic_params.dimensions()) {
        VLOG(1) << "Resizing camera params from "
                << optional_camera_params->intrinsic_params.dimensions()
                << " to " << dimensions;
        optional_camera_params = Resize(*optional_camera_params, dimensions);
      }
    }

    std::optional<Pose> optional_camera_t_sensor =
        OptionalCopyAt(camera_t_sensor_by_sensor_id, component_id);

    INTR_RET_CHECK(fallback_pixel_format_by_sensor_id.contains(component_id));
    INTR_ASSIGN_OR_RETURN(
        const ArvPixelFormat pixel_format,
        GetPixelFormat(buffer, i,
                       fallback_pixel_format_by_sensor_id.at(component_id)));

    size_t buffer_size_in_bytes = 0;
    const void* src_buffer =
        is_multipart
            ? arv_buffer_get_part_data(buffer, i, &buffer_size_in_bytes)
            : arv_buffer_get_data(buffer, &buffer_size_in_bytes);

    INTR_ASSIGN_OR_RETURN(
        SensorImage sensor_image,
        ToSensorImageImpl(src_buffer, component_id, optional_camera_params,
                          optional_camera_t_sensor, dimensions, pixel_format,
                          system_acquisition_time, distance_scale));
    sensor_images.push_back(std::move(sensor_image));
  }

  return CaptureResult{
      .capture_at = sensor_images.empty()
                        ? absl::UnixEpoch()
                        : sensor_images.front().acquisition_time(),
      .sensor_images = std::move(sensor_images),
      .capture_duration = std::nullopt,
  };
}

}  // namespace intrinsic::perception
