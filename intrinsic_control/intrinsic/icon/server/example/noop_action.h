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

#ifndef INTRINSIC_ICON_SERVER_EXAMPLE_NOOP_ACTION_H_
#define INTRINSIC_ICON_SERVER_EXAMPLE_NOOP_ACTION_H_

#include "absl/strings/string_view.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic {
namespace icon {

inline constexpr absl::string_view kNoopAction = "intrinsic.example.noop";
inline constexpr absl::string_view kNoopActionSlotName = "noop_slot";

// Returns the signature for "intrinsic.example.noop", which has no parameters
// and does nothing when started.
::intrinsic_proto::icon::v1::ActionSignature NoopActionSignature();

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_SERVER_EXAMPLE_NOOP_ACTION_H_
