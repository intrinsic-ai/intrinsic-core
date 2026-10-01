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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CLIENT_INTERFACE_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CLIENT_INTERFACE_H_

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "grpcpp/client_context.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"

// This header defines the Skill Service C++ client library, which is a thin
// wrapper around the Skill Service GRPC API.

namespace intrinsic {
namespace skills {

// A client interface for the Skill Service services (Projector, Executor).
class SkillServiceClientInterface {
 public:
  virtual ~SkillServiceClientInterface() = default;

  // Initializes concurrent logging. An implementation may then run log requests
  // in a separate thread. Tear down concurrent logging after done with the
  // client from the same thread that init was called from.
  virtual void InitConcurrentLogging() = 0;
  virtual void TearDownConcurrentLogging() = 0;

  // Invokes the GetFootprint rpc.
  //
  // If timeout is omitted, a default timeout will be used.
  virtual absl::StatusOr<intrinsic_proto::skills::GetFootprintResult>
  GetFootprint(absl::string_view world_id, const google::protobuf::Any& params,
               std::optional<absl::Duration> timeout,
               const intrinsic_proto::data_logger::Context& context) = 0;

  // Try to cancel a currently running call to GetFootprint().
  // GetFootprint internally uses an asynchronous call, but blocks on that. This
  // method issues a cancellation request, which will cause GetFootprint to fail
  // with a cancellation error.
  virtual absl::Status TryCancelGetFootprint() = 0;

  // Invokes the StartExecute rpc then waits for it to finish.
  //
  // If timeout is omitted, a default timeout will be used.
  virtual absl::StatusOr<intrinsic_proto::skills::ExecuteResult> Execute(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Message& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& context) = 0;

  virtual absl::StatusOr<intrinsic_proto::skills::ExecuteResult> Execute(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Any& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& context) = 0;

  // Context to observe a call to Execute.
  // This is produced by StartExecute() and shall be called for logging summary
  // and errors (if any) by the caller of WaitForExecute() once it announces a
  // skill failure or success.
  class ExecuteClientContextInterface {
   public:
    virtual ~ExecuteClientContextInterface() = default;
    // Call after an execution has finished, i.e., WaitForExecute returns
    // success or a failure that is not a wait timeout.
    virtual void LogSummary(
        const absl::StatusOr<intrinsic_proto::skills::ExecuteResult>&
            response) = 0;

    // Call after a skill execution starts, i.e., after StartExecute returns
    // success or a failure. This will log the request parameters.
    virtual void LogExecuteRequest() = 0;
  };

  // Invokes the StartExecute rpc.
  // Returns a context which:
  // the caller is expected to call LogSummary as soon as
  // WaitForExecute returns success or failure of the skill and that shall be
  // disposed off as soon as possible thereafter.
  virtual absl::StatusOr<std::unique_ptr<ExecuteClientContextInterface>>
  StartExecute(absl::string_view world_id,
               intrinsic_proto::skills::Footprint footprint,
               const google::protobuf::Message& params,
               const intrinsic_proto::data_logger::Context& context) = 0;

  // Returns a context which:
  // the caller is expected to call LogSummary as soon as
  // WaitForExecute returns success or failure of the skill and that shall be
  // disposed off as soon as possible thereafter.
  virtual absl::StatusOr<std::unique_ptr<ExecuteClientContextInterface>>
  StartExecute(absl::string_view world_id,
               intrinsic_proto::skills::Footprint footprint,
               const google::protobuf::Any& params,
               const intrinsic_proto::data_logger::Context& context) = 0;

  // Waits for skill execution operation that was started with StartExecute to
  // finish and returns the result.
  //
  // If timeout is omitted, a default timeout will be used.
  virtual absl::StatusOr<intrinsic_proto::skills::ExecuteResult> WaitForExecute(
      std::optional<absl::Duration> timeout) {
    ::grpc::ClientContext wait_ctx;
    return WaitForExecuteWithContext(wait_ctx, timeout);
  };

  // Same as WaitForExecute(timeout), but accetps a client context provided by
  // the caller.
  //
  // wait_ctx must outlive this call.
  virtual absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
  WaitForExecuteWithContext(::grpc::ClientContext& wait_ctx,
                            std::optional<absl::Duration> timeout) = 0;

  // Tries to cancel a skill execution operation started via Execute or
  // StartExecute. This method issues a cancellation request, which will cause
  // Execute/WaitForExecute to fail with a cancellation error (or an
  // unimplemented error if the skill does not support cancellation).
  virtual absl::Status TryCancelExecute() = 0;

  // Invokes the StartPreview rpc then waits for it to finish.
  //
  // If timeout is omitted, a default timeout will be used.
  virtual absl::StatusOr<intrinsic_proto::skills::PreviewResult> Preview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Message& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& context) = 0;

  virtual absl::StatusOr<intrinsic_proto::skills::PreviewResult> Preview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Any& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& context) = 0;

  // Invokes the StartPreview rpc.
  virtual absl::Status StartPreview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Message& params,
      const intrinsic_proto::data_logger::Context& context) = 0;

  virtual absl::Status StartPreview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Any& params,
      const intrinsic_proto::data_logger::Context& context) = 0;

  // Waits for skill preview operation that was started with StartPreview to
  // finish and returns the result.
  //
  // If timeout is omitted, a default timeout will be used.
  virtual absl::StatusOr<intrinsic_proto::skills::PreviewResult> WaitForPreview(
      std::optional<absl::Duration> timeout) = 0;

  // Tries to cancel a skill preview operation started via StartPreview. This
  // method issues a cancellation request, which will cause WaitForPreview to
  // fail with a cancellation error (or an unimplemented error if the skill does
  // not support cancellation).
  virtual absl::Status TryCancelPreview() = 0;

  // Calls ClearOperations on a skill referenced by this clients id.
  // This will clear all operations for this skill and not just this instance.
  virtual absl::Status ClearOperations() = 0;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CLIENT_INTERFACE_H_
