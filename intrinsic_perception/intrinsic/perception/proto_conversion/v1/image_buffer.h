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

#ifndef INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_IMAGE_BUFFER_H_
#define INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_IMAGE_BUFFER_H_

#include <optional>

#include "absl/status/statusor.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/perception/proto/v1/image_buffer.pb.h"
#include "intrinsic/perception/proto_conversion/v1/pixel_type.h"

namespace intrinsic_proto::perception::v1 {

intrinsic::perception::Encoding FromProto(Encoding encoding);

Encoding ToProto(intrinsic::perception::Encoding encoding);

template <typename ImageTrait>
DataType GetDataType();
extern template DataType GetDataType<intrinsic::perception::Rgb8u>();
extern template DataType GetDataType<intrinsic::perception::Gray8u>();
extern template DataType GetDataType<intrinsic::perception::Gray32f>();
extern template DataType GetDataType<intrinsic::perception::Depth32f>();
extern template DataType GetDataType<intrinsic::perception::Normal32f>();
extern template DataType GetDataType<intrinsic::perception::Point32f>();

template <typename ImageTrait>
PixelType GetPixelType();
extern template PixelType GetPixelType<intrinsic::perception::Rgb8u>();
extern template PixelType GetPixelType<intrinsic::perception::Gray8u>();
extern template PixelType GetPixelType<intrinsic::perception::Gray32f>();
extern template PixelType GetPixelType<intrinsic::perception::Depth32f>();
extern template PixelType GetPixelType<intrinsic::perception::Normal32f>();
extern template PixelType GetPixelType<intrinsic::perception::Point32f>();

template <typename ImageTrait>
absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<ImageTrait>& image,
    intrinsic::perception::Encoding encoding =
        intrinsic::perception::Encoding::kUnspecified,
    std::optional<int> compression_effort = std::nullopt);
extern template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Rgb8u>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
extern template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Gray8u>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
extern template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Gray32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
extern template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Depth32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
extern template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Normal32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);
extern template absl::StatusOr<ImageBuffer> ToProto(
    const intrinsic::perception::Image<intrinsic::perception::Point32f>& image,
    intrinsic::perception::Encoding encoding,
    std::optional<int> compression_effort);

template <typename ImageTrait>
absl::StatusOr<intrinsic::perception::Image<ImageTrait>> FromProto(
    const ImageBuffer& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Rgb8u>>
FromProto(const ImageBuffer& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray8u>>
FromProto(const ImageBuffer& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray32f>>
FromProto(const ImageBuffer& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Depth32f>>
FromProto(const ImageBuffer& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Normal32f>>
FromProto(const ImageBuffer& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Point32f>>
FromProto(const ImageBuffer& image_buffer);

template <typename ImageTrait>
absl::StatusOr<intrinsic::perception::Image<ImageTrait>> FromProto(
    ImageBuffer&& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Rgb8u>>
FromProto(ImageBuffer&& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray8u>>
FromProto(ImageBuffer&& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Gray32f>>
FromProto(ImageBuffer&& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Depth32f>>
FromProto(ImageBuffer&& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Normal32f>>
FromProto(ImageBuffer&& image_buffer);
extern template absl::StatusOr<
    intrinsic::perception::Image<intrinsic::perception::Point32f>>
FromProto(ImageBuffer&& image_buffer);

}  // namespace intrinsic_proto::perception::v1

// Convenience ToProto aliases to allow argument-dependent lookup. Directly use
// the versioned namespace alternatives above instead when dealing with
// different versions at the same time.
namespace intrinsic::perception {

using ::intrinsic_proto::perception::v1::ToProto;

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_IMAGE_BUFFER_H_
