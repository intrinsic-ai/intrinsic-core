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

#ifndef INTRINSIC_UTIL_PROTO_PARSED_TYPE_URL_H_
#define INTRINSIC_UTIL_PROTO_PARSED_TYPE_URL_H_

#include <ostream>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace intrinsic {

// A type URL of an Any proto consists of a type URL prefix followed by a '/'
// and the full message type. Intrinsic Type URLs have a specific format for the
// type URL prefix.
// The structure of separate parsed elements of a Type URL of
// Intrinsic Type URLs has the form:
// type.intrinsic.ai/<area>/<path>
// The elements are:
// custom prefix: always type.intrinsic.ai
// area: the designated resolver responsible, e.g., skill
// path: the resolver/area-specific path, e.g., <id>/<version> for a skill.
//       This part may be empty (e.g., for well-known types).
// message type: a specific full name of a proto
//
// Note that resolvers must identify a particular file descriptor set only by
// the area and path, not by message type. Caching will be performed based on
// the type URL without the message type.
//
// Example:
// type.intrinsic.ai/skills/my_skill/1.0.0/com.example.MyParameterProto
// |---------- type URL prefix -----------|----- message type --------|
// |- custom prefix-|-area-|-----path-----|----- message type --------|
//
struct ParsedUrl {
  std::string type_url;
  std::string prefix;
  std::string area;
  std::string path;  // May be empty.
  std::string message_type;
};

std::ostream& operator<<(std::ostream& os, const ParsedUrl& parsed_url);

// Parses a complete type URL into its parts.
absl::StatusOr<ParsedUrl> ParseTypeUrl(std::string_view type_url);

// Parses a type URL prefix, i.e., a type URL without the message type.
// This can end in a '/' or not.
// The message_type field in the returned ParsedUrl will be empty.
absl::StatusOr<ParsedUrl> ParseTypeUrlPrefix(std::string_view type_url_prefix);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_PARSED_TYPE_URL_H_
