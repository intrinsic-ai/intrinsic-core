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

#include "intrinsic/perception/proto_conversion/v1/image_buffer.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/perception/core/color_gradient.h"
#include "intrinsic/perception/core/conversion.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/core/operators.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/perception/proto/v1/dimensions.pb.h"
#include "intrinsic/perception/proto/v1/image_buffer.pb.h"
#include "intrinsic/perception/proto_conversion/v1/dimensions.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "libyuv/convert.h"
#include "libyuv/convert_argb.h"
#include "libyuv/convert_from.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

namespace intrinsic_proto::perception::v1 {

namespace {

// For more detailed information on the compression levels, see:
// https://docs.opencv.org/4.7.0/d8/d6a/group__imgcodecs__flags.html#ga292d81be8d76901bff7988d18d2b42ac

// Jpeg compression quality. Can range from 0 to 100 (higher is better).
constexpr int kDefaultJpegCompressions = 80;

// Webp compression quality. Can range from 0 to 101 (higher is better). For
// qualities above 100 lossless compression is used.
constexpr int kDefaultWebpCompressions = 101;

// Png compression level. Can range from 0 to 9. Higher values mean higher
// compression (-> smaller image size but higher compression time).
constexpr int kDefaultPngCompressions = 1;

int GetCompressionValueOrDefault(intrinsic::perception::Encoding encoding,
                                 std::optional<int> compression_effort) {
  if (compression_effort.has_value()) {
    return compression_effort.value();
  }
  switch (encoding) {
    case intrinsic::perception::Encoding::kJpeg:
      return kDefaultJpegCompressions;
    case intrinsic::perception::Encoding::kWebp:
      return kDefaultWebpCompressions;
    case intrinsic::perception::Encoding::kPng:
      return kDefaultPngCompressions;
    case intrinsic::perception::Encoding::kYuv420p:
      CHECK(false) << "Requested a default compression value for YUV420P";
    case intrinsic::perception::Encoding::kUnspecified:
    default:
      CHECK(false) << "Requested a default compression value for an "
                      "unspecified compression method.";
  }
}

bool IsLossy(intrinsic::perception::Encoding encoding,
             std::optional<int> compression_effort) {
  switch (encoding) {
    case intrinsic::perception::Encoding::kJpeg:
    case intrinsic::perception::Encoding::kYuv420p:
      return true;
    case intrinsic::perception::Encoding::kWebp:
      return compression_effort.has_value() &&
             compression_effort.value() < kDefaultWebpCompressions;
    case intrinsic::perception::Encoding::kPng:
    case intrinsic::perception::Encoding::kUnspecified:
    default:
      return false;
  }
}

template <typename PixelTraits>
absl::string_view GetBuffer(
    const intrinsic::perception::Image<PixelTraits>& image) {
  const auto* raw_buffer = reinterpret_cast<char const*>(image.data());
  return {raw_buffer, static_cast<size_t>(image.data_size())};
}

absl::StatusOr<intrinsic::perception::Image<intrinsic::perception::Rgb8u>>
TransformDepth32fToRgb8u(
    const intrinsic::perception::Image<intrinsic::perception::Depth32f>&
        image) {
  // Note: all colors are BGR.
  const intrinsic::perception::ColorGradientFunction<
      intrinsic::perception::Rgb8u>
      depth_to_color = intrinsic::perception::CustomColorGradientFunction<
          intrinsic::perception::Rgb8u>({
          {0.0f, intrinsic::perception::Rgb8u::PixelType(0, 0, 0)},
          {0.6f, intrinsic::perception::Rgb8u::PixelType(0, 0, 255)},
          {0.8f, intrinsic::perception::Rgb8u::PixelType(0, 255, 255)},
          {0.9f, intrinsic::perception::Rgb8u::PixelType(0, 255, 0)},
          {0.95f, intrinsic::perception::Rgb8u::PixelType(255, 255, 0)},
          {1.0f, intrinsic::perception::Rgb8u::PixelType(255, 0, 0)},
      });
  constexpr float kMaxDepth = 1.5f;
  intrinsic::perception::Image<intrinsic::perception::Rgb8u> image_rgb8u =
      intrinsic::perception::Transform<intrinsic::perception::Rgb8u>(
          image, [depth_to_color](
                     const intrinsic::perception::Depth32f::PixelType& depth) {
            return depth_to_color(depth / kMaxDepth);
          });
  return std::move(image_rgb8u);
}

intrinsic::perception::Image<intrinsic::perception::Rgb8u>
TransformNormal32fToRgb8u(
    const intrinsic::perception::Image<intrinsic::perception::Normal32f>&
        image) {
  cv::Mat scaled;
  constexpr double kAlpha = 127.5;
  constexpr double kBeta = 127.5;
  UnsafeConstCastCvMat(image).convertTo(scaled, CV_8UC3, kAlpha, kBeta);
  return intrinsic::perception::MoveToImage<intrinsic::perception::Rgb8u>(
      std::move(scaled));
}

intrinsic::perception::Image<intrinsic::perception::Normal32f>
TransformRgb8uToNormal32f(
    const intrinsic::perception::Image<intrinsic::perception::Rgb8u>& image) {
  cv::Mat scaled;
  constexpr double kAlpha = 1.0 / 127.5;
  constexpr double kBeta = -1.0;
  UnsafeConstCastCvMat(image).convertTo(scaled, CV_32FC3, kAlpha, kBeta);
  return intrinsic::perception::MoveToImage<intrinsic::perception::Normal32f>(
      std::move(scaled));
}

template <typename ImageTrait>
ImageBuffer CreateUnencodedImageBuffer(
    const intrinsic::perception::Image<ImageTrait>& image) {
  ImageBuffer image_buffer;
  image_buffer.set_encoding(ENCODING_UNSPECIFIED);
  image_buffer.set_pixel_type(GetPixelType<ImageTrait>());
  image_buffer.set_num_channels(ImageTrait::kNumChannels);
  image_buffer.set_type(GetDataType<ImageTrait>());
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());
  *image_buffer.mutable_data() = GetBuffer(image);
  return image_buffer;
}

struct EncodingParams {
  std::string extension;
  std::vector<int> parameters;
};

absl::StatusOr<EncodingParams> GetEncodingParams(
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  if (encoding == intrinsic::perception::Encoding::kJpeg) {
    return EncodingParams{.extension = ".jpg",
                          .parameters = {cv::IMWRITE_JPEG_QUALITY,
                                         GetCompressionValueOrDefault(
                                             encoding, compression_effort)}};
  } else if (encoding == intrinsic::perception::Encoding::kWebp) {
    return EncodingParams{.extension = ".webp",
                          .parameters = {cv::IMWRITE_WEBP_QUALITY,
                                         GetCompressionValueOrDefault(
                                             encoding, compression_effort)}};
  } else if (encoding == intrinsic::perception::Encoding::kPng) {
    return EncodingParams{.extension = ".png",
                          .parameters = {cv::IMWRITE_PNG_COMPRESSION,
                                         GetCompressionValueOrDefault(
                                             encoding, compression_effort)}};
  } else if (encoding == intrinsic::perception::Encoding::kYuv420p) {
    return absl::InvalidArgumentError(
        "Encoding params are unsupported for YUV420P encoding.");
  } else {
    return absl::InvalidArgumentError(absl::StrCat(
        "Requested unknown encoding for images. Encoding: ", encoding));
  }
}

absl::StatusOr<ImageBuffer> CreateEncodedImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Rgb8u>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  INTR_ASSIGN_OR_RETURN(const EncodingParams encoding_params,
                        GetEncodingParams(encoding, compression_effort));
  std::vector<unsigned char> image_data;
  cv::Mat image_bgr;
  cv::cvtColor(UnsafeConstCastCvMat(image), image_bgr, cv::COLOR_RGB2BGR);
  INTR_RET_CHECK(cv::imencode(encoding_params.extension, image_bgr, image_data,
                              encoding_params.parameters));
  ImageBuffer image_buffer;
  image_buffer.set_encoding(ToProto(encoding));
  image_buffer.set_pixel_type(PIXEL_INTENSITY);
  image_buffer.set_num_channels(3);
  image_buffer.set_type(TYPE_UINT8);
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());
  // Note: Since opencv uses std::vector we need to copy the buffer here.
  *image_buffer.mutable_data() =
      std::string(image_data.begin(), image_data.end());
  return std::move(image_buffer);
}

