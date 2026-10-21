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

#ifndef INTRINSIC_UTIL_VERSION_H_
#define INTRINSIC_UTIL_VERSION_H_

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace intrinsic {

// A generic version class for comparing versions, which can also handle
// SemVer2.
// A generic version can be delimited by `.`, `,`, `-`, `_`, or `/`.
// If you want to make sure to only compare SemVer2 compatible versions, you can
// use the `SemVer` factory method.
//
// Examples:
//  Version("1.13.2-alpha1") < Version("1.14")
//  Version("1_2_3") == Version("1_2_3_0")
//  Version("1.2.3-alpha") < Version("1.2.3-beta")
class Version {
 public:
  // Generic version constructor.
  explicit Version(std::string_view version);

  // SemVer2 version creator. Will fail if the passed string is not a valid
  // SemVer2 string.
  static absl::StatusOr<Version> SemVer(std::string_view version);

  bool operator==(const Version& other) const { return Compare(other) == 0; }
  bool operator!=(const Version& other) const { return Compare(other) != 0; }
  bool operator<(const Version& other) const { return Compare(other) < 0; }
  bool operator>(const Version& other) const { return Compare(other) > 0; }
  bool operator<=(const Version& other) const { return Compare(other) <= 0; }
  bool operator>=(const Version& other) const { return Compare(other) >= 0; }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Version& version) {
    sink.Append(version.string_);
  }

 private:
  int Compare(const Version& other) const;

  std::vector<std::string> parts_;
  enum class VersionType {
    kGeneric,
    kSemVer,
    kSemVerPrerelease,
  };
  VersionType version_type_;
  std::string string_;
};

std::ostream& operator<<(std::ostream& os, const Version& version);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_VERSION_H_
