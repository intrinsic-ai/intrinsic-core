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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_EXTENDED_STATUS_CODES_H_
#define INTRINSIC_EXECUTIVE_ENGINE_EXTENDED_STATUS_CODES_H_

#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/util/status/extended_status.pb.h"

namespace intrinsic {
namespace executive {

constexpr std::string_view kExtendedStatusComponent = "ai.intrinsic.executive";
constexpr std::string_view kExtendedStatusFile =
    "intrinsic/executive/status_specs_message.binarypb";

// Register function to initialize an ExtendedStatus in CLIPS. The lifetime of
// the proto_mgr must outlast any calls into this function via CLIPS.
absl::Status RegisterClipsCreateExtendedStatus(
    clips::Environment* env, clips::ProtobufManager* proto_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_EXTENDED_STATUS_CODES_H_
