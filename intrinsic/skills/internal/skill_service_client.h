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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CLIENT_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "grpcpp/client_context.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/skills/internal/skill_service_client_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.grpc.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/thread.h"

// This header defines the Skill Service C++ client library, which is a thin
// wrapper around the Skill Service GRPC API.

namespace intrinsic {
namespace skills {

struct SkillServiceClientConfig {
  absl::Duration timeout = intrinsic::connect::kGrpcClientConnectDefaultTimeout;
};

// A client for the Skill Service services (TestFeasibility, Projector,
// Executor).
//
// This object is moveable but not copyable.
class SkillServiceClient : public SkillServiceClientInterface {
 public:
  SkillServiceClient(
      intrinsic_proto::skills::SkillInstance instance,
      std::unique_ptr<intrinsic_proto::skills::Projector::StubInterface>
          projector_stub,
      std::unique_ptr<intrinsic_proto::skills::Executor::StubInterface>
          executor_stub,
      const SkillServiceClientConfig& config = SkillServiceClientConfig())
      : instance_(std::move(instance)),
        projector_stub_(std::move(projector_stub)),
        executor_stub_(std::move(executor_stub)),
        config_(config) {}

  void InitConcurrentLogging() final;
  // Stops the logging thread. Remaining log items may be discarded.
  void TearDownConcurrentLogging() final;

  // Calls the GetFootprint rpc on a skill and returns the results. Composes the
  // request from the given inputs.
  //
  // If timeout is omitted, a default timeout will be used.
  absl::StatusOr<intrinsic_proto::skills::GetFootprintResult> GetFootprint(
      absl::string_view world_id, const google::protobuf::Any& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context) final;

  absl::Status TryCancelGetFootprint() final;

  // Applies defaults to `params`, as specified by `instance`, then makes an rpc
  // request to SkillService::StartExecute and waits for it to finish. If
  // `instance.has_default_parameters()` is false, does not attempt to apply
  // defaults. The defaults in `instance.default_parameters()` must be of the
  // same type as `params`. Default parameters, are applied according to the
  // rules specified here:
  // intrinsic/skills/internal/default_parameters.h.
  // Note, in particular that:
  // * presence is checked for only at the top level of the parameter message
  // * non-optional singular fields are not considered 'present' if set to the
  //   Protobuf default value.
  //
  // If timeout is omitted, a default timeout will be used.
  absl::StatusOr<intrinsic_proto::skills::ExecuteResult> Execute(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Message& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context) final;

  // The same as above except that defaults are applied by the service, and the
  // defaults used are those returned by the skill registry service for the
  // requested skill.
  absl::StatusOr<intrinsic_proto::skills::ExecuteResult> Execute(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Any& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context) final;

  // Applies defaults to `params`, as specified by `instance`, then makes an rpc
  // request to SkillService::StartExecute. If
  // `instance.has_default_parameters()` is false, does not attempt to apply
  // defaults. The defaults in `instance.default_parameters()` must be of the
  // same type as `params`. Default parameters, are applied according to the
  // rules specified here:
  // intrinsic/skills/internal/default_parameters.h.
  // Note, in particular that:
  // * presence is checked for only at the top level of the parameter message
  // * non-optional singular fields are not considered 'present' if set to the
  //   Protobuf default value.
  // The returned context is used to track the entirety of the execution of the
  // skill. The owner of the context is expected to call LogSummary and dispose
  // of the context as soon as possible after the execution has finished, e.g.,
  // after WaitForExecute() has finished.
  absl::StatusOr<std::unique_ptr<
      SkillServiceClientInterface::ExecuteClientContextInterface>>
  StartExecute(absl::string_view world_id,
               intrinsic_proto::skills::Footprint footprint,
               const google::protobuf::Message& params,
               const intrinsic_proto::data_logger::Context& log_context) final;

  // The same as above except that defaults are applied by the service, and the
  // defaults used are those returned by the skill registry service for the
  // requested skill.
  absl::StatusOr<std::unique_ptr<
      SkillServiceClientInterface::ExecuteClientContextInterface>>
  StartExecute(absl::string_view world_id,
               intrinsic_proto::skills::Footprint footprint,
               const google::protobuf::Any& params,
               const intrinsic_proto::data_logger::Context& log_context) final;

  // If timeout is omitted, a default timeout will be used.
  absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
  WaitForExecuteWithContext(::grpc::ClientContext& wait_ctx,
                            std::optional<absl::Duration> timeout) final;

  absl::Status TryCancelExecute() final;

  // Applies defaults to `params`, as specified by `instance`, then makes an rpc
  // request to SkillService::StartPreview and waits for it to finish. If
  // `instance.has_default_parameters()` is false, does not attempt to apply
  // defaults. The defaults in `instance.default_parameters()` must be of the
  // same type as `params`. Default parameters, are applied according to the
  // rules specified here:
  // intrinsic/skills/internal/default_parameters.h.
  // Note, in particular that:
  // * presence is checked for only at the top level of the parameter message
  // * non-optional singular fields are not considered 'present' if set to the
  //   Protobuf default value.
  //
  // If timeout is omitted, a default timeout will be used.
  absl::StatusOr<intrinsic_proto::skills::PreviewResult> Preview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Message& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context) final;

