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

#ifndef INTRINSIC_SKILLS_CC_SKILL_INTERFACE_H_
#define INTRINSIC_SKILLS_CC_SKILL_INTERFACE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"

// IWYU pragma: begin_exports
#include "intrinsic/skills/cc/execute_context.h"
#include "intrinsic/skills/cc/execute_request.h"
#include "intrinsic/skills/cc/get_footprint_context.h"
#include "intrinsic/skills/cc/get_footprint_request.h"
#include "intrinsic/skills/cc/preview_context.h"
#include "intrinsic/skills/cc/preview_request.h"
// IWYU pragma: end_exports

namespace intrinsic {
namespace skills {

// Interface for skill projecting.
//
// Implementations of SkillProjectInterface predict how a skill might behave
// during execution. The methods of this interface should be invokable prior to
// execution to allow a skill to:
// * provide an understanding of what the footprint of the skill on the workcell
//   will be when it is executed.
class SkillProjectInterface {
 public:
  // Returns the resources required for running this skill.
  //
  // Skill developers should override this method with their implementation.
  //
  // If a skill does not implement GetFootprint(), the default implementation
  // specifies that the skill needs exclusive access to everything. The skill
  // will therefore not be able to execute in parallel with any other skill.
  //
  // `request` the get footprint request.
  // `context` provides access to services that the skill may use.
  //
  // Returns the skill's footprint.
  virtual absl::StatusOr<intrinsic_proto::skills::Footprint> GetFootprint(
      const GetFootprintRequest& request, GetFootprintContext& context) const {
    intrinsic_proto::skills::Footprint result;
    result.set_lock_the_universe(true);
    return std::move(result);
  }

  virtual ~SkillProjectInterface() = default;
};

// Interface for skill execution.
class SkillExecuteInterface {
 public:
  // Executes the skill.
  //
  // If the skill implementation supports cancellation, it should:
  // 1) Set `supports_cancellation` to true in its manifest.
  // 2) Stop as soon as possible and leave resources in a safe and recoverable
  //    state when a cancellation request is received (via its ExecuteContext).
  //    Cancelled skill executions should end by returning
  //    `absl::CancelledError`;
  //
  // Any error status returned by the skill will be handled by the executive
  // that runs the process to which the skill belongs. The effect of the error
  // will depend on how the skill is integrated into that process' behavior
  // tree. For instance, if the skill is part of a fallback node, a skill error
  // will trigger the fallback behavior. If the skill is not part of any node
  // that handles errors, then a skill error will cause the process to fail.
  //
  // Currently, there is no way to distinguish between potentially recoverable
  // failures that should lead to fallback handling (e.g., failure to achieve
  // the skill's objective) and other failures that should cause the entire
  // process to abort (e.g., failure to connect to a gRPC service).
  //
  // `request` the execute request.
  // `context` provides access to services that the skill may use.
  //
  // If skill execution succeeds, the skill should return either the
  // skill's result proto message, or nullptr if the skill has no output.
  // If the skill fails for one of the following reasons, it should return the
  // specified error along with a descriptive and, if possible, actionable error
  // message:
  // - `absl::CancelledError` if the skill is aborted due to a cancellation
  //   request (assuming the skill supports the cancellation).
  // - `absl::InvalidArgumentError` if the arguments provided as skill
  //   parameters are invalid.
  //
  // Refer to absl status documentation for other errors:
  // https://github.com/abseil/abseil-cpp/blob/master/absl/status/status.h
  virtual absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> Execute(
      const ExecuteRequest& request, ExecuteContext& context) = 0;

  // Previews the expected outcome of executing the skill.
  //
  // Preview() enables an application developer to perform a "dry run" of skill
  // execution (or execution of a process that includes that skill) in order to
  // preview the effect of executing the skill/process, but without any
  // real-world side-effects that normal execution would entail.
  //
  // Skill developers should override this method with their implementation. The
  // implementation will not have access to hardware resources that are provided
  // to Execute(), but will have access to a hypothetical world in which to
  // preview the execution. The implementation should return the expected output
  // of executing the skill in that world.
  //
  // If a skill does not override the default implementation, any process that
  // includes that skill will not be executable in "preview" mode.
  //
  // NOTE: In preview mode, the object world provided by the PreviewContext
  // is treated as the -actual- state of the physical world, rather than as the
  // belief state that it represents during normal skill execution. Because of
  // this difference, a skill in preview mode cannot introduce or correct
  // discrepancies between the physical and belief world (since they are
  // identical). For example, if a perception skill only updates the belief
  // state when it is executed, then its implementation of Preview() would
  // necessarily be a no-op.
  //
  // If executing the skill is expected to affect the physical world, then the
  // implementation should record the timing of its expected effects using
  // context.RecordWorldUpdate(). It should NOT make changes to the object
  // world via interaction with context.object_world().
  //
  // skill_interface_utils.h provides convenience utils that can be used to
  // implement Preview() in common scenarios. E.g.:
  // - PreviewViaExecute(): If executing the skill does not require resources or
  //   modify the world.
  //
  // `request` the preview request.
  // `context` provides access to services that the skill may use.
  //
  // If skill preview succeeds, the skill should return either the skill's
  // expected result proto message, or nullptr if the skill has no output.
  // If the skill fails for one of the following reasons, it should return the
  // specified error along with a descriptive and, if possible, actionable error
  // message:
  // - `absl::CancelledError` if the skill is aborted due to a cancellation
  //   request (assuming the skill supports the cancellation).
  // - `absl::InvalidArgumentError` if the arguments provided as skill
  //   parameters are invalid.
  //
  // Refer to absl status documentation for other errors:
  // https://github.com/abseil/abseil-cpp/blob/master/absl/status/status.h
  virtual absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> Preview(
      const PreviewRequest& request, PreviewContext& context) {
    return absl::UnimplementedError("Skill has not implemented `Preview`.");
  }

  virtual ~SkillExecuteInterface() = default;
};

// Interface for skills.
//
// This interface combines all skill constituents:
// - SkillProjectInterface: Skill prediction.
// - SkillExecuteInterface: Skill execution.
class SkillInterface : public SkillProjectInterface,
                       public SkillExecuteInterface {
 public:
  ~SkillInterface() override = default;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_CC_SKILL_INTERFACE_H_
