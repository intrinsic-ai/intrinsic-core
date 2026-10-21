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

#ifndef INTRINSIC_UTIL_PROTO_GET_TEXT_PROTO_H_
#define INTRINSIC_UTIL_PROTO_GET_TEXT_PROTO_H_

#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

// Loads a text proto file and parses it into proto.
//
// Returns a NotFoundError if the file cannot be read.  Returns an
// InvalidArgumentError if parsing fails. Errors during parsing are reported in
// the returned status.  Warnings during parsing are logged.
absl::Status GetTextProto(absl::string_view filename,
                          google::protobuf::Message& proto);

// Loads a text proto file and parses it into a proto. The input can be
// represented as an Any proto.
template <typename T>
absl::StatusOr<T> GetTextProtoAllowingAny(absl::string_view filename) {
  static_assert(std::is_base_of<google::protobuf::Message, T>::value,
                "GetTextProtoAllowingAny() template parameter T must be a "
                "google::protobuf::Message.");

  T proto;
  if (GetTextProto(filename, proto).ok()) {
    return proto;
  }

  google::protobuf::Any any;
  INTR_RETURN_IF_ERROR(GetTextProto(filename, any));
  return UnpackAny<T>(any);
}

// Implementation details below.
namespace internal {
absl::Status GetTextProtoPortable(absl::string_view filename,
                                  google::protobuf::Message& proto);
}  // namespace internal

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_GET_TEXT_PROTO_H_
