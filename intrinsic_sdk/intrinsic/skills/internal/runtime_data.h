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

// This file contains data types that are used by the skill service at runtime
// to provide our internal framework access to metadata about skills. Classes
// defined here should not be used in user-facing contexts.

#ifndef INTRINSIC_SKILLS_INTERNAL_RUNTIME_DATA_H_
#define INTRINSIC_SKILLS_INTERNAL_RUNTIME_DATA_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/assets/proto/status_spec.pb.h"
#include "intrinsic/skills/cc/client_common.h"
#include "intrinsic/skills/proto/skill_service_config.pb.h"

namespace intrinsic::skills::internal {

// Contains data about parameters that is required by the skill service at
// runtime.
class ParameterData {
 public:
  ParameterData() = default;
  explicit ParameterData(const google::protobuf::Any& default_value);

  ParameterData(const ParameterData& other) = default;
  ParameterData& operator=(const ParameterData& other) = default;

  const std::optional<google::protobuf::Any>& GetDefault() const {
    return default_;
  }

 private:
  std::optional<google::protobuf::Any> default_ = std::nullopt;
};

// Contains data about return types that is required by the skill service at
// runtime.
class ReturnTypeData {
 public:
  ReturnTypeData() = default;

  ReturnTypeData(const ReturnTypeData& other) = default;
  ReturnTypeData& operator=(const ReturnTypeData& other) = default;
};

// Contains data about execution options for a skill that are relevant to the
// skill services.
class ExecutionOptions {
 public:
  ExecutionOptions() = default;
  ExecutionOptions(bool supports_cancellation,
                   std::optional<absl::Duration> cancellation_ready_timeout,
                   std::optional<absl::Duration> execution_timeout);

  ExecutionOptions(const ExecutionOptions& other) = default;
  ExecutionOptions& operator=(const ExecutionOptions& other) = default;

  // Returns true if the skill supports cancellation during execution.
  bool SupportsCancellation() const { return supports_cancellation_; }

  absl::Duration GetCancellationReadyTimeout() const {
    return cancellation_ready_timeout_;
  }

  absl::Duration GetExecutionTimeout() const { return execution_timeout_; }

 private:
  bool supports_cancellation_ = false;
  absl::Duration cancellation_ready_timeout_ = absl::Seconds(30);
  absl::Duration execution_timeout_ = absl::InfiniteDuration();
};

// Contains data about resources for a skill that are relevant to the
// skill services.
class ResourceData {
 public:
  ResourceData() = default;
  explicit ResourceData(const absl::flat_hash_map<
                        std::string, intrinsic_proto::skills::ResourceSelector>&
                            resources_required);

  ResourceData(const ResourceData& other) = default;
  ResourceData& operator=(const ResourceData& other) = default;

  const absl::flat_hash_map<std::string,
                            intrinsic_proto::skills::ResourceSelector>&
  GetRequiredResources() const {
    return resources_required_;
  }

 private:
  absl::flat_hash_map<std::string, intrinsic_proto::skills::ResourceSelector>
      resources_required_ = {};
};

// Contains information about the status codes that are documented in the
// manifest that the skill may raise.
class StatusSpecs {
 public:
  StatusSpecs() = default;
  explicit StatusSpecs(
      const std::vector<intrinsic_proto::assets::StatusSpec>& specs);
  StatusSpecs(const StatusSpecs& other) = default;
  StatusSpecs& operator=(const StatusSpecs& other) = default;

  absl::StatusOr<intrinsic_proto::assets::StatusSpec> GetSpecForCode(
      uint32_t code) const {
    if (!specs_.contains(code)) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Error code %d is unknown", code));
    }
    return specs_.at(code);
  }

 private:
  // Keyed by status code
  absl::flat_hash_map<uint32_t, intrinsic_proto::assets::StatusSpec> specs_;
};

// Contains data about skills that is relevant to the skill services.
class SkillRuntimeData {
 public:
  SkillRuntimeData() = default;
  SkillRuntimeData(const ParameterData& parameter_data,
                   const ExecutionOptions& execution_options,
                   const ResourceData& resource_data,
                   const StatusSpecs& status_specs, absl::string_view id);

  SkillRuntimeData(const SkillRuntimeData& other) = default;
  SkillRuntimeData& operator=(const SkillRuntimeData& other) = default;

  const ParameterData& GetParameterData() const { return parameter_data_; }

  const ExecutionOptions& GetExecutionOptions() const {
    return execution_options_;
  }

  const ResourceData& GetResourceData() const { return resource_data_; }

  const StatusSpecs& GetStatusSpecs() const { return status_specs_; }

  absl::string_view GetId() const { return id_; }

 private:
  ParameterData parameter_data_;
  ExecutionOptions execution_options_;
  ResourceData resource_data_;
  StatusSpecs status_specs_;
  std::string id_;
};

// Constructs RuntimeData from the given skill proto, and the explicitly
// provided descriptors.
//
// Returns an error if the message name of the descriptor doesn't match the
// expected message names in the skill proto.
//
// This applies a default `cancellation_ready_timeout` of 30 seconds to the
// Execution options if no timeout is specified, in order to match the behavior
// of the skill signature.
absl::StatusOr<SkillRuntimeData> GetRuntimeDataFrom(
    const intrinsic_proto::skills::SkillServiceConfig& skill_service_config);

}  // namespace intrinsic::skills::internal

#endif  // INTRINSIC_SKILLS_INTERNAL_RUNTIME_DATA_H_
