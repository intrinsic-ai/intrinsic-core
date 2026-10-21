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

#ifndef INTRINSIC_UTIL_PROTO_PB_HASH_H_
#define INTRINSIC_UTIL_PROTO_PB_HASH_H_

#include <cstddef>
#include <functional>
#include <string>

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"
#include "google/protobuf/util/message_differencer.h"

namespace intrinsic {

// Hash function for proto messages instead of using gtl::pb_hash and
// gtl::pb_equals
//
// This is useful for using proto messages as keys in absl containers.
//
// Example Usage:
//   absl::flat_hash_map<MyProto, int, pb_hash, pb_equals> my_map;
struct pb_hash {
  size_t operator()(const ::google::protobuf::Message& message) const {
    std::string serialized;
    {
      ::google::protobuf::io::StringOutputStream sos(&serialized);
      ::google::protobuf::io::CodedOutputStream cos(&sos);
      cos.SetSerializationDeterministic(true);
      static_cast<void>(
          message.SerializeToCodedStream(&cos));  // TODO: Handle return value
    }
    return std::hash<std::string>{}(serialized);
  }
};

struct pb_equals {
  bool operator()(const ::google::protobuf::Message& lhs,
                  const ::google::protobuf::Message& rhs) const {
    return ::google::protobuf::util::MessageDifferencer::Equals(lhs, rhs);
  }
};
}  // namespace intrinsic
#endif  // INTRINSIC_UTIL_PROTO_PB_HASH_H_
