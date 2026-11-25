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

#include "intrinsic/executive/engine/extended_status_codes.h"

#include <cstdint>
#include <functional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_specs.h"

namespace intrinsic {
namespace executive {

namespace {
constexpr char kEsCreateExtendedStatus[] = "extended-status-create-from-spec";
}  // namespace

absl::Status RegisterClipsCreateExtendedStatus(
    clips::Environment* env, clips::ProtobufManager* proto_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  // Create and initialize an ExtendedStatus from the given code.
  return env->AddFunction(
      kEsCreateExtendedStatus,
      std::function([proto_mgr](int64_t code,
                                const std::string& message) -> int64_t {
        return proto_mgr->AddGeneratedProto(CreateExtendedStatus(code, message))
            .value();
      }));
}

}  // namespace executive
}  // namespace intrinsic
