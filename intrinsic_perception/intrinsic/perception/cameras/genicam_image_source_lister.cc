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

#include "intrinsic/perception/cameras/genicam_image_source_lister.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/cameras/aravis/aravis_utils.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/image_source_lister.h"

namespace intrinsic::perception {

absl::StatusOr<std::vector<CameraIdentifier>>
GenICamImageSourceLister::ListAvailableCameras() {
  const std::vector<std::string> device_ids = ListAvailableDeviceIds();
  std::vector<CameraIdentifier> camera_identifiers;
  camera_identifiers.reserve(device_ids.size());
  LOG(INFO) << "ListAvailableCameras() returned " << device_ids.size()
            << " Genicam cameras";
  for (size_t device_idx = 0; device_idx < device_ids.size(); ++device_idx) {
    LOG(INFO) << "Camera " << device_idx << ": " << device_ids[device_idx];
    camera_identifiers.push_back({.driver = CameraIdentifier::GenICam{
                                      .device_id = device_ids[device_idx]}});
  }
  return camera_identifiers;
}

REGISTER_IMAGE_SOURCE_LISTER(GenICamImageSourceLister, "genicam");

}  // namespace intrinsic::perception
