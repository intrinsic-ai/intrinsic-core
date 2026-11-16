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

#include "intrinsic/perception/cameras/file_image_source.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/overload.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_utils.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/perception/cameras/image_source.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/core_io.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image_file_reference.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "magic_enum/magic_enum.hpp"
#include "ortools/base/path.h"
#include "third_party/imported/cpp_libraries/clock/clock.h"
#include "third_party/strings_numbers/sort.h"

namespace intrinsic {
namespace perception {

namespace {

constexpr uint64_t kUndefinedFrameCount = std::numeric_limits<uint64_t>::max();

absl::StatusOr<uint64_t> GetFrameCount(
    const std::vector<uint64_t>& channel_counts) {
  uint64_t size = kUndefinedFrameCount;
  for (const int channel_count : channel_counts) {
    if (channel_count == 0) {
      continue;  // Ignore an empty channel.
    }
    if (size == kUndefinedFrameCount) {
      size = channel_count;
    } else if (channel_count != size) {
      return absl::FailedPreconditionError(
          "Inconsistent number of images in different channels.");
    }
  }
  if (size == kUndefinedFrameCount) {
    size = 0;  // No frames.
  }
  return size;
}

template <class ImageTraits, class... MetaData>
absl::StatusOr<SensorImage> ReadSensorImage(std::string_view filename,
                                            MetaData&&... metadata) {
  INTR_ASSIGN_OR_RETURN(Image<ImageTraits> image,
                        ReadImage<ImageTraits>(filename));
  return SensorImage(std::forward<MetaData>(metadata)..., std::move(image));
}

template <class... MetaData>
absl::StatusOr<SensorImage> ReadSensorImage(const ImageFileReference& file_ref,
                                            MetaData&&... metadata) {
  switch (file_ref.pixel_type) {
    case PixelType::kUnspecified:
    case PixelType::kIntensity:
      if (file_ref.num_channels == 1) {
        return ReadSensorImage<Gray8u>(file_ref.path,
                                       std::forward<MetaData>(metadata)...);
      } else if (file_ref.num_channels == 3 ||
                 file_ref.num_channels == /*unspecified*/ 0) {
        return ReadSensorImage<Rgb8u>(file_ref.path,
                                      std::forward<MetaData>(metadata)...);
      }
      INTR_RET_CHECK_FAIL()
          << "Unsupported number of channels: " << file_ref.num_channels;
    case PixelType::kDepth:
      return ReadSensorImage<Depth32f>(file_ref.path,
                                       std::forward<MetaData>(metadata)...);
    case PixelType::kPoint:
      return ReadSensorImage<Point32f>(file_ref.path,
                                       std::forward<MetaData>(metadata)...);
    case PixelType::kNormal:
      return ReadSensorImage<Normal32f>(file_ref.path,
                                        std::forward<MetaData>(metadata)...);
    default:
      INTR_RET_CHECK_FAIL() << "Unsupported pixel type: "
                            << magic_enum::enum_name(file_ref.pixel_type);
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<FileImageSource>> FileImageSource::FromFiles(
    std::vector<std::vector<ImageFileReference>> sensor_files_lists,
    int64_t start_index, bool loop_files) {
  if (sensor_files_lists.empty()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "No image files provided.";
  }
  std::optional<size_t> num_sensors;
  for (const std::vector<ImageFileReference>& multi_sensor_files :
       sensor_files_lists) {
    if (num_sensors.has_value()) {
      if (*num_sensors != multi_sensor_files.size()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "Number of sensors changed. Expected " << *num_sensors
               << ", got " << multi_sensor_files.size() << ".";
      }
    } else {
      num_sensors = multi_sensor_files.size();
    }
    for (const ImageFileReference& sensor_file : multi_sensor_files) {
      if (std::find(kAllowedImageExtensions.begin(),
                    kAllowedImageExtensions.end(),
                    file::Extension(sensor_file.path)) ==
          kAllowedImageExtensions.end()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "File is not supported " << sensor_file.path;
      }
    }
  }
  INTR_RET_CHECK(num_sensors.has_value());
  if (sensor_files_lists[0].empty()) {
    return absl::InvalidArgumentError("Frame has no sensor files.");
  }
  const ImageFileReference& first_file_ref = sensor_files_lists[0][0];
  INTR_RET_CHECK(first_file_ref.sensor_id.has_value());
  INTR_ASSIGN_OR_RETURN(
      SensorImage first_sensor_image,
      ReadSensorImage(first_file_ref, first_file_ref.sensor_id.value(),
                      absl::Now(), std::nullopt, std::nullopt));

  uint64_t frame_num = sensor_files_lists.size();
  Dimensions dimensions = first_sensor_image.Dimensions();
  return absl::WrapUnique(
      new FileImageSource(frame_num, start_index, loop_files,
                          std::move(sensor_files_lists), dimensions));
}

absl::StatusOr<std::unique_ptr<FileImageSource>> FileImageSource::FromDirectory(
    std::string_view directory,
    const std::vector<ImageFileReference>& sensor_references,
    int64_t start_index, bool remove_incomplete_frames, bool loop_files) {
  if (sensor_references.empty()) {
    std::vector<std::string> filenames;
    for (std::string_view extension : kAllowedImageExtensions) {
      INTR_ASSIGN_OR_RETURN(
          const std::vector<std::string> filenames_ext,
          FindFilesInStorage(directory, absl::StrCat("*", extension)));
      filenames.insert(filenames.end(), filenames_ext.begin(),
                       filenames_ext.end());
    }
    if (filenames.empty()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "No supported images found in " << directory;
    }
    std::sort(filenames.begin(), filenames.end(), autodigit_less());
    std::vector<std::vector<ImageFileReference>> frame_matrix;
    frame_matrix.reserve(filenames.size());
    for (const std::string& file : filenames) {
      if (std::find(kAllowedImageExtensions.begin(),
                    kAllowedImageExtensions.end(),
                    file::Extension(file)) == kAllowedImageExtensions.end()) {
        LOG(WARNING) << absl::StrCat("File is not supported, skipping : ",
                                     file);
        continue;
      }
      frame_matrix.push_back({ImageFileReference{
          .path = file,
          .pixel_type = PixelType::kIntensity,
          .num_channels = 3,
          .sensor_id = kRgb8uSensorId,
      }});
    }
    return FromFiles(std::move(frame_matrix), start_index, loop_files);
  }

  std::vector<std::pair<std::vector<std::string>, const ImageFileReference*>>
      filenames_with_suffix;
  filenames_with_suffix.reserve(sensor_references.size());

  for (const ImageFileReference& sensor_ref : sensor_references) {
    if (sensor_ref.path.empty()) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        std::vector<std::string> filenames,
        FindFilesInStorage(directory, absl::StrCat("*", sensor_ref.path)));
    std::sort(filenames.begin(), filenames.end(), autodigit_less());
    filenames_with_suffix.push_back(
        std::make_pair(std::move(filenames), &sensor_ref));
  }

  if (filenames_with_suffix.empty()) {
    return FromDirectory(directory, {}, start_index, remove_incomplete_frames,
                         loop_files);
  }

  if (remove_incomplete_frames) {
    absl::flat_hash_map<std::string, int> per_frame_channels;
    for (const auto& [filenames, sensor_ref] : filenames_with_suffix) {
      for (const std::string& filename : filenames) {
        per_frame_channels[absl::StripSuffix(filename, sensor_ref->path)]++;
      }
    }

    // Remove all filenames that don't have a full frame.
    for (auto& [filenames, sensor_ref] : filenames_with_suffix) {
      std::erase_if(filenames, [&](std::string_view filename) {
        return per_frame_channels[absl::StripSuffix(
                   filename, sensor_ref->path)] != filenames_with_suffix.size();
      });
    }
  }

  std::vector<uint64_t> counts;
  counts.reserve(filenames_with_suffix.size());
  for (const auto& [filenames, sensor_ref] : filenames_with_suffix) {
    counts.push_back(filenames.size());
  }
  INTR_ASSIGN_OR_RETURN(uint64_t frame_num, GetFrameCount(counts));

  if (frame_num == 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("No images found in '%s'", directory));
  }

  std::vector<std::vector<ImageFileReference>> frame_matrix;
  frame_matrix.reserve(frame_num);

  for (size_t i = 0; i < frame_num; ++i) {
    std::vector<ImageFileReference> sensor_refs;
    sensor_refs.reserve(filenames_with_suffix.size());
    for (const auto& [filenames, sensor_ref_ptr] : filenames_with_suffix) {
      ImageFileReference ref = *sensor_ref_ptr;
      ref.path = filenames[i];
      sensor_refs.push_back(std::move(ref));
    }
    frame_matrix.push_back(std::move(sensor_refs));
  }

  return FromFiles(std::move(frame_matrix), start_index, loop_files);
}

absl::StatusOr<std::unique_ptr<ImageSource>> FileImageSource::Create(
    const CameraIdentifier& camera_identifier) {
  if (const CameraIdentifier::FileCamera* file_camera =
          std::get_if<CameraIdentifier::FileCamera>(
              &camera_identifier.driver)) {
    std::unique_ptr<ImageSource> image_source;
    INTR_RETURN_IF_ERROR(std::visit(
        absl::Overload{
            [&](const CameraIdentifier::FileCamera::Directory& source) {
              INTR_ASSIGN_OR_RETURN(
                  image_source,
                  FileImageSource::FromDirectory(
                      source.path, source.sensor_references,
                      file_camera->start_index ? *file_camera->start_index : 0,
                      source.remove_incomplete_frames,
                      file_camera->loop_files));
              return absl::OkStatus();
            },
            [&](const CameraIdentifier::FileCamera::Files& source) {
              INTR_ASSIGN_OR_RETURN(
                  image_source,
                  FileImageSource::FromFiles(
                      source.sensor_files_lists,
                      file_camera->start_index ? *file_camera->start_index : 0,
                      file_camera->loop_files));
              return absl::OkStatus();
            },
            [&](const std::monostate&) {
              return absl::InternalError("Source for file camera not set.");
            },
        },
        file_camera->source));
    return image_source;
  }

  return absl::InvalidArgumentError(
      "File camera factory invoked without a configured file camera.");
}

FileImageSource::FileImageSource(
    uint64_t frame_num, int64_t start_index, bool loop_files,
    std::vector<std::vector<ImageFileReference>> filenames,
    Dimensions dimensions)
    : frame_num_(frame_num),
      filenames_(std::move(filenames)),
      dimensions_(dimensions),
      next_file_(start_index < 0 ? frame_num_ + start_index : start_index),
      loop_files_(loop_files),
      clock_(util::Clock::RealClock()) {}

absl::StatusOr<CaptureResult> FileImageSource::CaptureImpl(
    absl::Duration timeout, const absl::flat_hash_set<int64_t>& sensor_ids) {
  if (next_file_ < 0) {
    return absl::InternalError("next_file_ invalid negative index!");
  }
  if (next_file_ >= frame_num_) {
    if (loop_files_ && frame_num_ > 0) {
      next_file_ = 0;
    } else {
      return absl::ResourceExhaustedError("No image left!");
    }
  }

  const absl::Time capture_at = clock_->TimeNow();
  std::vector<SensorImage> sensor_images;
  sensor_images.reserve(filenames_[next_file_].size());

  for (const ImageFileReference& file_ref : filenames_[next_file_]) {
    INTR_RET_CHECK(file_ref.sensor_id.has_value());
    INTR_ASSIGN_OR_RETURN(
        SensorImage sensor_image,
        ReadSensorImage(file_ref, file_ref.sensor_id.value(), capture_at,
                        std::nullopt, std::nullopt));
    sensor_images.push_back(std::move(sensor_image));
  }

  CaptureResult capture_result{
      .capture_at = capture_at,
      .sensor_images = std::move(sensor_images),
  };

  ++next_file_;
  return std::move(capture_result);
}

absl::StatusOr<CameraSetting> FileImageSource::ReadCameraSettingImpl(
    std::string_view name) const {
  if (name == genicam::kWidth) {
    return CreateCameraSetting(name, dimensions_.cols);
  } else if (name == genicam::kHeight) {
    return CreateCameraSetting(name, dimensions_.rows);
  } else if (name == genicam::kOffsetX) {
    return CreateCameraSetting(name, 0);
  } else if (name == genicam::kOffsetY) {
    return CreateCameraSetting(name, 0);
  } else {
    return absl::InvalidArgumentError(
        "For file cameras, ReadCameraSetting only supports reading Width and "
        "Height, and OffsetX/OffsetY.");
  }
}

absl::StatusOr<std::vector<SensorInformation>>
FileImageSource::DescribeCameraSensorsImpl() const {
  std::vector<SensorInformation> sensor_infos;
  if (filenames_.empty()) {
    return sensor_infos;
  }
  const std::vector<ImageFileReference>& first_frame = filenames_[0];
  sensor_infos.reserve(first_frame.size());

  for (const ImageFileReference& file_ref : first_frame) {
    int64_t sensor_id = file_ref.sensor_id.value_or(kFallbackSensorId);
    std::string sensor_name;
    PixelType pixel_type = file_ref.pixel_type;
    switch (file_ref.pixel_type) {
      case PixelType::kUnspecified:
      case PixelType::kIntensity:
        if (file_ref.num_channels == 1) {
          sensor_name = "gray8u";
          pixel_type = PixelType::kIntensity;
        } else {
          sensor_name = "rgb8u";
          pixel_type = PixelType::kIntensity;
        }
        break;
      case PixelType::kDepth:
        sensor_name = "depth32f";
        pixel_type = PixelType::kDepth;
        break;
      case PixelType::kPoint:
        sensor_name = "point32f";
        pixel_type = PixelType::kPoint;
        break;
      case PixelType::kNormal:
        sensor_name = "normal32f";
        pixel_type = PixelType::kNormal;
        break;
      default:
        sensor_name = absl::StrCat("sensor_", sensor_id);
        break;
    }

    sensor_infos.push_back({sensor_id,
                            std::move(sensor_name),
                            std::nullopt,
                            std::nullopt,
                            {pixel_type},
                            dimensions_,
                            false});
  }
  return sensor_infos;
}

REGISTER_IMAGE_SOURCE(FileImageSource, "file_camera", FileImageSource::Create);

}  // namespace perception
}  // namespace intrinsic
