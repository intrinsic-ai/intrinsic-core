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

#include "intrinsic/perception/core/core_io.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>  // NOLINT
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>  // NOLINT
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/cloud/storage/client.h"
#include "google/cloud/storage/list_objects_reader.h"
#include "google/cloud/storage/object_read_stream.h"
#include "google/cloud/storage/object_write_stream.h"
#include "google/cloud/storage/well_known_parameters.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/io_conversions.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/core/operators.h"
#include "intrinsic/util/cloud.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/imgcodecs.hpp"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"

namespace intrinsic {
namespace perception {

namespace {

namespace gcs = ::google::cloud::storage;

const char kCnsPrefix[] = "/cns/";

// Helper function to write png images to Google file system.
template <typename ImageTrait>
absl::Status WritePngInternal(absl::string_view file_path,
                              const Image<ImageTrait>& image,
                              uint8_t compression_level) {
  std::vector<uint8_t> byte_buffer;
  if (!EncodeImagePng(image, &byte_buffer, compression_level)) {
    return absl::UnknownError("Error while encoding image as png!");
  }

  // Vector of uint8 needs to be converted to char buffer for writing to file.
  return file::SetContents(
      file_path,
      absl::string_view(reinterpret_cast<char*>(byte_buffer.data()),
                        byte_buffer.size()),
      file::Defaults());
}

// Helper function to write jpg images to Google file system.
template <typename ImageTrait>
absl::Status WriteJpegInternal(absl::string_view file_path,
                               const Image<ImageTrait>& image,
                               uint8_t compression_level) {
  std::vector<uint8_t> byte_buffer;
  if (!EncodeImageJpeg(image, &byte_buffer, compression_level)) {
    return absl::UnknownError("Error while encoding image as jpeg!");
  }

  // Vector of uint8 needs to be converted to char buffer for writing to file.
  return file::SetContents(
      file_path,
      absl::string_view(reinterpret_cast<char*>(byte_buffer.data()),
                        byte_buffer.size()),
      file::Defaults());
}

absl::Status WriteOpenCVWrapper(absl::string_view file_path,
                                const cv::Mat& cv_image) {
  if (cv::imwrite(std::string(file_path), cv_image)) {
    return absl::OkStatus();
  } else {
    return absl::UnknownError(
        absl::StrCat("Error writing file at %s", file_path));
  }
}

// Helper function to write images. Jpg and png write supports Google
// filesystem, for all other formats opencv's imwrite is used.
template <typename ImageTrait>
absl::Status WriteImageInternal(absl::string_view file_path,
                                const Image<ImageTrait>& image,
                                std::optional<uint8_t> compression_level) {
  static_assert(std::is_same<ImageTrait, Rgb8u>::value ||
                    std::is_same<ImageTrait, Gray8u>::value,
                "Data type is not supported for saving images.");
  if (absl::EndsWithIgnoreCase(file_path, ".png")) {
    const uint8_t png_compression =
        compression_level ? compression_level.value() : 6;
    return WritePngInternal(file_path, image, png_compression);
  } else if (absl::EndsWithIgnoreCase(file_path, ".jpg") ||
             absl::EndsWithIgnoreCase(file_path, ".jpeg")) {
    const uint8_t jpg_compression =
        compression_level ? compression_level.value() : 95;
    return WriteJpegInternal(file_path, image, jpg_compression);
  } else {
    if (std::is_same<ImageTrait, Rgb8u>::value) {
      Image<Bgr8u> brg_image = ConvertImage<Bgr8u>(image);
      cv::Mat cv_image = UnsafeConstCastCvMat(brg_image);
      return WriteOpenCVWrapper(file_path, cv_image);
    } else {
      cv::Mat cv_image = UnsafeConstCastCvMat(image);
      return WriteOpenCVWrapper(file_path, cv_image);
    }
  }
}

Image<Gray32f> NormalizeGray32f(const Image<Gray32f>& input) {
  float min_gray_value = std::numeric_limits<float>::max();
  float max_gray_value = std::numeric_limits<float>::lowest();
  for (const auto& pixel : input) {
    min_gray_value = std::min(pixel, min_gray_value);
    max_gray_value = std::max(pixel, max_gray_value);
  }
  if (max_gray_value <= min_gray_value) {
    return Image<Gray32f>(input.dimensions());
  }
  return Transform<Gray32f>(
      input, [min_gray_value, max_gray_value](const Gray32f::PixelType& pix) {
        const float grayvalue_in_0_1_range =
            (pix - min_gray_value) / (max_gray_value - min_gray_value);
        return Gray32f::PixelType(grayvalue_in_0_1_range);
      });
}

bool OutsideBounds(const Image<Gray32f>& image) {
  for (const float val : image) {  // NOLINT
    if (val < 0 || val > 1) {
      LOG(WARNING)
          << "Values of Gray32f image must all lie within [0, 1] but value: "
          << val << " encountered. Values are normalized to [0, 1] range.";
      return true;
    }
  }
  return false;
}

std::string FilenameFrom(absl::string_view images_dir,
                         absl::string_view image_prefix,
                         absl::string_view image_suffix) {
  return file::JoinPath(images_dir, absl::StrCat(image_prefix, image_suffix));
}

std::string FilenameFrom(absl::string_view images_dir, int num_frame,
                         absl::string_view image_suffix) {
  return FilenameFrom(images_dir, absl::StrFormat("%06d_", num_frame),
                      image_suffix);
}

template <typename ImageTrait>
absl::StatusOr<std::optional<Image<ImageTrait>>> TryReadImage(
    absl::string_view filename) {
  auto image_or_error = ReadImage<ImageTrait>(filename);
  if (!image_or_error.ok()) {
    if (absl::IsNotFound(image_or_error.status())) {
      return std::nullopt;
    }
    // github ortools's file::Exists() returns InvalidArgumentError if
    // the file does not exist.
    if (absl::IsInvalidArgument(image_or_error.status())) {
      return std::nullopt;
    }
    return image_or_error.status();
  }
  return image_or_error.value();
}

absl::StatusOr<std::string> ReadFileFromGcs(absl::string_view filename) {
  gcs::Client gcs_client;

  INTR_ASSIGN_OR_RETURN(const GcsBlobPathInformation blob_path_info,
                        GetGcsBlobPathInformationFromUri(filename));

  gcs::ObjectReadStream stream = gcs_client.ReadObject(
      blob_path_info.bucket_name, blob_path_info.object_name,
      blob_path_info.GenerationAs<gcs::Generation>());
  if (!stream.status().ok()) {
    return ToAbsl(stream.status());
  }

  std::ostringstream file_content_stream;
  stream >> file_content_stream.rdbuf();
  return file_content_stream.str();
}

absl::StatusOr<std::string> WriteToGcs(absl::string_view content,
                                       absl::string_view filename) {
  gcs::Client gcs_client;

  INTR_ASSIGN_OR_RETURN(const GcsBlobPathInformation blob_path_info,
                        GetGcsBlobPathInformationFromUri(filename));
  gcs::ObjectWriteStream stream = gcs_client.WriteObject(
      blob_path_info.bucket_name, blob_path_info.object_name);
  stream << content;
  stream.Close();
  const auto metadata_or_status = stream.metadata();
  if (!metadata_or_status.ok()) {
    return ToAbsl(metadata_or_status.status());
  }

  return absl::StrCat(
      kGcsUriPrefix,
      file::JoinPath(blob_path_info.bucket_name, blob_path_info.object_name),
      "#", metadata_or_status->generation());
}

absl::Status FileExistsInGcs(absl::string_view filename) {
  gcs::Client gcs_client;

  INTR_ASSIGN_OR_RETURN(const GcsBlobPathInformation blob_path_info,
                        GetGcsBlobPathInformationFromUri(filename));

  return ToAbsl(gcs_client
                    .GetObjectMetadata(
                        blob_path_info.bucket_name, blob_path_info.object_name,
                        blob_path_info.GenerationAs<gcs::Generation>())
                    .status());
}

absl::StatusOr<std::vector<std::string>> FindFilesInGcs(
    absl::string_view dir_path, absl::string_view extension_pattern) {
  if (!extension_pattern.empty() && !absl::StartsWith(extension_pattern, "*")) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "For GCS paths the extension pattern must start with a '*' but "
              "provided was: "
           << extension_pattern;
  }
  // The GCS client doesn't support glob patterns, so we restrict filtering to
  // a simple glob format: *<suffix>
  const absl::string_view extension_suffix =
      extension_pattern.empty() ? "" : extension_pattern.substr(1);
  if (absl::StrContains(extension_suffix, "*")) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "GCS path filtering only supports the glob format '*<suffix>' "
              "but provided was: "
           << extension_pattern;
  }