absl::StatusOr<ImageBuffer> CreateEncodedImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Gray8u>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  INTR_ASSIGN_OR_RETURN(const EncodingParams encoding_params,
                        GetEncodingParams(encoding, compression_effort));
  std::vector<unsigned char> image_data;
  INTR_RET_CHECK(cv::imencode(encoding_params.extension,
                              UnsafeConstCastCvMat(image), image_data,
                              encoding_params.parameters));
  ImageBuffer image_buffer;
  image_buffer.set_encoding(ToProto(encoding));
  image_buffer.set_pixel_type(PIXEL_INTENSITY);
  image_buffer.set_num_channels(1);
  image_buffer.set_type(TYPE_UINT8);
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());
  // Note: Since opencv uses std::vector we need to copy the buffer here.
  *image_buffer.mutable_data() =
      std::string(image_data.begin(), image_data.end());
  return std::move(image_buffer);
}

absl::StatusOr<ImageBuffer> CreateEncodedImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Gray32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  return CreateEncodedImageBuffer(
      intrinsic::perception::ConvertGray32fToGray8uImage(image), encoding,
      compression_effort);
}

absl::StatusOr<ImageBuffer> CreateEncodedImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Depth32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  INTR_ASSIGN_OR_RETURN(
      const intrinsic::perception::Image<intrinsic::perception::Rgb8u>
          image_rgb8u,
      TransformDepth32fToRgb8u(image));
  INTR_ASSIGN_OR_RETURN(const EncodingParams encoding_params,
                        GetEncodingParams(encoding, compression_effort));
  std::vector<unsigned char> image_data;
  INTR_RET_CHECK(cv::imencode(encoding_params.extension,
                              UnsafeConstCastCvMat(image_rgb8u), image_data,
                              encoding_params.parameters));
  ImageBuffer image_buffer;
  image_buffer.set_encoding(ToProto(encoding));
  image_buffer.set_pixel_type(PIXEL_INTENSITY);
  image_buffer.set_num_channels(3);
  image_buffer.set_type(TYPE_UINT8);
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());
  // Note: Since opencv uses std::vector we need to copy the buffer here.
  *image_buffer.mutable_data() =
      std::string(image_data.begin(), image_data.end());
  return std::move(image_buffer);
}

