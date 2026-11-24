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

#include "intrinsic/executive/clips_cpp/context.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips/cc/cel.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::executive::clips {

absl::StatusOr<std::unique_ptr<ClipsContext>> ClipsContext::Create(
    util::Clock* absl_nonnull clock) {
  auto env = std::make_unique<Environment>();
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<ProtobufManager> proto_mgr,
                        ProtobufManager::Create(env.get()));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<CelManager> cel_mgr,
                        CelManager::Create(env.get(), proto_mgr.get()));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<TraceSpanManager> span_mgr,
                        TraceSpanManager::Create(env.get(), clock));
  return absl::WrapUnique(new ClipsContext(std::move(env), std::move(proto_mgr),
                                           std::move(cel_mgr),
                                           std::move(span_mgr)));
}

ClipsContext::ClipsContext(std::unique_ptr<Environment> environment,
                           std::unique_ptr<ProtobufManager> proto_mgr,
                           std::unique_ptr<CelManager> cel_mgr,
                           std::unique_ptr<TraceSpanManager> span_mgr)
    : environment_(std::move(environment)),
      proto_mgr_(std::move(proto_mgr)),
      cel_mgr_(std::move(cel_mgr)),
      span_mgr_(std::move(span_mgr)) {}

ClipsContext::~ClipsContext() { proto_mgr_->RemoveAllProtos(); }

absl::Status ClipsContext::LoadTestRunfile(absl::string_view clips_filename)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsEnvironment()->mutex()) {
  environment_->mutex()->AssertHeld();
  return environment_->LoadTestRunfile(clips_filename);
}

absl::Status ClipsContext::LoadTestRunfiles(
    absl::Span<const std::string> clips_filenames)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsEnvironment()->mutex()) {
  environment_->mutex()->AssertHeld();
  for (const std::string& clips_filename : clips_filenames) {
    INTR_RETURN_IF_ERROR(environment_->LoadTestRunfile(clips_filename));
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::executive::clips
