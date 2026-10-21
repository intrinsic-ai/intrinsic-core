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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_INIT_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_INIT_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/skills/internal/skill_repository.h"
#include "intrinsic/skills/proto/skill_service_config.pb.h"

namespace intrinsic::skills {

// Starts the skill services on a gRPC server at port `skill_service_port`. This
// server hosts services required for serving a skill including:
// * SkillProjectorService
// * SkillExecutorService
// * SkillInformationService
//
// Establishes connections to common clients of skills.
// The `connection_timeout` applies to the establishment of each connection, not
// the cumulative connection time.
//
// The skills services are configured using the proto data contained in the
// service_config.
//
// If setup passes, this method does not return until the gRPC skill server is
// shutdown. This normally occurs when the process is killed.
//
// Returns early on error.
absl::Status SkillInit(
    const intrinsic_proto::skills::SkillServiceConfig& service_config,
    absl::string_view data_logger_grpc_service_address,
    absl::string_view world_service_address,
    absl::string_view geometry_service_address,
    absl::string_view motion_planner_service_address,
    int32_t skill_service_port, absl::Duration connection_timeout,
    SkillRepository& skill_repository);

}  // namespace intrinsic::skills

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_INIT_H_