absl::StatusOr<ImageBuffer> CreateEncodedImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Point32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  // For now we only encode the depth channel of the point32f image.
  return CreateEncodedImageBuffer(
      intrinsic::perception::Transform<intrinsic::perception::Depth32f>(
          image,
          [](const intrinsic::perception::Point32f::PixelType& pix) {
            return intrinsic::perception::Depth32f::PixelType(pix[2]);
          }),
      encoding, compression_effort);
}

absl::StatusOr<ImageBuffer> CreateEncodedImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Normal32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  if (IsLossy(encoding, compression_effort)) {
    LOG_FIRST_N(WARNING, 1)
        << "Lossy encoded buffer creation from a normal image is inaccurate.";
  }
  return CreateEncodedImageBuffer(TransformNormal32fToRgb8u(image), encoding,
                                  compression_effort);
}

absl::StatusOr<ImageBuffer> CreateYuv420pImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Rgb8u>& image) {
  if (image.dimensions().cols % 2 != 0 || image.dimensions().rows % 2 != 0) {
    return absl::InvalidArgumentError("Width and height must be even.");
  }

  ImageBuffer image_buffer;
  image_buffer.set_encoding(Encoding::ENCODING_YUV420P);
  image_buffer.set_pixel_type(PIXEL_INTENSITY);
  image_buffer.set_num_channels(3);
  image_buffer.set_type(TYPE_UINT8);
  image_buffer.set_packing_type(PACKING_TYPE_PLANAR);
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());

  // convert rgb8 to yuv420p
  const size_t width = image.dimensions().cols;
  const size_t height = image.dimensions().rows;
  const size_t rgb_stride = width * 3;
  const size_t y_stride = width;
  const size_t y_size = width * height;
  const size_t uv_stride = width / 2;
  const size_t uv_size = (width / 2) * (height / 2);
  const size_t u_offset = y_size;
  const size_t v_offset = u_offset + uv_size;
  const size_t yuv_size = y_size + uv_size * 2;

  const uint8_t* data_src = reinterpret_cast<const uint8_t*>(image.data());

  std::string* data = image_buffer.mutable_data();
  data->resize(yuv_size);
  uint8_t* data_dst = reinterpret_cast<uint8_t*>(data->data());
  uint8_t* y_dst = data_dst;
  uint8_t* u_dst = data_dst + u_offset;
  uint8_t* v_dst = data_dst + v_offset;

  int r = libyuv::RAWToJ420(data_src, rgb_stride, y_dst, y_stride, u_dst,
                            uv_stride, v_dst, uv_stride, width, height);
  if (r != 0) {
    return absl::InternalError(
        absl::StrFormat("Could not convert to I420: %d", r));
  }

  return std::move(image_buffer);
}

