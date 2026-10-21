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

#include "intrinsic/util/status/annotate.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace intrinsic {

absl::Status AnnotateError(const absl::Status& status,
                           std::string_view message) {
  if (status.ok()) {
    return status;
  }
  absl::Status new_status(status.code(),
                          absl::StrCat(status.message(), "; ", message));
  status.ForEachPayload(
      [&new_status](absl::string_view type_url, const absl::Cord& payload) {
        new_status.SetPayload(type_url, payload);
      });

  return new_status;
}

absl::Status PrependError(const absl::Status& status,
                          std::string_view message) {
  if (status.ok()) {
    return status;
  }
  absl::Status new_status(status.code(),
                          absl::StrCat(message, " ", status.message()));
  status.ForEachPayload(
      [&new_status](absl::string_view type_url, const absl::Cord& payload) {
        new_status.SetPayload(type_url, payload);
      });

  return new_status;
}

}  // namespace intrinsic
