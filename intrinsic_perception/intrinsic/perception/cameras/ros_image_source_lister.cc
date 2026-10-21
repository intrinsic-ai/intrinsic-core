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

#include "intrinsic/perception/cameras/ros_image_source_lister.h"

#include <exception>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/image_source_lister.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/pubsub_ros.h"
#include "intrinsic/util/status/status_macros.h"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"
#include "snapshot_interfaces/msg/detail/discovered_camera__struct.hpp"
#include "snapshot_interfaces/srv/detail/discover__struct.hpp"

namespace intrinsic::perception {
namespace {

using ::snapshot_interfaces::srv::Discover;

constexpr std::string_view kDiscoverServiceName = "0/cameras/discover/**";

absl::StatusOr<Discover::Response> CallDiscover(PubSub& pubsub) {
  const Discover::Request request;
  const PubSub::QueryOptions query_options = {};

  // Serialize the request.
  rclcpp::Serialization<Discover::Request> request_serialization;
  rclcpp::SerializedMessage serialized_request;
  try {
    request_serialization.serialize_message(static_cast<const void*>(&request),
                                            &serialized_request);
  } catch (std::exception& ex) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize request: ", ex.what(),
                     " for service ", kDiscoverServiceName));
  }

  // Call the ros service.
  INTR_ASSIGN_OR_RETURN(
      rclcpp::SerializedMessage serialized_response,
      pubsub.CallOne<rclcpp::SerializedMessage>(
          kDiscoverServiceName, serialized_request, query_options),
      _.LogError() << "Failed to call ros service " << kDiscoverServiceName);
  if (serialized_response.size() == 0) {
    return absl::UnavailableError(
        absl::StrCat("Failed to call ros service ", kDiscoverServiceName));
  }

  // Deserialize the response.
  rclcpp::Serialization<Discover::Response> response_serialization;
  Discover::Response response;
  try {
    response_serialization.deserialize_message(&serialized_response,
                                               static_cast<void*>(&response));
  } catch (std::exception& ex) {
    return absl::InternalError(
        absl::StrCat("Failed to deserialize response: ", ex.what(),
                     " for service ", kDiscoverServiceName));
  }

  if (!response.success) {
    return absl::InternalError(
        absl::StrCat("Discover service ", kDiscoverServiceName,
                     " returned with failure: ", response.error_message));
  }

  return response;
}

}  // namespace

RosImageSourceLister::RosImageSourceLister()
    : pubsub_(std::make_unique<PubSub>()) {}

absl::StatusOr<std::vector<CameraIdentifier>>
RosImageSourceLister::ListAvailableCameras() {
  std::vector<CameraIdentifier> camera_identifiers;
  absl::StatusOr<Discover::Response> response;
  {
    absl::MutexLock lock(mutex_);
    response = CallDiscover(*pubsub_);
  }
  if (!response.ok()) {
    if (response.status().code() == absl::StatusCode::kUnavailable) {
      return camera_identifiers;
    }
    return response.status();
  }
  camera_identifiers.reserve(response->cameras.size());
  for (const snapshot_interfaces::msg::DiscoveredCamera& camera :
       response->cameras) {
    camera_identifiers.push_back(
        {.driver = CameraIdentifier::Ros{.driver_type = camera.driver_type,
                                         .device_id = camera.camera_id}});
  }

  return camera_identifiers;
}

REGISTER_IMAGE_SOURCE_LISTER(RosImageSourceLister, "ros");

}  // namespace intrinsic::perception
