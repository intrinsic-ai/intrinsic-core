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

#ifndef INTRINSIC_UTIL_HASH_H_
#define INTRINSIC_UTIL_HASH_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "third_party/thorough_hash/thorough_hash.h"

namespace intrinsic {

// Returns a fingerprint of the given string.
inline uint64_t Fingerprint(absl::string_view s) {
  return ThoroughHash(s.data(), s.size());
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_HASH_H_