absl::StatusOr<ImageBuffer> CreateYuv420pImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Gray8u>& image) {
  if (image.dimensions().cols % 2 != 0 || image.dimensions().rows % 2 != 0) {
    return absl::InvalidArgumentError("Width and height must be even.");
  }

  ImageBuffer image_buffer;
  image_buffer.set_encoding(Encoding::ENCODING_YUV420P);
  image_buffer.set_pixel_type(PIXEL_INTENSITY);
  image_buffer.set_num_channels(3);
  image_buffer.set_type(TYPE_UINT8);
  image_buffer.set_packing_type(PACKING_TYPE_PLANAR);
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());

  // convert gray8 to yuv420p
  const size_t width = image.dimensions().cols;
  const size_t height = image.dimensions().rows;
  const size_t y_stride = width;
  const size_t y_size = width * height;
  const size_t uv_size = (width / 2) * (height / 2);
  const size_t u_offset = y_size;
  const size_t v_offset = u_offset + uv_size;
  const size_t yuv_size = y_size + uv_size * 2;

  const uint8_t* data_src = reinterpret_cast<const uint8_t*>(image.data());

  std::string* data = image_buffer.mutable_data();
  data->resize(yuv_size);
  uint8_t* data_dst = reinterpret_cast<uint8_t*>(data->data());
  uint8_t* y_dst = data_dst;
  uint8_t* u_dst = data_dst + u_offset;
  uint8_t* v_dst = data_dst + v_offset;

  // note: just copy the gray8 buffer as the intensity channel, leaving u and v
  // initialized to 128 ensures that the converted YUV420 image maintains the
  // same grayscale appearance as the original grayscale image
  int r = libyuv::I400Copy(data_src, y_stride, y_dst, y_stride, width, height);
  if (r != 0) {
    return absl::InternalError(
        absl::StrFormat("Could not convert to I400: %d", r));
  }
  std::fill(u_dst, u_dst + uv_size, 128);
  std::fill(v_dst, v_dst + uv_size, 128);

  return std::move(image_buffer);
}

absl::StatusOr<ImageBuffer> CreateYuv420pImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Gray32f>& image) {
  return CreateYuv420pImageBuffer(ConvertGray32fToGray8uImage(image));
}

