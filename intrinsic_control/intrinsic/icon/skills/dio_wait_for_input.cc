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

#include "intrinsic/icon/skills/dio_wait_for_input.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/skills/dio_wait_for_input.pb.h"
#include "intrinsic/icon/skills/util/analog_digital_io.h"
#include "intrinsic/icon/skills/util/analog_digital_io_factory.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::skills {

using ::intrinsic_proto::skills::DigitalInputBlock;

std::unique_ptr<SkillInterface> DioWaitForInput::CreateSkill() {
  return std::make_unique<DioWaitForInput>();
}

absl::StatusOr<intrinsic_proto::skills::Footprint>
DioWaitForInput::GetFootprint(const GetFootprintRequest& request,
                              GetFootprintContext& context) const {
  intrinsic_proto::skills::Footprint result;
  // Do not lock the universe to allow parallel skill execution.
  result.set_lock_the_universe(false);
  return std::move(result);
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DioWaitForInput::Execute(const ExecuteRequest& request,
                         ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::DioWaitForInputParams>(),
      _.LogError());

  if (!params.has_timeout()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "The '%s' skill requires that the parameter `timeout` is set.",
        kSkillName));
  }

  INTR_ASSIGN_OR_RETURN(const absl::Duration timeout,
                        ToAbslDuration(params.timeout()));

  std::optional<DigitalInputBlock> input_block = std::nullopt;

  if (params.block_name() != "" || !params.indices().empty() ||
      !params.values().empty()) {
    LOG(WARNING) << absl::StrFormat(
        "The '%s' skill was called with deprecated parameters `block_name`, "
        "`indices` and `values`. Please use the single `input_block` parameter "
        "instead.",
        kSkillName);

    DigitalInputBlock legacy_block;
    legacy_block.set_block_name(params.block_name());
    *legacy_block.mutable_indices() = params.indices();
    *legacy_block.mutable_values() = params.values();
    input_block = legacy_block;
  }

  // Fail when both new and legacy input parameters are defined.
  if (params.has_input_block() && input_block) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "The '%s' skill mixes deprecated parameters with the new `input_block` "
        "parameter. Please remove the legacy parameters `block_name`, "
        "`indices` and `values`, and keep the single `input_block` parameter "
        "instead.",
        kSkillName));
  }

  // Fail when no parameter is defined.
  if (!params.has_input_block() && !input_block) {
    return absl::InvalidArgumentError(
        absl::StrFormat("The '%s' skill requires that at least one input is "
                        "provided using `input_block`.",
                        kSkillName));
  }
  // Use the new parameters.
  if (params.has_input_block()) {
    input_block = params.input_block();
  }

  AnalogDigitalIOInterface::DigitalBlockMask mask;
  AnalogDigitalIOInterface::DigitalBlockValues values;
  if (input_block->indices_size() != input_block->values_size()) {
    return absl::InvalidArgumentError(
        "The same number of DIO indices and values are required by the skill.");
  }
  for (uint32_t i = 0; i < input_block->indices_size(); ++i) {
    const uint32_t dio_index = input_block->indices(i);
    const bool dio_value = input_block->values(i);
    mask.set(dio_index);
    values.set(dio_index, dio_value);
  }

  INTR_ASSIGN_OR_RETURN(auto digital_io, CreateAnalogDigitalIO(params.adio()),
                        _.LogError());

  const absl::Time deadline = absl::Now() + timeout;
  context.canceller().Ready();

  while (true) {
    const absl::Duration remaining_time = deadline - absl::Now();
    // Wait at most 1s so that the skill can be cancelled within a reasonable
    // interval.
    const absl::Duration step_timeout =
        std::min(remaining_time, absl::Seconds(1));

    if (step_timeout <= absl::ZeroDuration()) {
      return absl::DeadlineExceededError("Timeout waiting for DIO.");
    }

    absl::Status s = digital_io->WaitForInput(input_block->block_name(), mask,
                                              values, step_timeout);
    if (s.ok()) {
      return nullptr;
    }
    if (s.code() != absl::StatusCode::kDeadlineExceeded) {
      // Only retry when step_timeout is reached, expose other
      // errors.
      return s;
    }
    if (context.canceller().cancelled()) {
      return absl::CancelledError("User requested cancellation");
    }
    // overall timeout reached
    if (absl::Now() >= deadline) {
      LOG(ERROR) << "Timeout waiting for DIO: " << s;
      return s;
    }
  }

  // should not be reached
  return nullptr;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DioWaitForInput::Preview(const PreviewRequest& request,
                         PreviewContext& context) {
  return nullptr;
}

}  // namespace intrinsic::skills
