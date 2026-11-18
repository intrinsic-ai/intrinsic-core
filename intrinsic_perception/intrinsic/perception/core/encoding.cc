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

#include "intrinsic/perception/core/encoding.h"

#include <cstdint>
#include <vector>

#include "absl/log/check.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/io_conversions.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/imgproc/imgproc.hpp"

namespace intrinsic {
namespace perception {

template <>
bool EncodeImagePng(const Image<Bgr8u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 0)
      << "Invalid png compression level. Needs to be greater or equal to 0.";
  CHECK_LE(compression_level, 9)
      << "Invalid png compression level. Needs to be lower or equal to 9.";
  const std::vector<int> compression_params{cv::IMWRITE_PNG_COMPRESSION,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".png", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImagePng(const Image<Bgra8u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 0)
      << "Invalid png compression level. Needs to be greater or equal to 0.";
  CHECK_LE(compression_level, 9)
      << "Invalid png compression level. Needs to be lower or equal to 9.";
  const std::vector<int> compression_params{cv::IMWRITE_PNG_COMPRESSION,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".png", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImagePng(const Image<Bool8u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 0)
      << "Invalid png compression level. Needs to be greater or equal to 0.";
  CHECK_LE(compression_level, 9)
      << "Invalid png compression level. Needs to be lower or equal to 9.";
  const std::vector<int> compression_params{cv::IMWRITE_PNG_COMPRESSION,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".png", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImagePng(const Image<Gray8u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 0)
      << "Invalid png compression level. Needs to be greater or equal to 0.";
  CHECK_LE(compression_level, 9)
      << "Invalid png compression level. Needs to be lower or equal to 9.";
  const std::vector<int> compression_params{cv::IMWRITE_PNG_COMPRESSION,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".png", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImagePng(const Image<Gray32f>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Gray8u> image_converted = ConvertImage<Gray8u>(image);
  return EncodeImagePng(image_converted, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Rgb8u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Bgr8u> bgr_image = ConvertImage<Bgr8u>(image);
  return EncodeImagePng(bgr_image, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Rgba8u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Bgra8u> bgra_image = ConvertImage<Bgra8u>(image);
  return EncodeImagePng(bgra_image, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Depth16u>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Rgb8u> image_converted = ConvertImage<Rgb8u>(image);
  return EncodeImagePng(image_converted, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Depth32f>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Rgb8u> image_converted = ConvertImage<Rgb8u>(image);
  return EncodeImagePng(image_converted, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Normal32f>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Rgb8u> image_converted = ConvertImage<Rgb8u>(image);
  return EncodeImagePng(image_converted, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Label32i>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  Image<Rgba8u> image_converted = ConvertImage<Rgba8u>(image);
  return EncodeImagePng(image_converted, buffer, compression_level);
}

template <>
bool EncodeImagePng(const Image<Point32f>& image, std::vector<uint8_t>* buffer,
                    uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  // Image<Point32f> is stored as one png image, which is composed of
  // concatenated x, y, and z image.

  const int num_pixels = image.cols() * image.rows();
  Image<Rgb8u> total(image.cols(), 3 * image.rows());

  for (int channel = 0; channel < 3; ++channel) {
    for (int index = 0; index < num_pixels; ++index) {
      const Point32f::PixelType& pix = image.data(index);
      total.data(channel * num_pixels + index) = PointConvert(pix, channel);
    }
  }

  return EncodeImagePng(total, buffer, compression_level);
}

template <>
Image<Bgr8u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  // TODO: b/417927632 - Use sandboxed imdecode.
  return MoveToImage<Bgr8u>(cv::imdecode(buffer, cv::IMREAD_COLOR));
}

template <>
Image<Bgra8u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  // TODO: b/417927632 - Use sandboxed imdecode.
  return MoveToImage<Bgra8u>(cv::imdecode(buffer, cv::IMREAD_UNCHANGED));
}

template <>
Image<Rgb8u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  // TODO: b/417927632 - Use sandboxed imdecode.
  return MoveToImage<Rgb8u>(cv::imdecode(buffer, cv::IMREAD_COLOR_RGB));
}

template <>
Image<Rgba8u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  return ConvertImage<Rgba8u>(DecodeImagePng<Bgra8u>(buffer));
}

template <>
Image<Gray8u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  // TODO: b/417927632 - Use sandboxed imdecode.
  return MoveToImage<Gray8u>(cv::imdecode(buffer, cv::IMREAD_GRAYSCALE));
}

template <>
Image<Bool8u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  // TODO: b/417927632 - Use sandboxed imdecode.
  return MoveToImage<Bool8u>(cv::imdecode(buffer, cv::IMREAD_GRAYSCALE));
}

template <>
Image<Depth16u> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  return ConvertImage<Depth16u>(DecodeImagePng<Rgb8u>(buffer));
}

template <>
Image<Depth32f> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  return ConvertImage<Depth32f>(DecodeImagePng<Rgb8u>(buffer));
}

template <>
Image<Normal32f> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  return ConvertImage<Normal32f>(DecodeImagePng<Rgb8u>(buffer));
}

template <>
Image<Label32i> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  return ConvertImage<Label32i>(DecodeImagePng<Rgba8u>(buffer));
}

template <>
Image<Point32f> DecodeImagePng(const std::vector<uint8_t>& buffer) {
  const Image<Rgb8u> rgb_image = DecodeImagePng<Rgb8u>(buffer);
  CHECK(rgb_image.rows() % 3 == 0)
      << "Unexpected height of png encoding a point image.";
  const int width = rgb_image.cols();
  const int height = rgb_image.rows() / 3;
  const int num_pixels = width * height;
  Image<Point32f> point_image(rgb_image.cols(), height);
  for (int index = 0; index < num_pixels; ++index) {
    point_image.data(index) =
        PointConvert(rgb_image.data(index), rgb_image.data(num_pixels + index),
                     rgb_image.data(2 * num_pixels + index));
  }
  return point_image;
}

template <>
bool EncodeImageJpeg(const Image<Bgr8u>& image, std::vector<uint8_t>* buffer,
                     uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 10)
      << "Invalid compression quality. Needs to be greater or equal to 10.";
  CHECK_LE(compression_level, 100)
      << "Invalid compression quality. Needs to be lower or equal to 100.";
  const std::vector<int> compression_params{cv::IMWRITE_JPEG_QUALITY,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".jpeg", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImageJpeg(const Image<Bool8u>& image, std::vector<uint8_t>* buffer,
                     uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 10)
      << "Invalid compression quality. Needs to be greater or equal to 10.";
  CHECK_LE(compression_level, 100)
      << "Invalid compression quality. Needs to be lower or equal to 100.";
  const std::vector<int> compression_params{cv::IMWRITE_JPEG_QUALITY,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".jpeg", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImageJpeg(const Image<Gray8u>& image, std::vector<uint8_t>* buffer,
                     uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 10)
      << "Invalid compression quality. Needs to be greater or equal to 10.";
  CHECK_LE(compression_level, 100)
      << "Invalid compression quality. Needs to be lower or equal to 100.";
  const std::vector<int> compression_params{cv::IMWRITE_JPEG_QUALITY,
                                            compression_level};
  const auto cv_image = intrinsic::perception::UnsafeConstCastCvMat(image);
  return cv::imencode(".jpeg", cv_image, *buffer, compression_params);
}

template <>
bool EncodeImageJpeg(const Image<Rgb8u>& image, std::vector<uint8_t>* buffer,
                     uint8_t compression_level) {
  CHECK(buffer != nullptr) << "Invalid buffer.";
  CHECK_GE(compression_level, 10)
      << "Invalid compression quality. Needs to be greater or equal to 10.";
  CHECK_LE(compression_level, 100)
      << "Invalid compression quality. Needs to be lower or equal to 100.";
  Image<Bgr8u> bgr_image = ConvertImage<Bgr8u>(image);
  return EncodeImageJpeg(bgr_image, buffer, compression_level);
}

template <>
Image<Bgr8u> DecodeImageJpeg(const std::vector<uint8_t>& buffer) {
  return MoveToImage<Bgr8u>(cv::imdecode(buffer, cv::IMREAD_COLOR));
}

template <>
Image<Bool8u> DecodeImageJpeg(const std::vector<uint8_t>& buffer) {
  return MoveToImage<Bool8u>(cv::imdecode(buffer, cv::IMREAD_GRAYSCALE));
}

template <>
Image<Gray8u> DecodeImageJpeg(const std::vector<uint8_t>& buffer) {
  return MoveToImage<Gray8u>(cv::imdecode(buffer, cv::IMREAD_GRAYSCALE));
}

template <>
Image<Rgb8u> DecodeImageJpeg(const std::vector<uint8_t>& buffer) {
  return MoveToImage<Rgb8u>(cv::imdecode(buffer, cv::IMREAD_COLOR_RGB));
}

}  // namespace perception
}  // namespace intrinsic
