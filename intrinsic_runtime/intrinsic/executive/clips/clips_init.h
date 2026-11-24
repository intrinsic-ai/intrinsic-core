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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CLIPS_INIT_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CLIPS_INIT_H_

#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "intrinsic/executive/clips_cpp/context.h"
#include "intrinsic/executive/clips_cpp/environment.h"

namespace intrinsic {
namespace executive {
namespace clips {

// Performs the default initialization of a CLIPS environment.
// - loads the necessary clips files (.clp in this directory)
//   (requires that clips_dir is set to the directory containing these files)
// - asserts an initial empty world
class ProtobufManager;

absl::Status DefaultClipsInitialize(Environment* env,
                                    ProtobufManager* proto_mgr,
                                    TraceSpanManager* span_mgr,
                                    std::string_view clips_dir)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

absl::Status DefaultClipsInitializeForTest(ClipsContext* clips,
                                           std::string_view clips_dir)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips->GetClipsEnvironment()->mutex());

absl::Status InitClipsBehaviorTreeSupport(Environment* env,
                                          ProtobufManager* proto_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

absl::Status InitClipsBehaviorTreeSupportForTest(ClipsContext* clips)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips->GetClipsEnvironment()->mutex());

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CLIPS_INIT_H_