absl::StatusOr<ImageBuffer> CreateYuv420pImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Depth32f>&
        image) {
  if (image.dimensions().cols % 2 != 0 || image.dimensions().rows % 2 != 0) {
    return absl::InvalidArgumentError("Width and height must be even.");
  }

  INTR_ASSIGN_OR_RETURN(
      const intrinsic::perception::Image<intrinsic::perception::Rgb8u>
          image_rgb8u,
      TransformDepth32fToRgb8u(image));

  ImageBuffer image_buffer;
  image_buffer.set_encoding(Encoding::ENCODING_YUV420P);
  image_buffer.set_pixel_type(PIXEL_INTENSITY);
  image_buffer.set_num_channels(3);
  image_buffer.set_type(TYPE_UINT8);
  image_buffer.set_packing_type(PACKING_TYPE_PLANAR);
  *image_buffer.mutable_dimensions() = ToProto(image.dimensions());

  // convert rgb8 to yuv420p
  const size_t width = image_rgb8u.dimensions().cols;
  const size_t height = image_rgb8u.dimensions().rows;
  const size_t rgb_stride = width * 3;
  const size_t y_stride = width;
  const size_t y_size = width * height;
  const size_t uv_stride = width / 2;
  const size_t uv_size = (width / 2) * (height / 2);
  const size_t u_offset = y_size;
  const size_t v_offset = u_offset + uv_size;
  const size_t yuv_size = y_size + uv_size * 2;

  const uint8_t* data_src =
      reinterpret_cast<const uint8_t*>(image_rgb8u.data());

  std::string* data = image_buffer.mutable_data();
  data->resize(yuv_size);
  uint8_t* data_dst = reinterpret_cast<uint8_t*>(data->data());
  uint8_t* y_dst = data_dst;
  uint8_t* u_dst = data_dst + u_offset;
  uint8_t* v_dst = data_dst + v_offset;

  int r = libyuv::RGB24ToJ420(data_src, rgb_stride, y_dst, y_stride, u_dst,
                              uv_stride, v_dst, uv_stride, width, height);
  if (r != 0) {
    return absl::InternalError(
        absl::StrFormat("Could not convert to I420: %d", r));
  }

  return std::move(image_buffer);
}

absl::StatusOr<ImageBuffer> CreateYuv420pImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Point32f>&
        image) {
  // For now we only encode the depth channel of the point32f image.
  return CreateYuv420pImageBuffer(
      intrinsic::perception::Transform<intrinsic::perception::Depth32f>(
          image, [](const intrinsic::perception::Point32f::PixelType& pix) {
            return intrinsic::perception::Depth32f::PixelType(pix[2]);
          }));
}

absl::StatusOr<ImageBuffer> CreateYuv420pImageBuffer(
    const intrinsic::perception::Image<intrinsic::perception::Normal32f>&
        image) {
  LOG_FIRST_N(WARNING, 1)
      << "YUV420p buffer creation from a normal image is inaccurate.";
  return CreateYuv420pImageBuffer(TransformNormal32fToRgb8u(image));
}

template <typename ImageTrait>
absl::StatusOr<intrinsic::perception::Image<ImageTrait>> CreateImageFromYuv420p(
    const ImageBuffer& image_buffer) {
  return absl::UnimplementedError("Not implemented.");
}