  // The same as above except that defaults are applied by the service, and the
  // defaults used are those returned by the skill registry service for the
  // requested skill.
  absl::StatusOr<intrinsic_proto::skills::PreviewResult> Preview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Any& params,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context) final;

  // Applies defaults to `params`, as specified by `instance`, then makes an rpc
  // request to SkillService::StartPreview. If
  // `instance.has_default_parameters()` is false, does not attempt to apply
  // defaults. The defaults in `instance.default_parameters()` must be of the
  // same type as `params`. Default parameters, are applied according to the
  // rules specified here:
  // intrinsic/skills/internal/default_parameters.h.
  // Note, in particular that:
  // * presence is checked for only at the top level of the parameter message
  // * non-optional singular fields are not considered 'present' if set to the
  //   Protobuf default value.
  absl::Status StartPreview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Message& params,
      const intrinsic_proto::data_logger::Context& log_context) final;

  // The same as above except that defaults are applied by the service, and the
  // defaults used are those returned by the skill registry service for the
  // requested skill.
  absl::Status StartPreview(
      absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
      const google::protobuf::Any& params,
      const intrinsic_proto::data_logger::Context& log_context) final;

  // If timeout is omitted, a default timeout will be used.
  absl::StatusOr<intrinsic_proto::skills::PreviewResult> WaitForPreview(
      std::optional<absl::Duration> timeout) final;

  absl::Status TryCancelPreview() final;

  absl::Status ClearOperations() final;

  class ExecuteContext : public ExecuteClientContextInterface {
   public:
    ExecuteContext(SkillServiceClient& client ABSL_ATTRIBUTE_LIFETIME_BOUND,
                   const intrinsic_proto::skills::SkillInstance& instance,
                   const intrinsic_proto::skills::ExecuteRequest& request,
                   const intrinsic_proto::data_logger::Context& log_context)
        : client_(client),
          request_(request),
          trace_span_("SkillServiceClient/Execute"),
          skill_instance_(instance),
          log_context_(log_context),
          start_time_(absl::Now()) {}

    void LogSummary(
        const absl::StatusOr<intrinsic_proto::skills::ExecuteResult>& response)
        override;

    void LogExecuteRequest() override;

   private:
    SkillServiceClient& client_;
    const intrinsic_proto::skills::ExecuteRequest request_;
    const stats::ScopedSpan trace_span_;
    const intrinsic_proto::skills::SkillInstance skill_instance_;
    const intrinsic_proto::data_logger::Context log_context_;
    const absl::Time start_time_;
  };

 private:
  absl::StatusOr<intrinsic_proto::skills::GetFootprintResult> GetFootprint(
      const intrinsic_proto::skills::GetFootprintRequest& request,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context);
  absl::StatusOr<intrinsic_proto::skills::ExecuteResult> Execute(
      const intrinsic_proto::skills::ExecuteRequest& request,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context);
  absl::StatusOr<std::unique_ptr<
      SkillServiceClientInterface::ExecuteClientContextInterface>>
  StartExecute(const intrinsic_proto::skills::ExecuteRequest& request);
  absl::StatusOr<intrinsic_proto::skills::PreviewResult> Preview(
      const intrinsic_proto::skills::PreviewRequest& request,
      std::optional<absl::Duration> timeout,
      const intrinsic_proto::data_logger::Context& log_context);
  absl::Status StartPreview(
      const intrinsic_proto::skills::PreviewRequest& request);
  absl::Status AddLogRequest(
      const intrinsic_proto::data_logger::LogItem& log_item);

  intrinsic_proto::skills::SkillInstance instance_;
  std::unique_ptr<intrinsic_proto::skills::Projector::StubInterface>
      projector_stub_;
  std::unique_ptr<intrinsic_proto::skills::Executor::StubInterface>
      executor_stub_;
  SkillServiceClientConfig config_;

  absl::Mutex footprint_mutex_;
  std::unique_ptr<::grpc::ClientContext> footprint_context_
      ABSL_GUARDED_BY(footprint_mutex_);

  absl::Mutex operation_name_mutex_;
  std::string operation_name_;

  // This order ensures that the thread it stopped before the queue is
  // destroyed and thus prevents dangling references.
  std::optional<ConcurrentQueue<intrinsic_proto::data_logger::LogItem>>
      log_queue_;
  Thread thread_;
};

// Creates a client that connects to Skill Service GRPC services (namely,
// Projector and Executor services).
//
// The service addresses are obtained from `instance`.
absl::StatusOr<std::unique_ptr<SkillServiceClient>> CreateSkillServiceClient(
    intrinsic_proto::skills::SkillInstance instance,
    const SkillServiceClientConfig& config = SkillServiceClientConfig());

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CLIENT_H_
