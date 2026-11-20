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

#include "intrinsic/util/unique_id.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace intrinsic {
namespace {

// An alphabet that's often useful as an argument to RandomString().
static constexpr char kWebsafe64[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";

// Returns a string made of `size` random characters from `alphabet`.
std::string RandomString(size_t size, absl::string_view alphabet) {
  absl::BitGen gen;
  std::string output(size, ' ');
  absl::uniform_int_distribution<size_t> dist(0, alphabet.size() - 1);

  for (size_t i = 0; i < size; ++i) {
    size_t alphabet_index = dist(gen);
    output[i] = alphabet[alphabet_index];
  }
  return output;
}

}  // namespace

std::string UniqueId() {
  absl::BitGen bitgen;

  // Concatenate two 64 bit numbers to be backwards compatible with the
  // old implementation that used a single 128 bit key (before converting
  // to hex string).
  return absl::StrFormat("%016x%016x", absl::Uniform<uint64_t>(bitgen),
                         absl::Uniform<uint64_t>(bitgen));
}

std::string WebSafeUuid() {
  // Not a true UUID, but for our purposes should be more than enough.
  return RandomString(16, kWebsafe64);
}

}  // namespace intrinsic
