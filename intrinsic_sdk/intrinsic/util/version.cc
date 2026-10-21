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

#include "intrinsic/util/version.h"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "intrinsic/util/status/status_builder.h"
#include "re2/re2.h"
#include "third_party/strings_numbers/sort.h"

namespace intrinsic {

// See
// https://semver.org/#is-there-a-suggested-regular-expression-regex-to-check-a-semver-string.
constexpr LazyRE2 kSemVerRegex = {
    R"re(^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)re"};
constexpr char kSemVerDelimiter = '.';
constexpr char kGenericDelimiters[] = ".,-_/";
constexpr char kZero = '0';

Version::Version(std::string_view version) : string_(version) {
  parts_.resize(3);
  std::string prerelease;
  if (RE2::FullMatch(version, *kSemVerRegex, &parts_[0], &parts_[1], &parts_[2],
                     &prerelease, nullptr)) {
    for (std::string_view part : absl::StrSplit(prerelease, kSemVerDelimiter)) {
      parts_.emplace_back(part);
    }
    version_type_ = prerelease.empty() ? VersionType::kSemVer
                                       : VersionType::kSemVerPrerelease;
  } else {
    parts_ =
        absl::StrSplit(absl::StripAsciiWhitespace(version),
                       absl::ByAnyChar(kGenericDelimiters), absl::SkipEmpty());
    version_type_ = VersionType::kGeneric;
  }

  while (parts_.size() > 1 &&
         absl::c_all_of(parts_.back(), [](char c) { return c == kZero; })) {
    parts_.pop_back();
  }
}

absl::StatusOr<Version> Version::SemVer(std::string_view version) {
  Version semver(version);
  if (semver.version_type_ == VersionType::kSemVer ||
      semver.version_type_ == VersionType::kSemVerPrerelease) {
    return semver;
  }
  return intrinsic::InvalidArgumentErrorBuilder()
         << "Invalid SemVer2 version: \"" << version << "\".";
}

int Version::Compare(const Version& other) const {
  const VersionType version_type_a = version_type_;
  const VersionType version_type_b = other.version_type_;
  const std::vector<std::string>& p_a = parts_;
  const std::vector<std::string>& p_b = other.parts_;
  const int size_a = p_a.size();
  const int size_b = p_b.size();

  for (int i = 0; i < size_a && i < size_b; ++i) {
    if (int cmp = AutoDigitStrCmp(p_a[i], p_b[i], /*strict=*/false); cmp != 0) {
      if (version_type_a == VersionType::kSemVerPrerelease &&
          version_type_b == VersionType::kSemVerPrerelease) {
        // Numeric identifiers always have lower precedence than non-numeric
        // identifiers, see also https://semver.org/#spec-item-11.
        const bool digits_a = absl::c_all_of(p_a[i], absl::ascii_isdigit);
        const bool digits_b = absl::c_all_of(p_b[i], absl::ascii_isdigit);
        if (digits_a && !digits_b) {
          return -1;
        } else if (!digits_a && digits_b) {
          return 1;
        }
      }
      return cmp;
    }
  }

  // A pre-release SemVer version has lower precedence than a normal version.
  if (version_type_a == VersionType::kSemVerPrerelease &&
      version_type_b == VersionType::kSemVer) {
    return -1;
  } else if (version_type_a == VersionType::kSemVer &&
             version_type_b == VersionType::kSemVerPrerelease) {
    return 1;
  }
  return size_a - size_b;
}

std::ostream& operator<<(std::ostream& os, const Version& version) {
  return os << absl::StrCat(version);
}

}  // namespace intrinsic
