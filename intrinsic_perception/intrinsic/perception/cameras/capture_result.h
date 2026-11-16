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

#ifndef INTRINSIC_PERCEPTION_PUBLIC_CAPTURE_RESULT_H_
#define INTRINSIC_PERCEPTION_PUBLIC_CAPTURE_RESULT_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/time/time.h"
#include "intrinsic/perception/cameras/camera_config.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/post_processing.h"

namespace intrinsic::perception {

// A container for sensor images and any associated capture metadata.
struct CaptureResult {
  absl::Time capture_at;
  std::vector<SensorImage> sensor_images;
  std::optional<absl::Duration> capture_duration;
};

// Parameters for Capture methods.
struct CaptureArgs {
  constexpr static absl::Duration kDefaultTimeout = absl::Milliseconds(500);

  // Maximum time allowed for grabbing the images from the sensors.
  // The parameter must be read as a suggestion about the maximal time a call
  // to Capture() should take. As it is not implemented by all drivers some
  // cameras/sensors will not obey this suggestion and may take longer.
  // Important: Make sure that the specified 'timeout' is larger than the user
  // specified exposure time.
  absl::Duration timeout = kDefaultTimeout;
  // If provided, only return data from these sensors. The sensor ids are
  // defined through the DescribeCamera method of the respective camera
  // driver.
  std::vector<int64_t> sensor_ids;
  // Camera config to use for the returned sensor images.
  CameraConfig camera_config;
  // Optional post processing to apply to the returned sensor images. If not
  // specified, images will always be undistorted, if possible.
  PostProcessingBySensorId post_processing_by_sensor_id;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_PUBLIC_CAPTURE_RESULT_H_
