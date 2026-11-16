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

#include "intrinsic/perception/cameras/ros_image_source.h"

#include <stdbool.h>
#include <sys/types.h>

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/overload.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/camera_setting_utils.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/distortion_params.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/io_conversions.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/parameter_type.hpp"
#include "rcl_interfaces/msg/parameter_value.hpp"
#include "rcl_interfaces/srv/describe_parameters.hpp"
#include "rcl_interfaces/srv/get_parameters.hpp"
#include "rcl_interfaces/srv/list_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters_atomically.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"
#include "rcutils/allocator.h"
#include "rcutils/types/rcutils_ret.h"
#include "rcutils/types/uint8_array.h"
#include "rmw/serialized_message.h"
#include "rosidl_runtime_cpp/traits.hpp"
#include "sensor_msgs/image_encodings.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/detail/point_field__struct.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "snapshot_interfaces/msg/detail/image_snapshot__struct.hpp"
#include "snapshot_interfaces/msg/detail/point_cloud2_snapshot__struct.hpp"
#include "snapshot_interfaces/msg/detail/sensor_info__struct.hpp"
#include "snapshot_interfaces/srv/detail/describe__struct.hpp"
#include "snapshot_interfaces/srv/detail/snapshot__struct.hpp"

namespace intrinsic::perception {

using ::geometry_msgs::msg::TransformStamped;
using ::rcl_interfaces::msg::Parameter;
using ::rcl_interfaces::msg::ParameterDescriptor;
using ::rcl_interfaces::msg::ParameterType;
using ::rcl_interfaces::msg::ParameterValue;
using ::rcl_interfaces::msg::SetParametersResult;
using ::rcl_interfaces::srv::DescribeParameters;
using ::rcl_interfaces::srv::GetParameters;
using ::rcl_interfaces::srv::ListParameters;
using ::rcl_interfaces::srv::SetParameters;
using ::rcl_interfaces::srv::SetParametersAtomically;
using ::sensor_msgs::msg::CameraInfo;
using ::snapshot_interfaces::msg::ImageSnapshot;
using ::snapshot_interfaces::msg::PointCloud2Snapshot;
using ::snapshot_interfaces::srv::Describe;
using ::snapshot_interfaces::srv::Snapshot;
using RosDuration = ::builtin_interfaces::msg::Duration;
using RosImage = ::sensor_msgs::msg::Image;
using RosPointCloud2 = ::sensor_msgs::msg::PointCloud2;
using RosSensorInfo = ::snapshot_interfaces::msg::SensorInfo;
using RosTime = ::builtin_interfaces::msg::Time;

namespace {

inline constexpr int kPubSubInitTimeoutMs = 1000;

constexpr int kMinimumEnumValues = 2;

RosDuration ToRosDuration(absl::Duration duration) {
  const timespec ts = absl::ToTimespec(duration);
  RosDuration ros_duration;
  ros_duration.sec = ts.tv_sec;
  ros_duration.nanosec = ts.tv_nsec;
  return ros_duration;
}

absl::StatusOr<CameraSettingProperties>
CreateCameraSettingPropertiesFromDescriptor(
    std::string_view name, const ParameterDescriptor& descriptor) {
  if (descriptor.type == ParameterType::PARAMETER_INTEGER &&
      !descriptor.integer_range.empty()) {
    auto descriptor_range = descriptor.integer_range[0];
    return CameraSettingProperties{
        .name = std::string(name),
        .properties = CameraSettingProperties::Integer{
            .range =
                CameraSettingProperties::Integer::Range{
                    .minimum = descriptor_range.from_value,
                    .maximum = descriptor_range.to_value},
            .increment = static_cast<int64_t>(descriptor_range.step)}};
  } else if (descriptor.type == ParameterType::PARAMETER_DOUBLE &&
             !descriptor.floating_point_range.empty()) {
    auto range = descriptor.floating_point_range[0];
    return CameraSettingProperties{
        .name = std::string(name),
        .properties = CameraSettingProperties::Float{
            .range =
                CameraSettingProperties::Float::Range{
                    .minimum = range.from_value, .maximum = range.to_value},
            .increment = range.step}};
  } else if (descriptor.type == ParameterType::PARAMETER_STRING &&
             !descriptor.additional_constraints.empty()) {
    // Split enum values on comma or semicolon. Assume that if there are less
    // than kMinimumEnumValues, then it's not an enum or it's from a generic ros
    // camera driver.
    using absl::ByAnyChar;
    std::vector<absl::string_view> values =
        absl::StrSplit(descriptor.additional_constraints, ByAnyChar(",;"));
    if (values.size() >= kMinimumEnumValues) {
      std::vector<std::string> enum_values;
      enum_values.reserve(values.size());
      for (const auto& value : values) {
        enum_values.push_back(std::string(value));
      }
      return CameraSettingProperties{
          .name = std::string(name),
          .properties = CameraSettingProperties::Enumeration{
              .values = std::move(enum_values)}};
    }
    return CameraSettingProperties{.name = std::string(name)};
  }
  return absl::InvalidArgumentError("Unsupported parameter type");
}

absl::StatusOr<CameraSetting> CreateCameraSettingFromParameterValue(
    std::string_view name, const ParameterValue& value,
    std::optional<CameraSettingProperties> properties = std::nullopt) {
  if (value.type == ParameterType::PARAMETER_INTEGER) {
    return CreateCameraSetting(name, value.integer_value);
  } else if (value.type == ParameterType::PARAMETER_DOUBLE) {
    return CreateCameraSetting(name, value.double_value);
  } else if (value.type == ParameterType::PARAMETER_BOOL) {
    return CreateCameraSetting(name, value.bool_value);
  } else if (value.type == ParameterType::PARAMETER_STRING) {
    if (properties.has_value() &&
        std::holds_alternative<CameraSettingProperties::Enumeration>(
            properties->properties)) {
      return CreateEnumCameraSetting(name, value.string_value);
    }
    return CreateCameraSetting(name, value.string_value);
  }
  return absl::InvalidArgumentError("Unsupported parameter type");
}

absl::StatusOr<Parameter> CreateParameter(const CameraSetting& camera_setting) {
  Parameter parameter;
  parameter.name = camera_setting.name;
  return std::visit(
      absl::Overload{
          [&](const intrinsic::perception::CameraSetting::Integer& value)
              -> absl::StatusOr<Parameter> {
            parameter.value.type = ParameterType::PARAMETER_INTEGER;
            INTR_ASSIGN_OR_RETURN(parameter.value.integer_value,
                                  GetValue<int64_t>(camera_setting));
            return parameter;
          },
          [&](const intrinsic::perception::CameraSetting::Float& value)
              -> absl::StatusOr<Parameter> {
            parameter.value.type = ParameterType::PARAMETER_DOUBLE;
            INTR_ASSIGN_OR_RETURN(parameter.value.double_value,
                                  GetValue<double>(camera_setting));
            return parameter;
          },
          [&](const intrinsic::perception::CameraSetting::Boolean& value)
              -> absl::StatusOr<Parameter> {
            parameter.value.type = ParameterType::PARAMETER_BOOL;
            INTR_ASSIGN_OR_RETURN(parameter.value.bool_value,
                                  GetValue<bool>(camera_setting));
            return parameter;
          },
          [&](const intrinsic::perception::CameraSetting::String& value)
              -> absl::StatusOr<Parameter> {
            parameter.value.type = ParameterType::PARAMETER_STRING;
            INTR_ASSIGN_OR_RETURN(parameter.value.string_value,
                                  GetValue<std::string>(camera_setting));
            return parameter;
          },
          [&](const intrinsic::perception::CameraSetting::Enumeration& value)
              -> absl::StatusOr<Parameter> {
            parameter.value.type = ParameterType::PARAMETER_STRING;
            INTR_ASSIGN_OR_RETURN(parameter.value.string_value,
                                  GetEnumerationValue(camera_setting));
            return parameter;
          },
          [&](const intrinsic::perception::CameraSetting::Command&)
              -> absl::StatusOr<Parameter> {
            LOG(ERROR) << "Command value not supported as a parameter. Name: "
                       << camera_setting.name;
            return absl::InvalidArgumentError(
                "Command value not supported as a parameter");
          },
          [&](const std::monostate&) -> absl::StatusOr<Parameter> {
            return absl::InvalidArgumentError("No value set in camera setting");
          }},
      camera_setting.value);
}

std::optional<CameraParams> CreateCameraParams(const CameraInfo& info) {
  const double fx = info.k[0];
  const double fy = info.k[4];
  const double cx = info.k[2];
  const double cy = info.k[5];

  if (fx == 0.0 || fy == 0.0) {
    return std::nullopt;
  }

  const Dimensions dimensions(info.width, info.height);
  const IntrinsicParams intrinsics(dimensions, fx, fy, cx, cy);
  std::optional<DistortionParams> distortion = std::nullopt;

  // ROS2 follows the OpenCV convention for the distortion matrix:
  // [k1, k2, p1, p2, [k3, [k4, k5, k6, [s1, s2, s3, s4, [tx, ty]]]]]
  const double k1 = !info.d.empty() ? info.d[0] : 0.0;
  const double k2 = info.d.size() > 1 ? info.d[1] : 0.0;
  const double p1 = info.d.size() > 2 ? info.d[2] : 0.0;
  const double p2 = info.d.size() > 3 ? info.d[3] : 0.0;
  const double k3 = info.d.size() > 4 ? info.d[4] : 0.0;
  const double k4 = info.d.size() > 5 ? info.d[5] : 0.0;
  const double k5 = info.d.size() > 6 ? info.d[6] : 0.0;
  const double k6 = info.d.size() > 7 ? info.d[7] : 0.0;
  const double s1 = info.d.size() > 8 ? info.d[8] : 0.0;
  const double s2 = info.d.size() > 9 ? info.d[9] : 0.0;
  const double s3 = info.d.size() > 10 ? info.d[10] : 0.0;
  const double s4 = info.d.size() > 11 ? info.d[11] : 0.0;
  const double tx = info.d.size() > 12 ? info.d[12] : 0.0;
  const double ty = info.d.size() > 13 ? info.d[13] : 0.0;
  if (k1 != 0.0 || k2 != 0.0 || p1 != 0.0 || p2 != 0.0 || k3 != 0.0 ||
      k4 != 0.0 || k5 != 0.0 || k6 != 0.0 || s1 != 0.0 || s2 != 0.0 ||
      s3 != 0.0 || s4 != 0.0 || tx != 0.0 || ty != 0.0) {
    distortion = DistortionParams{.k1 = k1,
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
  return CameraParams(intrinsics, distortion);
}

std::optional<Pose> CreatePose(const TransformStamped& ros_transform) {
  const double qx = ros_transform.transform.rotation.x;
  const double qy = ros_transform.transform.rotation.y;
  const double qz = ros_transform.transform.rotation.z;
  const double qw = ros_transform.transform.rotation.w;
  const double px = ros_transform.transform.translation.x;
  const double py = ros_transform.transform.translation.y;
  const double pz = ros_transform.transform.translation.z;

  intrinsic::eigenmath::Quaterniond quaternion = {qw, qx, qy, qz};
  intrinsic::eigenmath::Vector3d position = {px, py, pz};
  return intrinsic::Pose(quaternion, position);
}

absl::StatusOr<SensorInformation> CreateSensorInformation(
    int64_t sensor_id, const RosSensorInfo& sensor_info) {
  if (sensor_info.info.empty()) {
    return absl::InvalidArgumentError(
        "Cannot create sensor information from empty camera info.");
  }
  const CameraInfo& camera_info = sensor_info.info[0];

  const Dimensions dimensions(camera_info.width, camera_info.height);
  const std::optional<CameraParams> camera_params =
      CreateCameraParams(camera_info);

  const std::optional<Pose> camera_t_sensor =
      CreatePose(sensor_info.camera_t_sensor);

  std::vector<PixelType> pixel_types;
  if (sensor_info.sensor_type == RosSensorInfo::IMAGE) {
    // TODO(b/395343221): check for depth type?
    pixel_types = {PixelType::kIntensity};
  } else if (sensor_info.sensor_type == RosSensorInfo::POINT_CLOUD) {
    pixel_types = {PixelType::kPoint};
  } else if (sensor_info.sensor_type == RosSensorInfo::DEPTH) {
    pixel_types = {PixelType::kDepth};
  } else if (sensor_info.sensor_type == RosSensorInfo::NORMAL) {
    pixel_types = {PixelType::kNormal};
  } else if (sensor_info.sensor_type == RosSensorInfo::IMU) {
    return absl::UnimplementedError("IMU sensor type not implemented yet.");
  } else if (sensor_info.sensor_type == RosSensorInfo::TEMPERATURE) {
    return absl::UnimplementedError(
        "Temperature sensor type not implemented yet.");
  } else {
    return absl::InvalidArgumentError("Unsupported sensor type.");
  }

  const bool is_disabled = sensor_info.topic_name.empty();

  return SensorInformation(sensor_id, sensor_info.sensor_name, camera_params,
                           camera_t_sensor, pixel_types, dimensions,
                           is_disabled);
}

absl::StatusOr<SensorImage> CreateSensorImage(
    const int64_t sensor_id, RosImage&& image,
    std::optional<CameraParams> camera_params = std::nullopt,
    std::optional<Pose> camera_t_sensor = std::nullopt) {
  // TODO(b/433993510): Revert to header timestamp once
  // https://github.com/akasha-imaging/ips2_driver/issues/172 is fixed.
  // Parse the capture time from the ROS image header.
  // const absl::Time capture_at = FromRosTime(image.header.stamp);
  const absl::Time capture_at = absl::Now();

  // Extract image dimensions.
  const Dimensions dimensions(image.width, image.height);

  if (image.encoding == sensor_msgs::image_encodings::MONO8) {
    return SensorImage(
        sensor_id, capture_at, camera_params, camera_t_sensor,
        CreateImageFromMemory<Gray8u>(dimensions, std::move(image.data)));
  } else if (image.encoding == sensor_msgs::image_encodings::RGB8) {
    return SensorImage(
        sensor_id, capture_at, camera_params, camera_t_sensor,
        CreateImageFromMemory<Rgb8u>(dimensions, std::move(image.data)));
  } else if (image.encoding == sensor_msgs::image_encodings::RGBA8) {
    Image<Rgb8u> rgb_image = ConvertImage<Rgb8u>(
        CreateImageFromMemory<Rgba8u>(dimensions, std::move(image.data)));
    return SensorImage(sensor_id, capture_at, camera_params, camera_t_sensor,
                       std::move(rgb_image));
  } else if (image.encoding == sensor_msgs::image_encodings::TYPE_32FC1) {
    return SensorImage(
        sensor_id, capture_at, camera_params, camera_t_sensor,
        CreateImageFromMemory<Depth32f>(dimensions, std::move(image.data)));
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported sensor_msgs/Image encoding: ", image.encoding));
}

absl::StatusOr<SensorImage> CreateSensorImage(
    const int64_t sensor_id, RosPointCloud2&& point_cloud,
    std::optional<CameraParams> camera_params = std::nullopt,
    std::optional<Pose> camera_t_sensor = std::nullopt) {
  // TODO(b/395343221): add support for xyzrgb point clouds.
  const bool point_cloud_format_supported =
      (point_cloud.fields.size() == 3 || point_cloud.fields.size() == 4) &&
      point_cloud.point_step == point_cloud.fields.size() * 4;
  if (!point_cloud_format_supported) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unsupported sensor_msgs/PointCloud2 format. Expected 3 "
        "or 4 channels with aligned data, but got ",
        point_cloud.fields.size(), " channels with a step size of ",
        point_cloud.point_step, " bytes."));
  }
  // TODO(b/433993510): Revert to header timestamp once
  // https://github.com/akasha-imaging/ips2_driver/issues/172 is fixed.
  // Parse the capture time from the ROS image header.
  // const absl::Time capture_at = FromRosTime(point_cloud.header.stamp);
  const absl::Time capture_at = absl::Now();

  // Extract image dimensions.
  const Dimensions dimensions(point_cloud.width, point_cloud.height);

  // determine whether there is a normal point cloud stored in the
  // RosPointCloud2 msg
  if (point_cloud.fields.size() == 3 &&
      absl::c_all_of(point_cloud.fields,
                     [](const sensor_msgs::msg::PointField& field) {
                       return absl::StrContains(
                           absl::AsciiStrToLower(field.name), "normal");
                     })) {
    return SensorImage(sensor_id, capture_at, camera_params, camera_t_sensor,
                       CreateImageFromMemory<Normal32f>(
                           dimensions, std::move(point_cloud.data)));
  }

  if (point_cloud.fields.size() == 4) {
    Image<Point32f> point_image =
        ConvertImage<Point32f>(CreateImageFromMemory<Generic32f4>(
            dimensions, std::move(point_cloud.data)));
    return SensorImage(sensor_id, capture_at, camera_params, camera_t_sensor,
                       std::move(point_image));
  }
  return SensorImage(
      sensor_id, capture_at, camera_params, camera_t_sensor,
      CreateImageFromMemory<Point32f>(dimensions, std::move(point_cloud.data)));
}

}  // namespace

absl::StatusOr<std::unique_ptr<ImageSource>> RosImageSource::Create(
    const CameraIdentifier& camera_identifier) {
  const CameraIdentifier::Ros* ros =
      std::get_if<CameraIdentifier::Ros>(&camera_identifier.driver);
  if (ros == nullptr || ros->device_id.empty()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "ROS camera creation triggered without the "
              "necessary configuration 'ros'.";
  }

  // As we transition from an IPS2-specific ROS discovery system to a more
  // generic ROS discovery service, we need to continue to support solutions
  // in which the ips2 device_type is not specified. As a transition measure,
  // we will supply "ips2" as the driver_type if it is not specified in the
  // solution. As those older solutions are migrated, we can remove this
  // transition measure.
  std::string driver_type = ros->driver_type;
  if (driver_type.empty()) {
    LOG(WARNING) << "ROS camera driver_type is empty. Using 'ips2'";
    driver_type = "ips2";
  }

  std::unique_ptr<RosImageSource> ros_camera(
      new RosImageSource(driver_type, ros->device_id));
  INTR_RETURN_IF_ERROR(ros_camera->Init());

  return ros_camera;
}

RosImageSource::RosImageSource(std::string_view driver_type,
                               std::string_view device_id)
    : ImageSource(), driver_type_(driver_type), device_id_(device_id) {}

RosImageSource::~RosImageSource() { StopStream().IgnoreError(); }

absl::Status RosImageSource::Init() {
  LOG(INFO) << "RosImageSource::Init() with driver_type: " << driver_type_
            << " and device_id: " << device_id_;
  {
    absl::MutexLock lock(mutex_);
    pubsub_ = std::make_unique<PubSub>();
    // Wait 1s for pubsub to initialize.
    absl::SleepFor(absl::Milliseconds(kPubSubInitTimeoutMs));
  }

  INTR_RETURN_IF_ERROR(DescribeCameraSensorsImpl().status());

  return absl::OkStatus();
}

absl::StatusOr<std::vector<SensorInformation>>
RosImageSource::DescribeCameraSensorsImpl() const {
  VLOG(1) << "RosImageSource::DescribeCameraSensorsImpl()";

  INTR_ASSIGN_OR_RETURN(Describe::Response describe_response, CallDescribe());

  absl::MutexLock lock(sensor_info_mutex_);
  sensor_id_by_topic_.clear();
  sensor_info_by_id_.clear();

  // TODO(b/395343221): generalize when we work on the adapted camera ros node.
  // If the camera only has one sensor, we use kFallbackSensorId otherwise we
  // begin enumerating them, in order, from 1.
  int64_t sensor_id =
      describe_response.sensors.size() <= 1 ? kFallbackSensorId : 1;
  for (const auto& sensor_info : describe_response.sensors) {
    if (!sensor_info.topic_name.empty()) {
      sensor_id_by_topic_.try_emplace(sensor_info.topic_name, sensor_id);
    }
    sensor_info_by_id_.try_emplace(sensor_id, sensor_info);

    VLOG(1) << "sensor_id: " << sensor_id;
    VLOG(1) << "sensor_name: " << sensor_info.sensor_name;
    VLOG(1) << "topic_name: " << sensor_info.topic_name;
    VLOG(1) << "sensor_type: " << sensor_info.sensor_type;

    sensor_id++;
  }

  std::vector<SensorInformation> sensors;
  sensors.reserve(sensor_info_by_id_.size());
  for (const auto& [sensor_id, sensor_info] : sensor_info_by_id_) {
    absl::StatusOr<SensorInformation> or_sensor_information =
        CreateSensorInformation(sensor_id, sensor_info);
    if (!or_sensor_information.ok()) {
      if (or_sensor_information.status().code() !=
          absl::StatusCode::kUnimplemented) {
        LOG(ERROR) << "Failed to create sensor information: "
                   << or_sensor_information.status();
      }
      continue;
    }
    sensors.push_back(or_sensor_information.value());
  }
  return sensors;
}

absl::StatusOr<CaptureResult> RosImageSource::CaptureImpl(
    const absl::Duration timeout,
    const absl::flat_hash_set<int64_t>& sensor_ids) {
  VLOG(2) << "RosImageSource::CaptureImpl(" << timeout << ")";
  const absl::Time start_at = absl::Now();
  const absl::Time deadline_at = start_at + timeout;

  INTR_ASSIGN_OR_RETURN(Snapshot::Response snapshot_response,
                        CallSnapshot(ToRosDuration(timeout)));
  const absl::Time end_at = absl::Now();

  if (end_at > deadline_at) {
    return absl::DeadlineExceededError(absl::StrCat(
        "Capture exceeded the deadline: ", absl::FormatTime(end_at), " > ",
        absl::FormatTime(deadline_at)));
  }

  const std::optional<absl::flat_hash_set<int64_t>> active_stream_sensor_ids =
      GetActiveStreamSensorIdsIfStreaming();
  const auto should_keep_sensor = [&](int64_t sensor_id) {
    if (sensor_ids.empty() || sensor_ids.contains(sensor_id)) {
      return true;
    }
    if (!active_stream_sensor_ids.has_value()) {
      return false;
    }
    return active_stream_sensor_ids->empty() ||
           active_stream_sensor_ids->contains(sensor_id);
  };

  absl::MutexLock lock(sensor_info_mutex_);

  // TODO(b/395343221): handle imus and temperatures
  std::vector<SensorImage> sensor_images;
  sensor_images.reserve(snapshot_response.images.size() +
                        snapshot_response.point_clouds.size());

  for (ImageSnapshot& image_snapshot : snapshot_response.images) {
    if (image_snapshot.topic_name.empty()) {
      LOG(ERROR) << "ImageSnapshot found with empty topic_name";
      continue;
    }
    const auto it_sensor_id =
        sensor_id_by_topic_.find(image_snapshot.topic_name);
    if (it_sensor_id == sensor_id_by_topic_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Unable to find sensor with topic_name: ",
                       image_snapshot.topic_name));
    }
    const int64_t sensor_id = it_sensor_id->second;
    if (!should_keep_sensor(sensor_id)) {
      continue;
    }
    const auto it_sensor_info = sensor_info_by_id_.find(sensor_id);
    INTR_RET_CHECK(it_sensor_info != sensor_info_by_id_.end());