template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray32f>>
CreateImageFromYuv420p(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Point32f>>
CreateImageFromYuv420p(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Depth32f>>
CreateImageFromYuv420p(const ImageBuffer& image_buffer);

template <>
absl::StatusOr<intrinsic::perception::Image<intrinsic::perception::Rgb8u>>
CreateImageFromYuv420p(const ImageBuffer& image_buffer) {
  const size_t width = image_buffer.dimensions().cols();
  const size_t height = image_buffer.dimensions().rows();

  if (width % 2 != 0 || height % 2 != 0) {
    return absl::InvalidArgumentError("Width and height must be even.");
  }

  const size_t y_stride = width;
  const size_t y_size = width * height;
  const size_t uv_stride = width / 2;
  const size_t uv_size = (width / 2) * (height / 2);
  const size_t u_offset = y_size;
  const size_t v_offset = u_offset + uv_size;
  const size_t rgb_stride = width * 3;
  const size_t rgb_size = width * height * 3;

  const uint8_t* data_src =
      reinterpret_cast<const uint8_t*>(image_buffer.data().data());
  const uint8_t* y_src = data_src;
  const uint8_t* u_src = data_src + u_offset;
  const uint8_t* v_src = data_src + v_offset;

  std::string data;
  data.resize(rgb_size);
  uint8_t* data_dst = reinterpret_cast<uint8_t*>(data.data());

  int r = libyuv::J420ToRAW(y_src, y_stride, u_src, uv_stride, v_src, uv_stride,
                            data_dst, rgb_stride, width, height);
  if (r != 0) {
    return absl::InternalError(
        absl::StrFormat("Could not convert from I420: %d", r));
  }

  return CreateImageFromMemory<intrinsic::perception::Rgb8u>(
      FromProto(image_buffer.dimensions()), std::move(data));
}

template <>
absl::StatusOr<intrinsic::perception::Image<intrinsic::perception::Gray8u>>
CreateImageFromYuv420p(const ImageBuffer& image_buffer) {
  const size_t width = image_buffer.dimensions().cols();
  const size_t height = image_buffer.dimensions().rows();
  const size_t y_stride = width;
  const size_t gray_stride = width;
  const size_t gray_size = width * height;

  const uint8_t* data_src =
      reinterpret_cast<const uint8_t*>(image_buffer.data().data());

  std::string data;
  data.resize(gray_size);
  uint8_t* data_dst = reinterpret_cast<uint8_t*>(data.data());

  int r = libyuv::I400Copy(data_src, y_stride, data_dst, gray_stride, width,
                           height);
  if (r != 0) {
    return absl::InternalError(
        absl::StrFormat("Could not convert from I400: %d", r));
  }

  return CreateImageFromMemory<intrinsic::perception::Gray8u>(
      FromProto(image_buffer.dimensions()), std::move(data));
}

template <>
absl::StatusOr<intrinsic::perception::Image<intrinsic::perception::Normal32f>>
CreateImageFromYuv420p(const ImageBuffer& image_buffer) {
  LOG_FIRST_N(WARNING, 1)
      << "Normal image creation from a YUV420p buffer is inaccurate.";
  INTR_ASSIGN_OR_RETURN(
      const intrinsic::perception::Image<intrinsic::perception::Rgb8u> rgb8u,
      CreateImageFromYuv420p<intrinsic::perception::Rgb8u>(image_buffer));
  return TransformRgb8uToNormal32f(rgb8u);
}

template <typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

}  // namespace

intrinsic::perception::Encoding FromProto(Encoding encoding) {
  switch (encoding) {
    case Encoding::ENCODING_JPEG:
      return intrinsic::perception::Encoding::kJpeg;
    case Encoding::ENCODING_PNG:
      return intrinsic::perception::Encoding::kPng;
    case Encoding::ENCODING_WEBP:
      return intrinsic::perception::Encoding::kWebp;
    case Encoding::ENCODING_YUV420P:
      return intrinsic::perception::Encoding::kYuv420p;
    case Encoding::ENCODING_UNSPECIFIED:
    default:
      return intrinsic::perception::Encoding::kUnspecified;
  }
}

Encoding ToProto(intrinsic::perception::Encoding encoding) {
  switch (encoding) {
    case intrinsic::perception::Encoding::kJpeg:
      return Encoding::ENCODING_JPEG;
    case intrinsic::perception::Encoding::kPng:
      return Encoding::ENCODING_PNG;
    case intrinsic::perception::Encoding::kWebp:
      return Encoding::ENCODING_WEBP;
    case intrinsic::perception::Encoding::kYuv420p:
      return Encoding::ENCODING_YUV420P;
    case intrinsic::perception::Encoding::kUnspecified:
    default:
      return Encoding::ENCODING_UNSPECIFIED;
  }
}

template <typename ImageTrait>
DataType GetDataType() {
  if constexpr (IsAnyOf<ImageTrait, intrinsic::perception::Rgb8u,
                        intrinsic::perception::Gray8u>) {
    return TYPE_UINT8;
  } else if constexpr (IsAnyOf<ImageTrait, intrinsic::perception::Gray32f,
                               intrinsic::perception::Depth32f,
                               intrinsic::perception::Normal32f,
                               intrinsic::perception::Point32f>) {
    return TYPE_FLOAT32;
  } else {
    static_assert(false, "Unsupported image trait.");
  }
  return TYPE_UNSPECIFIED;
}
template DataType GetDataType<intrinsic::perception::Rgb8u>();
template DataType GetDataType<intrinsic::perception::Gray8u>();
template DataType GetDataType<intrinsic::perception::Gray32f>();
template DataType GetDataType<intrinsic::perception::Depth32f>();
template DataType GetDataType<intrinsic::perception::Normal32f>();
template DataType GetDataType<intrinsic::perception::Point32f>();

template <typename ImageTrait>
PixelType GetPixelType() {
  if constexpr (IsAnyOf<ImageTrait, intrinsic::perception::Rgb8u,
                        intrinsic::perception::Gray8u,
                        intrinsic::perception::Gray32f>) {
    return PIXEL_INTENSITY;
  } else if constexpr (std::same_as<ImageTrait,
                                    intrinsic::perception::Depth32f>) {
    return PIXEL_DEPTH;
  } else if constexpr (std::same_as<ImageTrait,
                                    intrinsic::perception::Normal32f>) {
    return PIXEL_NORMAL;
  } else if constexpr (std::same_as<ImageTrait,
                                    intrinsic::perception::Point32f>) {
    return PIXEL_POINT;
  } else {
    static_assert(false, "Unsupported image trait.");
  }
  return PIXEL_UNSPECIFIED;
}
template PixelType GetPixelType<intrinsic::perception::Rgb8u>();
template PixelType GetPixelType<intrinsic::perception::Gray8u>();
template PixelType GetPixelType<intrinsic::perception::Gray32f>();
template PixelType GetPixelType<intrinsic::perception::Depth32f>();
template PixelType GetPixelType<intrinsic::perception::Normal32f>();
template PixelType GetPixelType<intrinsic::perception::Point32f>();

template <typename ImageTrait>
absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<ImageTrait>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort) {
  if (image.empty()) return ImageBuffer{};
  if (encoding == intrinsic::perception::Encoding::kUnspecified) {
    return CreateUnencodedImageBuffer(image);
  }
  if constexpr (IsAnyOf<ImageTrait, intrinsic::perception::Rgb8u,
                        intrinsic::perception::Gray8u,
                        intrinsic::perception::Gray32f,
                        intrinsic::perception::Depth32f,
                        intrinsic::perception::Normal32f,
                        intrinsic::perception::Point32f>) {
    if (encoding == intrinsic::perception::Encoding::kYuv420p) {
      return CreateYuv420pImageBuffer(image);
    }
    return CreateEncodedImageBuffer(image, encoding, compression_effort);
  }
  return absl::InvalidArgumentError(
      absl::StrCat("encoding type not implemented: ", encoding));
}
template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Rgb8u>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Gray8u>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Gray32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Depth32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Normal32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Point32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);

