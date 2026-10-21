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

#ifndef INTRINSIC_UTIL_PROTO_PARSE_TEXT_PROTO_H_
#define INTRINSIC_UTIL_PROTO_PARSE_TEXT_PROTO_H_

#include <string>
#include <string_view>
#include <type_traits>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

// Parses the given text proto into the given message. Returns an error if the
// parsing fails, e.g., if the text proto does not match the type of the given
// message.
//
// Note that ParseTextProtoInto and all other ParseTextProto* functions might
// replace parts of custom Intrinsic type URLs with markers like _DOT_, etc.
// during the parsing process. This is automatically undone after parsing unless
// there are nested Any protos (i.e., an Any encoded within an Any).
//
// TODO: b/400685095 - Remove this workaround once TextFormat::Parser supports
// arbitrary type URL prefixes.
absl::Status ParseTextProtoInto(std::string_view asciipb,
                                google::protobuf::Message* message);

// Parses the given text proto as a protocol message of type 'T' and returns the
// result in a StatusOr<T>.
template <typename T, typename = std::enable_if_t<
                          std::is_base_of_v<google::protobuf::Message, T>>>
absl::StatusOr<T> ParseTextProto(absl::string_view asciipb) {
  T message;
  INTR_RETURN_IF_ERROR(ParseTextProtoInto(asciipb, &message));
  return message;
}

namespace internal {

// Internal helper class for ParseTextProtoOrDie() / ParseTextOrDie().
// Do not use directly.
class ParseTextProtoHelper {
 public:
  explicit ParseTextProtoHelper(absl::string_view text) : text_(text) {}

  template <typename T, typename = std::enable_if_t<
                            std::is_base_of_v<google::protobuf::Message, T>>>
  operator T() const {  // NOLINT(google-explicit-constructor)
    auto parse_result = ParseTextProto<T>(text_);
    CHECK_OK(parse_result.status());
    return *parse_result;
  }

 private:
  std::string text_;
  friend ParseTextProtoHelper ParseTextProtoOrDie(absl::string_view);
};

}  // namespace internal

// Parses the given text proto as a protocol message whose type is automatically
// inferred from the return type. If the parsing fails, prints a failure message
// and terminates the program.
inline internal::ParseTextProtoHelper ParseTextProtoOrDie(
    absl::string_view asciipb) {
  return internal::ParseTextProtoHelper(asciipb);
}

// Parses the given text proto as a protocol message of type 'T'. If the parsing
// fails, prints a failure message and terminates the program.
template <typename T, typename = std::enable_if_t<
                          std::is_base_of_v<google::protobuf::Message, T>>>
T ParseTextOrDie(absl::string_view asciipb) {
  return ParseTextProtoOrDie(asciipb);
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_PARSE_TEXT_PROTO_H_