    std::optional<CameraParams> camera_params =
        CreateCameraParams(image_snapshot.camera_info);
    std::optional<Pose> camera_t_sensor =
        CreatePose(it_sensor_info->second.camera_t_sensor);

    INTR_ASSIGN_OR_RETURN(
        SensorImage sensor_image,
        CreateSensorImage(sensor_id, std::move(image_snapshot.image),
                          camera_params, camera_t_sensor));
    sensor_images.push_back(std::move(sensor_image));
  }

  for (PointCloud2Snapshot& point_cloud_snapshot :
       snapshot_response.point_clouds) {
    const auto it_sensor_id =
        sensor_id_by_topic_.find(point_cloud_snapshot.topic_name);
    INTR_RET_CHECK(it_sensor_id != sensor_id_by_topic_.end());
    const int64_t sensor_id = it_sensor_id->second;
    if (!should_keep_sensor(sensor_id)) {
      continue;
    }
    const auto it_sensor_info = sensor_info_by_id_.find(sensor_id);
    INTR_RET_CHECK(it_sensor_info != sensor_info_by_id_.end());

    std::optional<Pose> camera_t_sensor =
        CreatePose(it_sensor_info->second.camera_t_sensor);

    INTR_ASSIGN_OR_RETURN(
        SensorImage sensor_image,
        CreateSensorImage(sensor_id,
                          std::move(point_cloud_snapshot.point_cloud),
                          /*camera_params=*/std::nullopt, camera_t_sensor));
    sensor_images.push_back(std::move(sensor_image));
  }

  return CaptureResult{
      .capture_at = start_at,
      .sensor_images = std::move(sensor_images),
      .capture_duration = end_at - start_at,
  };
}

