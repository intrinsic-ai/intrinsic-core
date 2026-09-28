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

#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
#include "snapshot_interfaces/msg/discovered_camera.hpp"
#include "snapshot_interfaces/msg/image_snapshot.hpp"
#include "snapshot_interfaces/msg/imu_snapshot.hpp"
#include "snapshot_interfaces/msg/point_cloud2_snapshot.hpp"
#include "snapshot_interfaces/msg/sensor_info.hpp"
#include "snapshot_interfaces/msg/temperature_snapshot.hpp"
#include "snapshot_interfaces/srv/describe.hpp"
#include "snapshot_interfaces/srv/discover.hpp"
#include "snapshot_interfaces/srv/legacy_discover.hpp"
#include "snapshot_interfaces/srv/snapshot.hpp"

namespace {

template <typename T>
bool RoundTrip(const T& input) {
  rclcpp::Serialization<T> serialization;
  rclcpp::SerializedMessage buffer;
  serialization.serialize_message(&input, &buffer);
  T output;
  serialization.deserialize_message(&buffer, &output);
  return input == output;
}

}  // namespace

int main() {
  snapshot_interfaces::srv::Snapshot::Request request;
  request.capture_policy = request.WAIT_FOR_TRIGGER_TIME;
  request.trigger_time.sec = 42;
  request.timeout.nanosec = 123456;
  snapshot_interfaces::srv::Snapshot::Response response;
  response.success = true;
  response.images.resize(1);
  response.images[0].topic_name = "camera/image";
  response.images[0].image.width = 2;
  response.images[0].image.height = 1;
  response.images[0].image.encoding = "mono8";
  response.images[0].image.data = {12, 34};
  response.point_clouds.resize(1);
  response.imus.resize(1);
  response.temperatures.resize(1);
  response.temperatures[0].temperature.temperature = 24.5;
  bool ok = RoundTrip(request) && RoundTrip(response);
  ok &= RoundTrip(snapshot_interfaces::msg::DiscoveredCamera{});
  ok &= RoundTrip(snapshot_interfaces::msg::ImageSnapshot{});
  ok &= RoundTrip(snapshot_interfaces::msg::ImuSnapshot{});
  ok &= RoundTrip(snapshot_interfaces::msg::PointCloud2Snapshot{});
  ok &= RoundTrip(snapshot_interfaces::msg::SensorInfo{});
  ok &= RoundTrip(snapshot_interfaces::msg::TemperatureSnapshot{});
  ok &= RoundTrip(snapshot_interfaces::srv::Describe::Request{});
  ok &= RoundTrip(snapshot_interfaces::srv::Describe::Response{});
  ok &= RoundTrip(snapshot_interfaces::srv::Discover::Request{});
  ok &= RoundTrip(snapshot_interfaces::srv::Discover::Response{});
  ok &= RoundTrip(snapshot_interfaces::srv::LegacyDiscover::Request{});
  ok &= RoundTrip(snapshot_interfaces::srv::LegacyDiscover::Response{});
  ok &= rosidl_typesupport_cpp::get_service_type_support_handle<
            snapshot_interfaces::srv::Snapshot>() != nullptr;
  return ok ? 0 : 1;
}