namespace {

template <typename ImageTrait, typename ImageBufferRef>
absl::StatusOr<intrinsic::perception::Image<ImageTrait>>
CreateImageFromUnencodedBuffer(ImageBufferRef&& image_buffer) {
  if (image_buffer.type() != GetDataType<ImageTrait>()) {
    return absl::InvalidArgumentError("Incompatible image buffer data type");
  }
  if (image_buffer.num_channels() != ImageTrait::kNumChannels) {
    return absl::InvalidArgumentError("Incompatible image buffer num channels");
  }
  const intrinsic::perception::Dimensions dimensions =
      FromProto(image_buffer.dimensions());
  if constexpr (std::is_rvalue_reference_v<ImageBufferRef&&>) {
    return CreateImageFromMemory<ImageTrait>(
        dimensions, std::move(*image_buffer.mutable_data()));
  } else {
    std::string data = image_buffer.data();
    return CreateImageFromMemory<ImageTrait>(dimensions, std::move(data));
  }
}

}  // namespace

template <typename ImageTrait>
absl::StatusOr<intrinsic::perception::Image<ImageTrait>> FromProto(
    const ImageBuffer& image_buffer) {
  if (image_buffer.data().empty()) {
    return absl::InvalidArgumentError("data field in image_buffer is empty");
  }
  if (image_buffer.encoding() == ENCODING_UNSPECIFIED) {
    return CreateImageFromUnencodedBuffer<ImageTrait>(image_buffer);
  } else if (image_buffer.encoding() == ENCODING_YUV420P) {
    return CreateImageFromYuv420p<ImageTrait>(image_buffer);
  }
  // WebP only supports 3 channels, so we need to convert it to gray on the fly.
  // TODO: b/417927632 - Use sandboxed imdecode.
  cv::Mat image = cv::imdecode(
      {reinterpret_cast<const unsigned char*>(image_buffer.data().data()),
       static_cast<int>(image_buffer.data().size())},
      image_buffer.encoding() == ENCODING_WEBP && ImageTrait::kNumChannels == 1
          ? cv::IMREAD_GRAYSCALE
          : cv::IMREAD_UNCHANGED);
  if (image.empty()) {
    return absl::InvalidArgumentError("Couldn't decode image from buffer");
  }
  if (image.channels() == 3) {
    cv::Mat image_rgb;
    cv::cvtColor(image, image_rgb, cv::COLOR_BGR2RGB);

    if constexpr (std::same_as<ImageTrait, intrinsic::perception::Normal32f>) {
      if (image_buffer.encoding() != ENCODING_WEBP &&
          image_buffer.encoding() != ENCODING_PNG) {
        LOG_FIRST_N(WARNING, 1) << "Normal image creation from a lossy encoded "
                                   "buffer may be inaccurate.";
      }
      return TransformRgb8uToNormal32f(
          intrinsic::perception::MoveToImage<intrinsic::perception::Rgb8u>(
              std::move(image_rgb)));
    }

    return intrinsic::perception::MoveToImage<ImageTrait>(std::move(image_rgb));
  }
  return intrinsic::perception::MoveToImage<ImageTrait>(std::move(image));
}
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Rgb8u>>
FromProto(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray8u>>
FromProto(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray32f>>
FromProto(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Depth32f>>
FromProto(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Normal32f>>
FromProto(const ImageBuffer& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Point32f>>
FromProto(const ImageBuffer& image_buffer);

template <typename ImageTrait>
absl::StatusOr<intrinsic::perception::Image<ImageTrait>> FromProto(
    ImageBuffer&& image_buffer) {
  if (!image_buffer.data().empty() &&
      image_buffer.encoding() == ENCODING_UNSPECIFIED) {
    return CreateImageFromUnencodedBuffer<ImageTrait>(std::move(image_buffer));
  }
  return FromProto<ImageTrait>(std::as_const(image_buffer));
}
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Rgb8u>>
FromProto(ImageBuffer&& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray8u>>
FromProto(ImageBuffer&& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray32f>>
FromProto(ImageBuffer&& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Depth32f>>
FromProto(ImageBuffer&& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Normal32f>>
FromProto(ImageBuffer&& image_buffer);
template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Point32f>>
FromProto(ImageBuffer&& image_buffer);

}  // namespace intrinsic_proto::perception::v1
