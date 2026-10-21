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

#include "intrinsic/util/proto/parsed_type_url.h"

#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "intrinsic/util/proto/status_specs.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {

// Parses a type_url or type_url_prefix up until and including the area part.
// On success returns the partially filled ParsedUrl and the remainder of the
// type_url starting after the '/' from the 'area' part. The remainder may be
// empty if there is no path or message type after the area.
absl::StatusOr<std::pair<ParsedUrl, std::string_view>> ParseTypeUrlToArea(
    std::string_view type_url) {
  ParsedUrl parsed_url = {.type_url = std::string(type_url)};

  std::string_view remainder;
  if (type_url.starts_with(kIntrinsicTypeUrlPrefix)) {
    parsed_url.prefix = std::string(kIntrinsicTypeUrlPrefix);
    std::string_view type_url_parsed = type_url;
    type_url_parsed.remove_prefix(kIntrinsicTypeUrlPrefix.length());

    std::pair<std::string_view, std::string_view> area_and_remainder =
        absl::StrSplit(type_url_parsed, absl::MaxSplits(kTypeUrlSeparator, 1));

    if (area_and_remainder.first.empty()) {
      std::string message = absl::StrFormat(
          "Type URL '%s' is missing area after Intrinsic prefix", type_url);
      return (StatusBuilder(absl::StatusCode::kInvalidArgument) << message)
          .AttachExtendedStatus(
              util::proto::kExtendedStatusComponent,
              util::proto::kInvalidUrlCode,
              {.title = util::proto::kInvalidUrlTitle,
               .user_message = message,
               .user_instructions = util::proto::kInvalidUrlInstructions});
    }

    parsed_url.area = std::string(area_and_remainder.first);
    if (parsed_url.area == kIntrinsicTypeUrlAreaWellKnown) {
      parsed_url.area = std::string(kIntrinsicTypeUrlAreaCommon);
    }
    remainder = area_and_remainder.second;
  } else if (type_url.starts_with(kTypeUrlPrefix) ||
             type_url == absl::StripSuffix(kTypeUrlPrefix, kTypeUrlSeparator)) {
    parsed_url.prefix = std::string(kIntrinsicTypeUrlPrefix);
    parsed_url.area = std::string(kIntrinsicTypeUrlAreaCommon);
    remainder = type_url;
    if (remainder.starts_with(kTypeUrlPrefix)) {
      remainder.remove_prefix(kTypeUrlPrefix.length());
    } else {
      remainder = "";
    }
  } else {
    std::string message =
        absl::StrFormat("Type URL '%s' does not start with '%s'", type_url,
                        kIntrinsicTypeUrlPrefix);
    return (StatusBuilder(absl::StatusCode::kInvalidArgument) << message)
        .AttachExtendedStatus(
            util::proto::kExtendedStatusComponent, util::proto::kInvalidUrlCode,
            {.title = util::proto::kInvalidUrlTitle,
             .user_message = message,
             .user_instructions = util::proto::kInvalidUrlInstructions});
  }

  return std::make_pair(std::move(parsed_url), remainder);
}

}  // namespace

absl::StatusOr<ParsedUrl> ParseTypeUrl(std::string_view type_url) {
  INTR_ASSIGN_OR_RETURN(auto parsed_url_and_remainder,
                        ParseTypeUrlToArea(type_url));
  ParsedUrl parsed_url = std::move(parsed_url_and_remainder.first);
  std::string_view remainder_after_area = parsed_url_and_remainder.second;

  std::string_view::size_type last_slash_pos =
      remainder_after_area.rfind(kTypeUrlSeparator);
  if (last_slash_pos == std::string_view::npos) {
    parsed_url.path = "";
    parsed_url.message_type = remainder_after_area;
  } else {
    parsed_url.path = remainder_after_area.substr(0, last_slash_pos);
    parsed_url.message_type = remainder_after_area.substr(last_slash_pos + 1);
  }

  if (parsed_url.message_type.empty()) {
    std::string message =
        absl::StrFormat("Type URL '%s' is missing message type", type_url);
    return (StatusBuilder(absl::StatusCode::kInvalidArgument) << message)
        .AttachExtendedStatus(
            util::proto::kExtendedStatusComponent, util::proto::kInvalidUrlCode,
            {.title = util::proto::kInvalidUrlTitle,
             .user_message = message,
             .user_instructions = util::proto::kInvalidUrlInstructions});
  }

  return parsed_url;
}

absl::StatusOr<ParsedUrl> ParseTypeUrlPrefix(std::string_view type_url_prefix) {
  INTR_ASSIGN_OR_RETURN(auto parsed_url_and_remainder,
                        ParseTypeUrlToArea(type_url_prefix));
  ParsedUrl parsed_url = std::move(parsed_url_and_remainder.first);
  std::string_view remainder_after_area = parsed_url_and_remainder.second;

  if (remainder_after_area.ends_with(kTypeUrlSeparator)) {
    remainder_after_area.remove_suffix(kTypeUrlSeparator.length());
  }

  parsed_url.path = remainder_after_area;

  return parsed_url;
}

std::ostream& operator<<(std::ostream& os, const ParsedUrl& parsed_url) {
  os << "ParsedUrl{type_url: " << parsed_url.type_url
     << ", prefix: " << parsed_url.prefix << ", area: " << parsed_url.area
     << ", path: " << parsed_url.path
     << ", message_type: " << parsed_url.message_type << "}";
  return os;
}

}  // namespace intrinsic
