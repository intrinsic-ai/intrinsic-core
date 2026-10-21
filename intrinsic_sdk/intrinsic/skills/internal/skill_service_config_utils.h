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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CONFIG_UTILS_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CONFIG_UTILS_H_

#include <optional>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"
#include "intrinsic/skills/proto/skill_service_config.pb.h"

namespace intrinsic::skills {

// Returns the SkillServiceConfig at `skill_service_config_filename`.
//
// Reads the proto binary file at `skill_service_config_filename` and returns
// the contents. This file must contain a proto binary
// intrinsic_proto.skills.SkillServiceConfig message.
absl::StatusOr<intrinsic_proto::skills::SkillServiceConfig>
GetSkillServiceConfig(absl::string_view skill_service_config_filename);

// Returns the SkillServiceConfig for the skill with the given manifest.
absl::StatusOr<intrinsic_proto::skills::SkillServiceConfig>
GetSkillServiceConfigFromManifest(
    absl::string_view manifest_pbbin_filename,
    absl::string_view file_descriptor_set_pbbin_filename,
    std::optional<absl::string_view> version = std::nullopt);
absl::StatusOr<intrinsic_proto::skills::SkillServiceConfig>
GetSkillServiceConfigFromManifest(
    const intrinsic_proto::skills::SkillManifest& manifest,
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    std::optional<absl::string_view> version = std::nullopt);
absl::StatusOr<intrinsic_proto::skills::SkillServiceConfig>
GetSkillServiceConfigFromManifest(
    const intrinsic_proto::skills::SkillManifest& manifest,
    const google::protobuf::FileDescriptorSet& parameter_descriptor_set,
    const google::protobuf::FileDescriptorSet& return_type_descriptor_set,
    std::optional<absl::string_view> version = std::nullopt);

}  // namespace intrinsic::skills

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_CONFIG_UTILS_H_
