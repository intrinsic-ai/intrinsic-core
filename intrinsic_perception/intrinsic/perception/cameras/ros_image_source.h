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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_ROS_IMAGE_SOURCE_H_
#define INTRINSIC_PERCEPTION_CAMERAS_ROS_IMAGE_SOURCE_H_

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/pubsub_ros.h"
#include "intrinsic/util/status/status_macros.h"
#include "rcl_interfaces/msg/parameter.hpp"
#include "rcl_interfaces/srv/describe_parameters.hpp"
#include "rcl_interfaces/srv/get_parameters.hpp"
#include "rcl_interfaces/srv/list_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters_atomically.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"
#include "rmw/serialized_message.h"
#include "rosidl_runtime_cpp/traits.hpp"
#include "snapshot_interfaces/msg/detail/sensor_info__struct.hpp"
#include "snapshot_interfaces/msg/sensor_info.hpp"
#include "snapshot_interfaces/srv/describe.hpp"
#include "snapshot_interfaces/srv/detail/describe__struct.hpp"
#include "snapshot_interfaces/srv/detail/snapshot__struct.hpp"
#include "snapshot_interfaces/srv/snapshot.hpp"

namespace intrinsic::perception {

class RosImageSource : public ImageSource {
 public:
  static absl::StatusOr<std::unique_ptr<ImageSource>> Create(
      const CameraIdentifier& camera_identifier);

  ~RosImageSource() override;

 protected:
  explicit RosImageSource(std::string_view driver_type,
                          std::string_view device_id);

 private:
  absl::Status Init() ABSL_LOCKS_EXCLUDED(mutex_, sensor_info_mutex_);

  absl::StatusOr<std::vector<SensorInformation>> DescribeCameraSensorsImpl()
      const override ABSL_LOCKS_EXCLUDED(mutex_, sensor_info_mutex_);

  absl::StatusOr<CaptureResult> CaptureImpl(
      absl::Duration timeout,
      const absl::flat_hash_set<int64_t>& sensor_ids) override
      ABSL_LOCKS_EXCLUDED(mutex_, sensor_info_mutex_);

  absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccessImpl(
      absl::string_view name) const override;

  absl::StatusOr<CameraSettingProperties> ReadCameraSettingPropertiesImpl(
      absl::string_view name) const override;

  absl::StatusOr<CameraSetting> ReadCameraSettingImpl(
      absl::string_view name) const override;

  absl::Status UpdateCameraSettingImpl(
      const CameraSetting& camera_setting) override;

  // ListParameters.srv: List all supported parameters.
  absl::StatusOr<rcl_interfaces::srv::ListParameters::Response>
  CallListParameters(
      uint64_t depth =
          rcl_interfaces::srv::ListParameters::Request::DEPTH_RECURSIVE,
      absl::Span<const std::string> prefixes = {}) const;

  // DescribeParameters.srv: Describe zero or more parameters.
  absl::StatusOr<rcl_interfaces::srv::DescribeParameters::Response>
  CallDescribeParameters(absl::Span<const std::string> parameter_names) const;

  // GetParameters.srv: Get zero or more parameters.
  absl::StatusOr<rcl_interfaces::srv::GetParameters::Response>
  CallGetParameters(absl::Span<const std::string> parameter_names) const;

  // SetParameters.srv: Set zero or more parameters. Caller is responsible for
  // handling individual parameter errors.
  absl::StatusOr<rcl_interfaces::srv::SetParameters::Response>
  CallSetParameters(
      absl::Span<const rcl_interfaces::msg::Parameter> parameters) const;

  // SetParametersAtomically.srv: Set zero or more parameters atomically.
  absl::StatusOr<rcl_interfaces::srv::SetParametersAtomically::Response>
  CallSetParametersAtomically(
      absl::Span<const rcl_interfaces::msg::Parameter> parameters) const;

  // Describe.srv: Describe the camera to get all sensor info.
  absl::StatusOr<snapshot_interfaces::srv::Describe::Response> CallDescribe()
      const;

  // Snapshot.srv: Capture sensor data snapshot from the image source.
  absl::StatusOr<snapshot_interfaces::srv::Snapshot::Response> CallSnapshot(
      const builtin_interfaces::msg::Duration& timeout,
      uint32_t capture_policy =
          snapshot_interfaces::srv::Snapshot::Request::WAIT_FOR_NEXT) const;

  absl::Status GetFaultsStatus() const override;

  absl::Status ClearFaults() override;

  struct ServiceCallOptions {
    std::optional<std::string> domain;
    std::optional<absl::Duration> timeout;
  };

  // Call a ros service using the pubsub client.
  template <typename ResponseT, typename RequestT>
  absl::StatusOr<ResponseT> CallRosService(
      std::string_view local_service_name, const RequestT& request,
      const ServiceCallOptions& call_options = {}) const;

  const std::string driver_type_;
  const std::string device_id_;

  mutable absl::Mutex mutex_;
  std::unique_ptr<PubSub> pubsub_ ABSL_GUARDED_BY(mutex_);

  mutable absl::Mutex sensor_info_mutex_;
  mutable absl::flat_hash_map<std::string, int64_t> sensor_id_by_topic_
      ABSL_GUARDED_BY(sensor_info_mutex_);
  mutable absl::btree_map<int64_t, snapshot_interfaces::msg::SensorInfo>
      sensor_info_by_id_ ABSL_GUARDED_BY(sensor_info_mutex_);
};

template <typename ResponseT, typename RequestT>
absl::StatusOr<ResponseT> RosImageSource::CallRosService(
    std::string_view local_service_name, const RequestT& request,
    const ServiceCallOptions& call_options) const {
  static_assert(rosidl_generator_traits::is_service_request<RequestT>::value,
                "Request must be a ros service request");
  static_assert(rosidl_generator_traits::is_service_response<ResponseT>::value,
                "Response must be a ros service response");

  const std::string domain = call_options.domain.value_or("0");
  const std::string global_service_name =
      absl::StrCat(domain, "/", driver_type_, "_", device_id_, "/",
                   local_service_name, "/**");
  const PubSub::QueryOptions query_options = {
      .timeout = call_options.timeout,
  };

  // Serialize request.
  rclcpp::Serialization<RequestT> request_serialization;
  rclcpp::SerializedMessage serialized_request;
  try {
    request_serialization.serialize_message(static_cast<const void*>(&request),
                                            &serialized_request);
  } catch (std::exception& ex) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize request: ", ex.what(),
                     " for service ", global_service_name));
  }

  // Call ROS 2 service.
  rclcpp::Serialization<ResponseT> response_serialization;
  rclcpp::SerializedMessage serialized_response;
  {
    absl::MutexLock lock(mutex_);
    INTR_ASSIGN_OR_RETURN(
        serialized_response,
        pubsub_->CallOne<rclcpp::SerializedMessage>(
            global_service_name, serialized_request, query_options),
        _.LogError() << "Failed to call ros service " << global_service_name);
  }

  if (serialized_response.size() == 0) {
    return absl::UnavailableError(
        absl::StrCat("Failed to call ros service ", global_service_name));
  }

  // Deserialize response.
  ResponseT response;
  try {
    response_serialization.deserialize_message(&serialized_response,
                                               static_cast<void*>(&response));
  } catch (std::exception& ex) {
    return absl::InternalError(
        absl::StrCat("Failed to deserialize response: ", ex.what(),
                     " for service ", global_service_name));
  }

  return std::move(response);
}

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_ROS_IMAGE_SOURCE_H_