  gcs::Client gcs_client;

  const absl::StatusOr<GcsBlobPathInformation> blob_path_info =
      GetGcsBlobPathInformationFromUri(dir_path);
  if (!blob_path_info.ok()) {
    return intrinsic::InternalErrorBuilder()
           << "Error happened when getting GCS blob path information: "
           << blob_path_info.status();
  }

  gcs::ListObjectsReader reader = gcs_client.ListObjects(
      blob_path_info->bucket_name, gcs::Prefix(blob_path_info->object_name));

  std::vector<std::string> file_names;
  for (auto&& object_metadata : reader) {
    if (!object_metadata) {
      LOG(WARNING) << object_metadata.status().message();
    } else {
      const std::string relative_filename = object_metadata->name();
      const std::string full_filename = absl::StrCat(
          kGcsUriPrefix, blob_path_info->bucket_name, "/", relative_filename);
      if (extension_suffix.empty()) {
        file_names.push_back(full_filename);
      } else {
        if (absl::EndsWith(relative_filename, extension_suffix)) {
          file_names.push_back(full_filename);
        }
      }
    }
  }
  return file_names;
}

template <typename ImageTrait>
absl::StatusOr<Image<ImageTrait>> ReadImageFromStringCv(
    absl::string_view serialized_image, int flags, int type) {
  // TODO: b/417927632 - Use sandboxed imdecode.
  cv::Mat mat = cv::imdecode(
      {serialized_image.data(), static_cast<int>(serialized_image.size())},
      flags);
  if (mat.empty()) {
    return absl::Status(absl::StatusCode::kUnknown, "cv::decode failed");
  }
  if (mat.type() != type) {
    return absl::Status(absl::StatusCode::kUnknown,
                        "The image is not stored in the correct format.");
  }
  return MoveToImage<ImageTrait>(std::move(mat));
}

template <typename ImageTrait>
absl::StatusOr<Image<ImageTrait>> ReadImageCv(absl::string_view filename,
                                              int flags, int type) {
  if (IsGcsPath(filename) || absl::StartsWith(filename, kCnsPrefix)) {
    INTR_ASSIGN_OR_RETURN(const std::string file_content,
                          ReadFileFromStorage(filename));
    return ReadImageFromString<ImageTrait>(file_content);
  }
  INTR_RETURN_IF_ERROR(file::Exists(filename, file::Defaults()));
  std::string file_content;
  INTR_RETURN_IF_ERROR(
      file::GetContents(filename, &file_content, file::Defaults()));
  INTR_ASSIGN_OR_RETURN(
      Image<ImageTrait> result,
      ReadImageFromStringCv<ImageTrait>(file_content, flags, type));
  return result;
}
}  // namespace

