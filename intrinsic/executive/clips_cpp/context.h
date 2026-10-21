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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_CONTEXT_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_CONTEXT_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "third_party/imported/cpp_libraries/clock/clock.h"

namespace intrinsic::executive::clips {

class ProtobufManager;
class CelManager;
class TraceSpanManager;

class ClipsContext {
 public:
  virtual ~ClipsContext();

  // Creates a ClipsContext with a fresh environment and its managers.
  //
  // `clock` is forwarded to the TraceSpanManager (see
  // TraceSpanManager::Create). It is not owned. If a custom clock is passed, it
  // must outlive the returned ClipsContext. The default real clock is never
  // destroyed.
  static absl::StatusOr<std::unique_ptr<ClipsContext>> Create(
      util::Clock* absl_nonnull clock ABSL_ATTRIBUTE_LIFETIME_BOUND =
          util::Clock::RealClock());

  Environment* GetClipsEnvironment() const { return environment_.get(); }
  ProtobufManager* GetProtobufManager() const { return proto_mgr_.get(); }
  CelManager* GetCelManager() const { return cel_mgr_.get(); }
  TraceSpanManager* GetSpanManager() const { return span_mgr_.get(); }

  absl::Status LoadTestRunfile(absl::string_view clips_filename)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsEnvironment()->mutex());

  absl::Status LoadTestRunfiles(absl::Span<const std::string> clips_filenames)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsEnvironment()->mutex());

 private:
  ClipsContext(std::unique_ptr<Environment> environment,
               std::unique_ptr<ProtobufManager> proto_mgr,
               std::unique_ptr<CelManager> cel_mgr,
               std::unique_ptr<TraceSpanManager> span_mgr);

  std::unique_ptr<Environment> environment_;
  std::unique_ptr<ProtobufManager> proto_mgr_;
  std::unique_ptr<CelManager> cel_mgr_;
  std::unique_ptr<TraceSpanManager> span_mgr_;
};

}  // namespace intrinsic::executive::clips

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_CONTEXT_H_
