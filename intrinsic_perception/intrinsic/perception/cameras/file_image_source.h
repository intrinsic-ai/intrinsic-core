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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_FILE_IMAGE_SOURCE_H_
#define INTRINSIC_PERCEPTION_CAMERAS_FILE_IMAGE_SOURCE_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image_file_reference.h"
#include "third_party/imported/cpp_libraries/clock/clock.h"

namespace intrinsic {
namespace perception {

constexpr std::array<const std::string_view, 4> kAllowedImageExtensions{
    "jpeg", "jpg", "png", "webp"};

// Implementation of an ImageSource which streams image files from disk.
class FileImageSource final : public ImageSource {
 public:
  // The directory based image source always returns rgb8u images, so we use the
  // default intensity sensor id of 0 for them.
  static constexpr int64_t kRgb8uSensorId = 0;
  static constexpr int64_t kGray8uSensorId = 1;
  static constexpr int64_t kDepth32fSensorId = 2;
  static constexpr int64_t kPoint32fSensorId = 3;
  static constexpr int64_t kNormal32fSensorId = 4;

  // Creates a camera which returns frames from a directory matched against
  // optional sensor_references. If sensor_references is empty, all supported
  // images in the directory will be returned in alphabetical order as intensity
  // sensor images. Once all images have been served, starts again from the
  // first one if loop_files is true.
  static absl::StatusOr<std::unique_ptr<FileImageSource>> FromDirectory(
      std::string_view directory,
      const std::vector<ImageFileReference>& sensor_references = {},
      int64_t start_index = 0, bool remove_incomplete_frames = false,
      bool loop_files = false);

  // Creates a camera which returns frames referenced by `sensor_files_lists`.
  // Each inner vector represents one frame containing references for 1 or more
  // sensors.
  static absl::StatusOr<std::unique_ptr<FileImageSource>> FromFiles(
      std::vector<std::vector<ImageFileReference>> sensor_files_lists,
      int64_t start_index = 0, bool loop_files = false);

  static absl::StatusOr<std::unique_ptr<ImageSource>> Create(
      const CameraIdentifier& camera_identifier);

  void SetClockForTesting(util::Clock* clock) { clock_ = clock; }

  absl::Status GetFaultsStatus() const final { return absl::OkStatus(); }

  absl::Status ClearFaults() final { return absl::OkStatus(); }

 private:
  FileImageSource(uint64_t frame_num, int64_t start_index, bool loop_files,
                  std::vector<std::vector<ImageFileReference>> filenames,
                  Dimensions dimensions);

  // Note: This class does currently not implement 'timeout' functionality.
  absl::StatusOr<CaptureResult> CaptureImpl(
      absl::Duration timeout,
      const absl::flat_hash_set<int64_t>& sensor_ids) override;

  // Reads and returns the currently configured camera settings.
  // For a FileImageSource, only the Width and Height can be queried, as these
  // are the only parameters that are preserved when saving images to file. The
  // OffsetX and OffsetY can be queried, but will always return 0, as this
  // information is no longer available.
  // For all other values, the function raises an error.
  absl::StatusOr<CameraSetting> ReadCameraSettingImpl(
      absl::string_view name) const override;

  absl::StatusOr<std::vector<SensorInformation>> DescribeCameraSensorsImpl()
      const override;

  uint64_t frame_num_ = 0;  // Total number of frames.
  std::vector<std::vector<ImageFileReference>> filenames_;
  const Dimensions dimensions_;
  int next_file_;
  bool loop_files_;
  util::Clock* clock_;  // Externally owned.
};

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_CAMERAS_FILE_IMAGE_SOURCE_H_
