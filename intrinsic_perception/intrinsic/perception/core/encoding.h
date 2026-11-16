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

// Encoding and decoding of images. By design, not all PixelTypes are
// supported. Not every type of image can be correctly encoded into every
// format, so we require the user to perform the conversion to a supported type
// manually. See the .cc files for details of what encodings are available.
//
// The encodeImage* functions encode the image into the given buffer, returning
// false on encoding failure.

#ifndef INTRINSIC_PERCEPTION_CORE_ENCODING_H_
#define INTRINSIC_PERCEPTION_CORE_ENCODING_H_

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"

namespace intrinsic {
namespace perception {

enum class Encoding {
  kUnspecified,
  kJpeg,
  kPng,
  kWebp,
  kYuv420p,
};

// A map from sensor id to image encoding.
using EncodingBySensorId = absl::flat_hash_map<int64_t, Encoding>;

template <class ImageTrait>
bool EncodeImagePng(const Image<ImageTrait>& image,
                    std::vector<uint8_t>* buffer,
                    uint8_t compression_level = 1);

template <class ImageTrait>
Image<ImageTrait> DecodeImagePng(const std::vector<uint8_t>& buffer);

template <class ImageTrait>
bool EncodeImageJpeg(const Image<ImageTrait>& image,
                     std::vector<uint8_t>* buffer, uint8_t compression_level);

template <class ImageTrait>
Image<ImageTrait> DecodeImageJpeg(const std::vector<uint8_t>& buffer);

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_CORE_ENCODING_H_