absl::StatusOr<CameraSettingAccess> RosImageSource::ReadCameraSettingAccessImpl(
    absl::string_view name) const {
  VLOG(1) << "RosImageSource::ReadCameraSettingAccessImpl(" << name << ")";
  INTR_ASSIGN_OR_RETURN(
      const DescribeParameters::Response describe_parameters_response,
      CallDescribeParameters({std::string(name)}));

  if (describe_parameters_response.descriptors.size() != 1) {
    return absl::NotFoundError(
        absl::StrCat("Could not read camera setting access for ", name,
                     " invalid response size: ",
                     describe_parameters_response.descriptors.size()));
  }

  const ParameterDescriptor& descriptor =
      describe_parameters_response.descriptors[0];
  return CreateCameraSettingAccess(
      name, descriptor.read_only ? CameraSettingAccess::Mode::kRead
                                 : CameraSettingAccess::Mode::kReadWrite);
};

absl::StatusOr<CameraSettingProperties>
RosImageSource::ReadCameraSettingPropertiesImpl(absl::string_view name) const {
  VLOG(1) << "RosImageSource::ReadCameraSettingPropertiesImpl(" << name << ")";
  INTR_ASSIGN_OR_RETURN(
      const DescribeParameters::Response describe_parameters_response,
      CallDescribeParameters({std::string(name)}));

  if (describe_parameters_response.descriptors.size() != 1) {
    return absl::NotFoundError(
        absl::StrCat("Could not read camera setting properties for ", name,
                     " invalid response size: ",
                     describe_parameters_response.descriptors.size()));
  }

  const ParameterDescriptor& descriptor =
      describe_parameters_response.descriptors[0];
  return CreateCameraSettingPropertiesFromDescriptor(name, descriptor);
};

