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

#include "intrinsic/skills/internal/skill_proto_utils.h"

#include <optional>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/assets/proto/documentation.pb.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/util/proto/source_code_info_view.h"
#include "intrinsic/util/status/status_macros.h"
#include "re2/re2.h"

namespace intrinsic {
namespace skills {

namespace {

// This is the recommended regex for semver. It is copied from
// https://semver.org/#is-there-a-suggested-regular-expression-regex-to-check-a-semver-string
constexpr LazyRE2 kSemverRegex = {
    R"reg(^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)reg"};

void StripSourceCodeInfo(
    google::protobuf::FileDescriptorSet& file_descriptor_set) {
  for (google::protobuf::FileDescriptorProto& file :
       *file_descriptor_set.mutable_file()) {
    file.clear_source_code_info();
  }
}

// Add file descriptor set for a skill's parameter and clear source code info.
absl::StatusOr<intrinsic_proto::skills::ParameterDescription>
CreateParameterDescription(
    const intrinsic_proto::skills::ParameterMetadata& metadata,
    const google::protobuf::FileDescriptorSet& file_descriptor_set) {
  intrinsic_proto::skills::ParameterDescription description;

  description.set_parameter_message_full_name(metadata.message_full_name());

  if (metadata.has_default_value()) {
    *description.mutable_default_value() = metadata.default_value();
  }

  *description.mutable_parameter_descriptor_fileset() = file_descriptor_set;

  SourceCodeInfoView source_code_info;
  if (absl::Status status = source_code_info.Init(file_descriptor_set);
      !status.ok()) {
    if (status.code() == absl::StatusCode::kInvalidArgument) {
      return status;
    }
  }

  INTR_ASSIGN_OR_RETURN(
      *description.mutable_parameter_field_comments(),
      source_code_info.GetNestedFieldCommentMap(metadata.message_full_name()));
  StripSourceCodeInfo(*description.mutable_parameter_descriptor_fileset());

  return description;
}

// Add file descriptor set for a skill's return and clear source code info.
absl::StatusOr<intrinsic_proto::skills::ReturnValueDescription>
CreateReturnDescription(
    const intrinsic_proto::skills::ReturnMetadata& metadata,
    const google::protobuf::FileDescriptorSet& file_descriptor_set) {
  intrinsic_proto::skills::ReturnValueDescription description;

  description.set_return_value_message_full_name(metadata.message_full_name());

  *description.mutable_descriptor_fileset() = file_descriptor_set;
  SourceCodeInfoView source_code_info;
  if (absl::Status status = source_code_info.Init(file_descriptor_set);
      !status.ok()) {
    if (status.code() == absl::StatusCode::kInvalidArgument) {
      return status;
    }
  }

  INTR_ASSIGN_OR_RETURN(
      *description.mutable_return_value_field_comments(),
      source_code_info.GetNestedFieldCommentMap(metadata.message_full_name()));
  StripSourceCodeInfo(*description.mutable_descriptor_fileset());

  return description;
}

}  // namespace

absl::StatusOr<intrinsic_proto::skills::Skill> BuildSkillProto(
    const intrinsic_proto::skills::SkillManifest& manifest,
    const google::protobuf::FileDescriptorSet& parameter_file_descriptor_set,
    const google::protobuf::FileDescriptorSet& return_value_file_descriptor_set,
    std::optional<absl::string_view> semver_version) {
  intrinsic_proto::skills::Skill skill;
  skill.set_skill_name(manifest.id().name());
  skill.set_id(manifest.id().package() + "." + manifest.id().name());
  skill.set_package_name(manifest.id().package());
  if (semver_version.has_value()) {
    if (!RE2::FullMatch(*semver_version, *kSemverRegex)) {
      return absl::InvalidArgumentError(
          absl::StrCat("semver_version: ", *semver_version,
                       " is not a valid semver version."));
    }
    skill.set_id_version(absl::StrCat(skill.id(), ".", *semver_version));
  } else {
    skill.set_id_version(skill.id());
  }
  skill.set_description(manifest.documentation().description());
  skill.set_display_name(manifest.display_name());
  *skill.mutable_resource_selectors() =
      manifest.dependencies().required_equipment();

  skill.mutable_execution_options()->set_supports_cancellation(
      manifest.options().supports_cancellation());

  if (manifest.has_parameter()) {
    INTR_ASSIGN_OR_RETURN(
        *skill.mutable_parameter_description(),
        CreateParameterDescription(manifest.parameter(),
                                   parameter_file_descriptor_set));
  }
  if (manifest.has_return_type()) {
    INTR_ASSIGN_OR_RETURN(
        *skill.mutable_return_value_description(),
        CreateReturnDescription(manifest.return_type(),
                                return_value_file_descriptor_set));
  }

  return skill;
}

absl::StatusOr<intrinsic_proto::skills::Skill> BuildSkillProto(
    const intrinsic_proto::skills::SkillManifest& manifest,
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    std::optional<absl::string_view> semver_version) {
  return BuildSkillProto(manifest, file_descriptor_set, file_descriptor_set,
                         semver_version);
}

}  // namespace skills
}  // namespace intrinsic
