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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_PROTO_UTILS_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_PROTO_UTILS_H_

#include <optional>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"

namespace intrinsic {
namespace skills {

absl::StatusOr<intrinsic_proto::skills::Skill> BuildSkillProto(
    const intrinsic_proto::skills::SkillManifest& manifest,
    const google::protobuf::FileDescriptorSet& parameter_file_descriptor_set,
    const google::protobuf::FileDescriptorSet& return_value_file_descriptor_set,
    std::optional<absl::string_view> semver_version = std::nullopt);

// A convenience wrapper for the above when all file_descriptor_sets are the
// same.
absl::StatusOr<intrinsic_proto::skills::Skill> BuildSkillProto(
    const intrinsic_proto::skills::SkillManifest& manifest,
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    std::optional<absl::string_view> semver_version = std::nullopt);

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_PROTO_UTILS_H_