absl::StatusOr<CameraSetting> RosImageSource::ReadCameraSettingImpl(
    absl::string_view name) const {
  VLOG(1) << "RosImageSource::ReadCameraSettingImpl(" << name << ")";
  INTR_ASSIGN_OR_RETURN(const GetParameters::Response get_parameters_response,
                        CallGetParameters({std::string(name)}));

  if (get_parameters_response.values.size() != 1) {
    return absl::NotFoundError(absl::StrCat(
        "Could not read camera setting ", name,
        " invalid response size: ", get_parameters_response.values.size()));
  }

  const ParameterValue& parameter_value = get_parameters_response.values[0];
  return CreateCameraSettingFromParameterValue(name, parameter_value);
};

absl::Status RosImageSource::UpdateCameraSettingImpl(
    const CameraSetting& camera_setting) {
  const std::string& name = camera_setting.name;
  VLOG(1) << "RosImageSource::UpdateCameraSettingImpl(" << name << ")";
  INTR_ASSIGN_OR_RETURN(Parameter parameter, CreateParameter(camera_setting));

  INTR_ASSIGN_OR_RETURN(const SetParameters::Response set_parameters_response,
                        CallSetParameters({parameter}));

  if (set_parameters_response.results.size() != 1) {
    return absl::NotFoundError(absl::StrCat(
        "Could not update camera setting ", name,
        " invalid response size: ", set_parameters_response.results.size()));
  }

  const SetParametersResult& result = set_parameters_response.results[0];
  if (!result.successful) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Could not update camera setting ", name, ": ", result.reason));
  }

  return absl::OkStatus();
}

