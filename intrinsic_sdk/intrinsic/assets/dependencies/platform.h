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

#ifndef INTRINSIC_ASSETS_DEPENDENCIES_PLATFORM_H_
#define INTRINSIC_ASSETS_DEPENDENCIES_PLATFORM_H_

#include "absl/strings/string_view.h"

namespace intrinsic::assets::dependencies::platform {

inline constexpr absl::string_view kRuntimeAssetID = "ai.intrinsic.runtime";
inline constexpr absl::string_view kRuntimeInstanceName = "intrinsic_runtime";

}  // namespace intrinsic::assets::dependencies::platform

#endif  // INTRINSIC_ASSETS_DEPENDENCIES_PLATFORM_H_
