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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_CAMERA_PLUGIN_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_CAMERA_PLUGIN_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gz/msgs/MessageTypes.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/System.hh"
#include "gz/transport/Node.hh"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/camera_service.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/triggered_camera_client.h"
#include "sdf/Element.hh"
#include "sdf/Sensor.hh"

ABSL_DECLARE_FLAG(bool, stream_simulated_images_to_pubsub);
ABSL_DECLARE_FLAG(absl::Duration, render_engine_initialization_timeout);

namespace intrinsic::simulation {

// A plugin for serving RGB or RGB + Depth camera images from Gazebo to the
// simulation camera server.
// The plugin initializes the camera server if it is not running already.
// The plugin must be included in the model SDF as follows:
// <model name="model_name">
//   ...
//   <link name="link_name">
//     ...
//     <sensor name="sensor_name" type="rgbd">
//       <topic>/some/image/topic</topic>
//       ...
//       <update_rate>...</update_rate>
//       <camera name="camera_name">
//         ...
//       </camera>
//       <plugin
//         filename="static://intrinsic::simulation::CameraPlugin"
//         name="intrinsic::simulation::CameraPlugin">
//         <camera_identifier>
//           genicam {
//             device_id: "basler_device_id"
//           }
//         </camera_identifier>
//         <image type="intensity" id=1 />
//         <image type="point" id=2 />
//       </plugin>
//     </sensor>
//   </link>
// </model>
//
// The supported sensor types are 'camera' and 'rgbd'.
//
// The plugin takes the following parameters:
//   <camera_identifier>: Serialized CameraIdentifier proto which is
//     automatically added during the world to sdf conversion based on the
//     associated camera configuration and should not be set explicitly outside
//     of tests.
//   <image>: Optional sensor id override for a particular image type.
//     - `camera` sensor: Only 'intensity' image type is supported for `camera`
//       type sensors. If no `<image>` override is provided, a default sensor id
//       will be used.
//     - `rgbd` sensor: Image types 'intensity', 'point' and 'depth' are
//       supported for `rgbd` type sensors. If no `<image>` override is
//       provided, 'intensity' and 'point' sensor images will be available with
//       default sensor ids. Specify overrides for any or both of `point` and
//       `depth` image types to selectively enable them.
//     - Sensor ids must be unique across all image types.
class CameraPlugin : public gz::sim::System, public gz::sim::ISystemConfigure {
 public:
  using PixelTypeToIdMap = absl::flat_hash_map<perception::PixelType, int64_t>;
  ~CameraPlugin() override;

  void Configure(const gz::sim::Entity& entity,
                 const std::shared_ptr<const sdf::Element>& sdf,
                 gz::sim::EntityComponentManager& ecm,
                 gz::sim::EventManager& eventMgr) final;

 protected:
  absl::Status LoadSensorSdf(const sdf::Sensor& sensor_sdf);
  void InitCameraConnection();
  void OnImage(const gz::msgs::Image& image)
      ABSL_LOCKS_EXCLUDED(image_callback_mutex_);
  void SetSensorImages() ABSL_EXCLUSIVE_LOCKS_REQUIRED(image_callback_mutex_);

  std::string model_name_;
  sdf::SensorType sensor_type_;
  std::string intensity_topic_;
  std::string range_topic_;
  gz::transport::Node node_;
  gz::sim::Entity parent_entity_ = gz::sim::kNullEntity;

  // Camera identifier for the camera resource.
  perception::CameraIdentifier camera_identifier_;

  // We use an optional since the CameraPlugin is required to be default
  // constructible.
  std::optional<intrinsic::perception::CameraParams> camera_params_ =
      std::nullopt;
  Pose3d camera_t_sensor_;
  std::unique_ptr<intrinsic::perception::GazeboCameraConnection> connection_;

  PixelTypeToIdMap sensor_ids_by_pixel_type_;

  // Mutex to protect state variables updated in the `OnImage` callback.
  absl::Mutex image_callback_mutex_;

  std::vector<intrinsic::perception::SensorImage> sensor_images_
      ABSL_GUARDED_BY(image_callback_mutex_);

  // A unique set of sensors that have been requested from the camera server but
  // have not yet been received.
  absl::flat_hash_set<int64_t> pending_sensor_ids_
      ABSL_GUARDED_BY(image_callback_mutex_);

  // Time at which last image was generated. Note that this differs from the
  // sim time at which the image generation started.
  absl::Time acquisition_time_;

  std::unique_ptr<Publisher> image_pub_ = nullptr;
  std::unique_ptr<PubSub> pubsub_ = nullptr;

  std::optional<TriggeredCameraClient> triggered_camera_client_;
  std::string trigger_topic_;
};

}  // namespace intrinsic::simulation

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_CAMERA_PLUGIN_H_