absl::StatusOr<ListParameters::Response> RosImageSource::CallListParameters(
    const uint64_t depth, absl::Span<const std::string> prefixes) const {
  ListParameters::Request request;
  request.depth = depth;
  request.prefixes.reserve(prefixes.size());
  for (const auto& prefix : prefixes) {
    request.prefixes.push_back(prefix);
  }

  INTR_ASSIGN_OR_RETURN(
      const ListParameters::Response response,
      CallRosService<ListParameters::Response>("list_parameters", request));

  return response;
}

absl::StatusOr<DescribeParameters::Response>
RosImageSource::CallDescribeParameters(
    absl::Span<const std::string> parameter_names) const {
  DescribeParameters::Request request;
  request.names.reserve(parameter_names.size());
  for (const auto& name : parameter_names) {
    request.names.push_back(name);
  }

  INTR_ASSIGN_OR_RETURN(DescribeParameters::Response response,
                        CallRosService<DescribeParameters::Response>(
                            "describe_parameters", request));

  return response;
}

absl::StatusOr<GetParameters::Response> RosImageSource::CallGetParameters(
    absl::Span<const std::string> parameter_names) const {
  GetParameters::Request request;
  request.names.reserve(parameter_names.size());
  for (const auto& name : parameter_names) {
    request.names.push_back(name);
  }

  INTR_ASSIGN_OR_RETURN(
      const GetParameters::Response response,
      CallRosService<GetParameters::Response>("get_parameters", request));

  return response;
}

