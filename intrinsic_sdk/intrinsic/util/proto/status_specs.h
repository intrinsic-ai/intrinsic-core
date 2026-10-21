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

#ifndef INTRINSIC_UTIL_PROTO_STATUS_SPECS_H_
#define INTRINSIC_UTIL_PROTO_STATUS_SPECS_H_

#include <cstdint>

#include "absl/strings/string_view.h"

namespace intrinsic::util::proto {

constexpr absl::string_view kExtendedStatusComponent =
    "ai.intrinsic.util.proto.parsed_type_url";

constexpr uint32_t kInvalidUrlCode = 12001;
constexpr char kInvalidUrlTitle[] = "Invalid type URL";
constexpr char kInvalidUrlInstructions[] =
    "Intrinsic type URLs must start with 'type.intrinsic.ai/' and conform to "
    "the Intrinsic URL specification.";

}  // namespace intrinsic::util::proto

#endif  // INTRINSIC_UTIL_PROTO_STATUS_SPECS_H_
