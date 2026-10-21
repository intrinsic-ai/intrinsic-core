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

// Server with single-skill based services.

#include <cstdint>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/time/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/skills/internal/runtime_data.h"
#include "intrinsic/skills/internal/single_skill_factory.h"
#include "intrinsic/skills/internal/skill_init.h"
#include "intrinsic/skills/internal/skill_service_config_utils.h"
#include "intrinsic/stats/opencensus.h"  
#include "intrinsic/util/status/status_specs.h"
// clang-format off
{{- range .CCHeaderPaths }}
#include "{{ . }}"
{{- end }}
// clang-format on

namespace {
using ::intrinsic::connect::kGrpcClientConnectDefaultTimeout;
}  // namespace

ABSL_FLAG(int32_t, port, 8001, "Port to serve gRPC on.");
ABSL_FLAG(std::string, skill_service_config_filename, "",
          "Filename for the SkillServiceConfig binary proto. When present, an "
          "additional server (skill information) is started. The skill "
          "registry queries this server to get information about this skill.");
ABSL_FLAG(std::string, data_logger_grpc_service_address, "",
          "(optional) Address of the Intrinsic DataLogger gRPC service.");
ABSL_FLAG(std::string, world_service_address,
          "world.app-intrinsic-app-chart.svc.cluster.local:8080",
          "gRpc target for the World service");
ABSL_FLAG(std::string, geometry_service_address,
          "geomservice.app-intrinsic-base.svc.cluster.local:8080",
          "gRpc target for the geometry service");
ABSL_FLAG(std::string, motion_planner_service_address,
          "motion-planner-service.app-intrinsic-app-chart.svc.cluster.local:"
          "8080",
          "gRpc target for the motion planner service");
// Deprecated: gRPC target for the skill registry service.
// This flag is a no-op and only kept to maintain compatibility with deployment
// templates that still pass it.
// TODO(b/305062153): Remove this flag.
ABSL_FLAG(std::string, skill_registry_service_address, "",
          "gRPC target for the skill registry service (deprecated/no-op)");
ABSL_FLAG(int32_t, grpc_connect_timeout_secs,
          absl::ToInt64Seconds(kGrpcClientConnectDefaultTimeout),
          "Time to wait for other grpc services to become available.");

ABSL_FLAG(bool, logtostderr, true, "Dummy flag, do not use");





namespace {

using ::intrinsic::InitExtendedStatusSpecs;
using ::intrinsic::skills::GetSkillServiceConfig;
using ::intrinsic::skills::SkillInit;
using ::intrinsic::skills::internal::GetRuntimeDataFrom;
using ::intrinsic::skills::internal::SingleSkillFactory;
using ::intrinsic::skills::internal::SkillRuntimeData;
using ::intrinsic_proto::skills::SkillServiceConfig;

}  // namespace

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin open_census;  

  absl::StatusOr<SkillServiceConfig> service_config =
      GetSkillServiceConfig(absl::GetFlag(FLAGS_skill_service_config_filename));
  QCHECK_OK(service_config.status())
      << "Failed to read skill service config at: "
      << absl::GetFlag(FLAGS_skill_service_config_filename);

  QCHECK_OK(InitExtendedStatusSpecs(service_config->skill_description().id(),
                                    service_config->status_info()))
      << "Failed to initialize status specs from config";

  // clang-format off
  absl::StatusOr<SkillRuntimeData> runtime_data = GetRuntimeDataFrom(
      *service_config);
  // clang-format on
  QCHECK_OK(runtime_data.status()) << "Failed to create SkillRuntimeData";

  // clang-format off
  SingleSkillFactory skill_factory(
      *runtime_data,
      {{.CreateSkillMethod}});
  // clang-format on
  QCHECK_OK(SkillInit(
      *service_config, absl::GetFlag(FLAGS_data_logger_grpc_service_address),
      absl::GetFlag(FLAGS_world_service_address),
      absl::GetFlag(FLAGS_geometry_service_address),
      absl::GetFlag(FLAGS_motion_planner_service_address),
      absl::GetFlag(FLAGS_port),
      absl::Seconds(absl::GetFlag(FLAGS_grpc_connect_timeout_secs)),
      skill_factory))
      << "Initializing skill service failed.";
  return 0;
}