absl::StatusOr<SetParameters::Response> RosImageSource::CallSetParameters(
    absl::Span<const Parameter> parameters) const {
  SetParameters::Request request;
  request.parameters.reserve(parameters.size());
  for (const auto& parameter : parameters) {
    request.parameters.push_back(parameter);
  }

  INTR_ASSIGN_OR_RETURN(
      const SetParameters::Response response,
      CallRosService<SetParameters::Response>("set_parameters", request));

  // Note: let the caller handle any individual parameter failures.
  return response;
}

absl::StatusOr<SetParametersAtomically::Response>
RosImageSource::CallSetParametersAtomically(
    absl::Span<const Parameter> parameters) const {
  SetParametersAtomically::Request request;
  request.parameters.reserve(parameters.size());
  for (const auto& parameter : parameters) {
    request.parameters.push_back(parameter);
  }

  INTR_ASSIGN_OR_RETURN(const SetParametersAtomically::Response response,
                        CallRosService<SetParametersAtomically::Response>(
                            "set_parameters_atomically", request));

  if (!response.result.successful) {
    return absl::InternalError(absl::StrCat(
        "Failed to set parameters atomically: ", response.result.reason));
  }

  return response;
}

absl::StatusOr<Describe::Response> RosImageSource::CallDescribe() const {
  Describe::Request request;

  INTR_ASSIGN_OR_RETURN(
      const Describe::Response response,
      CallRosService<Describe::Response>("describe", request));

  if (!response.success) {
    return absl::InternalError(
        absl::StrCat("Failed to describe camera: ", response.error_message));
  }

  return response;
}

absl::StatusOr<Snapshot::Response> RosImageSource::CallSnapshot(
    const RosDuration& timeout, const uint32_t capture_policy) const {
  Snapshot::Request request;
  request.timeout = timeout;
  request.capture_policy = capture_policy;

  INTR_ASSIGN_OR_RETURN(
      const Snapshot::Response response,
      CallRosService<Snapshot::Response>("snapshot", request));

  if (!response.success) {
    return absl::InternalError(
        absl::StrCat("Failed to capture snapshot: ", response.error_message));
  }

  return response;
}

absl::Status RosImageSource::GetFaultsStatus() const {
  // Ensure that communication with the driver works.
  INTR_RETURN_IF_ERROR(DescribeCameraSensorsImpl().status());
  return absl::OkStatus();
}

absl::Status RosImageSource::ClearFaults() { return absl::OkStatus(); };

REGISTER_IMAGE_SOURCE(RosImageSource, "ros", RosImageSource::Create);

}  // namespace intrinsic::perception