template <>
absl::StatusOr<Image<Bgr8u>> ReadImageFromString(
    absl::string_view serialized_image) {
  return ReadImageFromStringCv<Bgr8u>(serialized_image, cv::IMREAD_COLOR,
                                      CV_8UC3);
}

template <>
absl::StatusOr<Image<Rgb8u>> ReadImageFromString(
    absl::string_view serialized_image) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image,
                        ReadImageFromString<Bgr8u>(serialized_image));
  return ConvertImage<Rgb8u>(bgr_image);
}

template <>
absl::StatusOr<Image<Bgra8u>> ReadImageFromString(
    absl::string_view serialized_image) {
  return ReadImageFromStringCv<Bgra8u>(serialized_image, cv::IMREAD_UNCHANGED,
                                       CV_8UC4);
}

template <>
absl::StatusOr<Image<Gray8u>> ReadImageFromString(
    absl::string_view serialized_image) {
  return ReadImageFromStringCv<Gray8u>(serialized_image, cv::IMREAD_GRAYSCALE,
                                       CV_8UC1);
}

template <>
absl::StatusOr<Image<Bgr8u>> ReadImage(absl::string_view filename) {
  return ReadImageCv<Bgr8u>(filename, cv::IMREAD_COLOR, CV_8UC3);
}

template <>
absl::StatusOr<Image<Rgb8u>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image, ReadImage<Bgr8u>(filename));
  return ConvertImage<Rgb8u>(bgr_image);
}

template <>
absl::StatusOr<Image<Bgra8u>> ReadImage(absl::string_view filename) {
  return ReadImageCv<Bgra8u>(filename, cv::IMREAD_UNCHANGED, CV_8UC4);
}

