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

#ifndef INTRINSIC_UTIL_PROTO_TYPE_URL_H_
#define INTRINSIC_UTIL_PROTO_TYPE_URL_H_

#include <string>
#include <string_view>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/message.h"

namespace intrinsic {

constexpr std::string_view kIntrinsicTypeUrlPrefix = "type.intrinsic.ai/";
constexpr std::string_view kIntrinsicTypeUrlAreaSkills = "skills";
constexpr std::string_view kIntrinsicTypeUrlAreaAssets = "assets";
constexpr std::string_view kIntrinsicTypeUrlAreaCommon = "common";
// Deprecated: Use `kIntrinsicTypeUrlAreaCommon` instead.
constexpr std::string_view kIntrinsicTypeUrlAreaWellKnown = "well-known";

constexpr std::string_view kTypeUrlPrefix = "type.googleapis.com/";
constexpr std::string_view kTypeUrlSeparator = "/";

namespace internal {
template <typename T>
std::string PrefixWithSeparator(T&& p) {
  return absl::StrCat(kTypeUrlSeparator, p);
}
}  // namespace internal

inline std::string AddTypeUrlPrefix(
    std::string_view proto_type,
    std::string_view type_url_prefix = kTypeUrlPrefix) {
  if (absl::StartsWith(proto_type, type_url_prefix)) {
    return std::string(proto_type);
  }
  if (absl::EndsWith(type_url_prefix, kTypeUrlSeparator)) {
    return absl::StrCat(type_url_prefix, proto_type);
  }
  return absl::StrCat(type_url_prefix, kTypeUrlSeparator, proto_type);
}

// Generate an Intrinsic-style type URL for protos.
// The general pattern is: type.intrinsic.ai/<area>/<path>/<message type>
// Example:
// GenerateIntrinsicTypeUrl("skills", skill_id, skill_version,
//                          parameter_descriptor->full_name());
// Could result in
// type.intrinsic.ai/skills/ai.intrinsic.my_skill/1.0.0/proto_package.Params
template <typename... T>
std::string GenerateIntrinsicTypeUrl(std::string_view area,
                                     T&&... path_elements) {
  return absl::StrCat(
      kIntrinsicTypeUrlPrefix, area,
      internal::PrefixWithSeparator(std::forward<T>(path_elements))...);
}

// Similar as above, but extract the message full name from the given proto
// message type's descriptor.
// Example:
// GenerateIntrinsicTypeUrl<proto_package::Params>("skills", skill_id,
//                                                 skill_version);
// Could result in:
// type.intrinsic.ai/skills/ai.intrinsic.my_skill/1.0.0/proto_package.Params
template <typename M, typename... T,
          typename =
              std::enable_if_t<std::is_base_of_v<google::protobuf::Message, M>>>
std::string GenerateIntrinsicTypeUrlForMessage(std::string_view area,
                                               T&&... path_elements) {
  return absl::StrCat(
      kIntrinsicTypeUrlPrefix, area,
      internal::PrefixWithSeparator(std::forward<T>(path_elements))...,
      kTypeUrlSeparator, M::descriptor()->full_name());
}

inline std::string_view StripTypeUrlPrefix(
    std::string_view type_url ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  std::string_view::size_type pos = type_url.find_last_of(kTypeUrlSeparator);
  if (pos == std::string_view::npos) {
    return type_url;
  }
  return type_url.substr(pos + 1);
}

// Returns the type URL prefix (without trailing slash) of the given type URL.
// If the given string does not have a slash, returns the empty string.
inline std::string_view ExtractTypeUrlPrefix(std::string_view type_url) {
  std::string_view::size_type pos = type_url.find_last_of(kTypeUrlSeparator);
  if (pos == std::string_view::npos) {
    return "";
  }
  return type_url.substr(0, pos);
}

template <typename M, typename = std::enable_if_t<
                          std::is_base_of_v<google::protobuf::Message, M>>>
inline std::string AddTypeUrlPrefix(
    std::string_view type_url_prefix = kTypeUrlPrefix) {
  return AddTypeUrlPrefix(M::descriptor()->full_name(), type_url_prefix);
}

inline std::string AddTypeUrlPrefix(
    const google::protobuf::Message& m,
    std::string_view type_url_prefix = kTypeUrlPrefix) {
  return AddTypeUrlPrefix(m.GetDescriptor()->full_name(), type_url_prefix);
}

inline std::string AddTypeUrlPrefix(
    const google::protobuf::Message* m,
    std::string_view type_url_prefix = kTypeUrlPrefix) {
  return AddTypeUrlPrefix(m->GetDescriptor()->full_name(), type_url_prefix);
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_TYPE_URL_H_