template <>
absl::StatusOr<Image<Rgba8u>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgra8u> bgra_image, ReadImage<Bgra8u>(filename));
  return ConvertImage<Rgba8u>(bgra_image);
}

template <>
absl::StatusOr<Image<Gray8u>> ReadImage(absl::string_view filename) {
  return ReadImageCv<Gray8u>(filename, cv::IMREAD_GRAYSCALE, CV_8UC1);
}

template <>
absl::StatusOr<Image<Depth32f>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image, ReadImage<Bgr8u>(filename));
  return ConvertImage<Depth32f>(bgr_image);
}

template <>
absl::StatusOr<Image<Gray32f>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image, ReadImage<Bgr8u>(filename));
  return ConvertImage<Gray32f>(bgr_image);
}

template <>
absl::StatusOr<Image<Normal32f>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image, ReadImage<Bgr8u>(filename));
  return ConvertImage<Normal32f>(bgr_image);
}

template <>
absl::StatusOr<Image<Depth16u>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image, ReadImage<Bgr8u>(filename));
  return ConvertImage<Depth16u>(bgr_image);
}

template <>
absl::StatusOr<Image<Bool8u>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgr8u> bgr_image, ReadImage<Bgr8u>(filename));
  return ConvertImage<Bool8u>(bgr_image);
}

template <>
absl::StatusOr<Image<Point32f>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Rgb8u> rgb_image, ReadImage<Rgb8u>(filename));
  if (rgb_image.rows() % 3 != 0) {
    return absl::FailedPreconditionError(
        "Unexpected height of png encoding a point cloud.");
  }
  const int width = rgb_image.cols();
  const int height = rgb_image.rows() / 3;
  const int num_pixels = width * height;
  Image<Point32f> point_cloud(rgb_image.cols(), height);
  for (int index = 0; index < num_pixels; ++index) {
    point_cloud.data(index) =
        PointConvert(rgb_image.data(index), rgb_image.data(num_pixels + index),
                     rgb_image.data(2 * num_pixels + index));
  }
  return point_cloud;
}

template <>
absl::StatusOr<Image<Label32i>> ReadImage(absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(Image<Bgra8u> bgra_image, ReadImage<Bgra8u>(filename));
  return ConvertImage<Label32i>(bgra_image);
}

absl::Status FileExistsInStorage(absl::string_view filename) {
  if (IsGcsPath(filename)) {
    return FileExistsInGcs(filename);
  }
  return file::Exists(filename, file::Defaults());
}

absl::Status RecursivelyCreateDirInStorage(absl::string_view dir_path) {
  if (IsGcsPath(dir_path)) {
    return absl::OkStatus();
  }
  return file::RecursivelyCreateDir(dir_path, file::Defaults());
}

absl::StatusOr<std::string> ReadFileFromStorage(absl::string_view filename) {
  if (IsGcsPath(filename)) {
    return ReadFileFromGcs(filename);
  }
  std::string contents;
  INTR_RETURN_IF_ERROR(
      file::GetContents(filename, &contents, file::Defaults()));
  return contents;
}

absl::StatusOr<std::vector<std::string>> FindLocalFilesRecursive(
    absl::string_view dir_path, absl::string_view extension_pattern,
    bool recursive) {
  std::error_code error;
  std::vector<std::string> files;

  INTR_RETURN_IF_ERROR(file::Match(file::JoinPath(dir_path, extension_pattern),
                                   &files, file::Defaults()));

  if (recursive) {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(dir_path, error)) {
      if (std::filesystem::is_directory(entry.path())) {
        std::vector<std::string> sub_files;
        INTR_RETURN_IF_ERROR(file::Match(
            file::JoinPath(std::string(entry.path()), extension_pattern),
            &sub_files, file::Defaults()));
        files.insert(files.end(), sub_files.begin(), sub_files.end());
      }
    }
    if (error) {
      return absl::InternalError(
          absl::StrCat("Failed to list files in directory: ", dir_path,
                       " with error: ", error.message()));
    }
  }
  return files;
}

absl::StatusOr<std::vector<std::string>> FindFilesInStorage(
    absl::string_view dir_path, absl::string_view extension_pattern,
    bool recursive) {
  if (IsGcsPath(dir_path)) {
    return FindFilesInGcs(dir_path, extension_pattern);
  }
  return FindLocalFilesRecursive(dir_path, extension_pattern, recursive);
}

absl::StatusOr<std::string> WriteFileToStorage(absl::string_view content,
                                               absl::string_view filename) {
  if (IsGcsPath(filename)) {
    return WriteToGcs(content, filename);
  }
  INTR_RETURN_IF_ERROR(file::SetContents(filename, content, file::Defaults()));
  return std::string(filename);
}

bool IsGcsPath(absl::string_view path) {
  return absl::StartsWith(path, kGcsUriPrefix);
}

template <>
absl::Status WriteImage(absl::string_view filename, const Image<Rgb8u>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImageInternal(filename, image, compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename, const Image<Gray8u>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImageInternal(filename, image, compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename, const Image<Bool8u>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImageInternal(
      filename,
      Transform<Gray8u>(
          image, [](const Bool8u::PixelType& p) { return p == 0 ? 0 : 255; }),
      compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename, const Image<Rgba8u>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImageInternal(
      filename,
      Transform<Rgb8u>(image,
                       [](const Rgba8u::PixelType& p) { return p.head<3>(); }),
      compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename,
                        const Image<Label32i>& image,
                        std::optional<uint8_t> compression_level) {
  if (absl::EndsWithIgnoreCase(filename, ".png")) {
    const uint8_t png_compression =
        compression_level ? compression_level.value() : 6;
    return WritePngInternal(filename, image, png_compression);
  }

  return absl::InternalError("Only png is supported.");
}

template <>
absl::Status WriteImage(absl::string_view filename,
                        const Image<Depth16u>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImage(filename, ConvertImage<Rgb8u>(image), compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename,
                        const Image<Depth32f>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImage(filename, ConvertImage<Rgb8u>(image), compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename,
                        const Image<Normal32f>& image,
                        std::optional<uint8_t> compression_level) {
  return WriteImage(filename, ConvertImage<Rgb8u>(image), compression_level);
}

// Image<Point32f> is stored as one png image, which is composed of concatenated
// x, y, and z image.
template <>
absl::Status WriteImage(absl::string_view filename,
                        const Image<Point32f>& image,
                        std::optional<uint8_t> compression_level) {
  const int num_pixels = image.cols() * image.rows();
  Image<Rgb8u> total(image.cols(), 3 * image.rows());

  for (int index = 0; index < num_pixels; ++index) {
    const Point32f::PixelType& pix = image.data(index);
    for (int channel = 0; channel < 3; ++channel) {
      total.data(channel * num_pixels + index) = PointConvert(pix, channel);
    }
  }
  return WriteImage(filename, total, compression_level);
}

template <>
absl::Status WriteImage(absl::string_view filename, const Image<Gray32f>& image,
                        std::optional<uint8_t> compression_level) {
  Image<Gray32f> image_to_write = image;
  if (OutsideBounds(image)) {
    LOG(WARNING) << "Gray32f values are normalized to [0, 1] range.";
    image_to_write = NormalizeGray32f(image);
  }
  return WriteImage(filename, ConvertImage<Rgb8u>(image_to_write),
                    compression_level);
}

absl::Status WriteSensorImage(absl::string_view filename,
                              const SensorImage& sensor_image) {
  // NOTE(mbokeloh): 1 leads to almost identical file sizes but much lower
  // compression times.
  // TODO(mbokeloh): Test effect of compression level 1 to other channels.
  constexpr int kPngCompressionLevel = 1;
  if (sensor_image.gray32f().has_value()) {
    INTR_RETURN_IF_ERROR(
        WriteImage(filename, *sensor_image.gray32f(), kPngCompressionLevel));
  } else if (sensor_image.gray8u().has_value()) {
    INTR_RETURN_IF_ERROR(
        WriteImage(filename, *sensor_image.gray8u(), kPngCompressionLevel));
  } else if (sensor_image.rgb8u().has_value()) {
    INTR_RETURN_IF_ERROR(
        WriteImage(filename, *sensor_image.rgb8u(), kPngCompressionLevel));
  } else if (sensor_image.depth32f().has_value()) {
    INTR_RETURN_IF_ERROR(WriteImage(filename, *sensor_image.depth32f()));
  } else if (sensor_image.point32f().has_value()) {
    INTR_RETURN_IF_ERROR(WriteImage(filename, *sensor_image.point32f()));
  } else if (sensor_image.normal32f().has_value()) {
    INTR_RETURN_IF_ERROR(WriteImage(filename, *sensor_image.normal32f()));
  }
  return absl::OkStatus();
}

std::string SensorIdFilenameSuffix(int64_t sensor_id) {
  return absl::StrFormat("s%d.png", sensor_id);
}

template <typename T>
absl::StatusOr<std::string> GenericWriteSensorImage(
    const SensorImage& sensor_image, absl::string_view images_dir,
    T image_prefix_or_index) {
  const std::string filename =
      FilenameFrom(images_dir, image_prefix_or_index,
                   SensorIdFilenameSuffix(sensor_image.sensor_id()));
  INTR_RETURN_IF_ERROR(WriteSensorImage(filename, sensor_image));
  return filename;
}

absl::StatusOr<absl::flat_hash_map<int64_t, std::string>> WriteCaptureResult(
    const CaptureResult& capture_result, absl::string_view images_dir) {
  const std::string time_string = absl::FormatTime(
      "%Y%m%d-%H%M%S", capture_result.capture_at, absl::LocalTimeZone());
  const std::string image_prefix = absl::StrCat(time_string, "_");

  absl::flat_hash_map<int64_t, std::string> sensor_id_to_filename;
  for (const SensorImage& sensor_image : capture_result.sensor_images) {
    INTR_ASSIGN_OR_RETURN(
        const std::string filename,
        GenericWriteSensorImage(sensor_image, images_dir, image_prefix));
    sensor_id_to_filename[sensor_image.sensor_id()] = filename;
  }
  return sensor_id_to_filename;
}

absl::StatusOr<absl::flat_hash_map<int64_t, std::string>> WriteCaptureResult(
    const CaptureResult& capture_result, absl::string_view images_dir,
    int num_frame) {
  absl::flat_hash_map<int64_t, std::string> sensor_id_to_filename;
  for (const SensorImage& sensor_image : capture_result.sensor_images) {
    INTR_ASSIGN_OR_RETURN(
        const std::string filename,
        GenericWriteSensorImage(sensor_image, images_dir, num_frame));
    sensor_id_to_filename[sensor_image.sensor_id()] = filename;
  }
  return sensor_id_to_filename;
}

absl::StatusOr<absl::flat_hash_map<int64_t, std::string>> WriteCaptureResult(
    const CaptureResult& capture_result, absl::string_view images_dir,
    absl::string_view images_prefix) {
  absl::flat_hash_map<int64_t, std::string> sensor_id_to_filename;
  for (const SensorImage& sensor_image : capture_result.sensor_images) {
    INTR_ASSIGN_OR_RETURN(
        const std::string filename,
        GenericWriteSensorImage(sensor_image, images_dir, images_prefix));
    sensor_id_to_filename[sensor_image.sensor_id()] = filename;
  }
  return sensor_id_to_filename;
}

absl::StatusOr<CaptureResult> ReadCaptureResult(
    absl::string_view images_dir, int num_frame,
    const CameraParams& camera_params, absl::Time capture_at) {
  return absl::UnimplementedError("Not implemented.");
}

absl::StatusOr<CaptureResult> ReadCaptureResult(
    absl::string_view images_dir,
    const std::vector<std::string>& relative_filenames,
    const CameraParams& camera_params, absl::Time capture_at) {
  return absl::UnimplementedError("Not implemented.");
}

absl::Status WritePointCloudAsPly(const Image<Point32f>& point_cloud,
                                  absl::string_view filename) {
  std::vector<Point32f::PixelType> points;
  points.reserve(point_cloud.area());
  for (const Point32f::PixelType& point : point_cloud) {
    if (Point32f::IsValid(point)) {
      points.push_back(point);
    }
  }
  std::stringstream mesh_ply_stream;
  mesh_ply_stream << "ply" << std::endl;
  mesh_ply_stream << "format ascii 1.0" << std::endl;
  mesh_ply_stream << "element vertex " << points.size() << std::endl;
  mesh_ply_stream << "property float x" << std::endl;
  mesh_ply_stream << "property float y" << std::endl;
  mesh_ply_stream << "property float z" << std::endl;
  mesh_ply_stream << "end_header" << std::endl;

  for (const Point32f::PixelType& point : points) {
    mesh_ply_stream << point[0] << " " << point[1] << " " << point[2]
                    << std::endl;
  }

  return file::SetContents(filename, mesh_ply_stream.str(), file::Defaults());
  return absl::OkStatus();
}

}  // namespace perception
}  // namespace intrinsic
