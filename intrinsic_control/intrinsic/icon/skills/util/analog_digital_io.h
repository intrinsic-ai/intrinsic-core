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

#ifndef INTRINSIC_ICON_SKILLS_UTIL_ANALOG_DIGITAL_IO_H_
#define INTRINSIC_ICON_SKILLS_UTIL_ANALOG_DIGITAL_IO_H_

#include <array>
#include <bitset>
#include <cstddef>
#include <memory>
#include <string>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/hardware/gpio/gpio_service_equipment.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/icon/actions/adio_info.h"
#include "intrinsic/icon/control/parts/io_block.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"

namespace intrinsic::skills {

// This class offers a non-realtime interface for setting analog and digital
// outputs and for waiting on digital inputs. Multiple instances of the class
// can exist for the same piece of equipment and multiple clients can wait for
// the same digital input.
class AnalogDigitalIOInterface {
 public:
  static constexpr size_t kMaxDigitalValuesPerBlock =
      icon::DioBlock::kMaxValuesPerBlock;
  static constexpr size_t kMaxAnalogValuesPerBlock =
      icon::AnalogBlock::kMaxValuesPerBlock;

  // Specifies which bits in a block are active.
  using DigitalBlockMask = std::bitset<kMaxDigitalValuesPerBlock>;
  using AnalogBlockMask = std::bitset<kMaxAnalogValuesPerBlock>;

  // Specifies the values of a block. Block values are read at positions where
  // BlockMasks are active.
  using DigitalBlockValues = std::bitset<kMaxDigitalValuesPerBlock>;
  using AnalogBlockValues = std::array<double, kMaxAnalogValuesPerBlock>;

  struct DigitalOutputBlock {
    std::string name;
    DigitalBlockMask mask;
    DigitalBlockValues values;
  };

  struct AnalogOutputBlock {
    std::string name;
    AnalogBlockMask mask;
    AnalogBlockValues values;
  };

  virtual ~AnalogDigitalIOInterface() = default;

  // Sets multiple outputs to the user specified value.
  absl::Status SetDigitalOutput(absl::string_view output_block_name,
                                const DigitalBlockMask& output_mask,
                                const DigitalBlockValues& values) {
    DigitalOutputBlock block{.name = std::string(output_block_name),
                             .mask = output_mask,
                             .values = values};
    return SetDigitalOutputs({block});
  }

  // Sets multiple outputs to the user specified value for multiple output
  // blocks.
  //
  // The size of `output_block_names`, `output_masks`, and `values` must be
  // the same.
  // Each block in `output_blocks` must have a unique `name`. Returns
  // `AlreadyExistsError` on duplicate names.
  virtual absl::Status SetDigitalOutputs(
      absl::Span<const DigitalOutputBlock> output_blocks) = 0;

  // Sets multiple outputs to the user specified value for multiple output
  // blocks.
  //
  // The size of `output_block_names`, `output_masks`, and `values` must be
  // the same.
  // Each block in `output_blocks` must have a unique `name`. Returns
  // `AlreadyExistsError` on duplicate names.
  virtual absl::Status SetAnalogOutputs(
      absl::Span<const AnalogOutputBlock> output_blocks) = 0;

  // Waits until multiple inputs are set to the desired values.
  virtual absl::Status WaitForInput(absl::string_view input_block_name,
                                    const DigitalBlockMask& input_mask,
                                    const DigitalBlockValues& values,
                                    absl::Duration timeout) = 0;
};

// Implements AnalogDigitalIOInterface using ICON. `SetDigitalOutputs()` and
// SetAnalogOutputs()` require exclusive access to the ADIO part.
// `WaitForInput()` can also be called when the ADIO part is already in use.
class IconAnalogDigitalIO : public AnalogDigitalIOInterface {
 public:
  // Creates an instance of the class and establishes a connection with the
  // specified piece of DIO equipment.  `connection_info` is optional. Address
  // information from `connection_info` will be prioritized, if available.
  // Otherwise it must be provided by adio_config.
  static absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> Create(
      const intrinsic_proto::icon::Icon2AdioPart& adio_equipment_config,
      const icon::ChannelFactory& channel_factory,
      const intrinsic_proto::resources::ResourceConnectionInfo*
          connection_info);

  // Creates a digital IO class with user specified ADIO part and block names.
  IconAnalogDigitalIO(
      std::shared_ptr<ChannelInterface> icon_channel,
      absl::flat_hash_map<std::string, std::string> block_to_part_name);

  absl::Status SetDigitalOutputs(
      absl::Span<const DigitalOutputBlock> output_blocks) override;

  absl::Status SetAnalogOutputs(
      absl::Span<const AnalogOutputBlock> output_blocks) override;

  // Waits until multiple inputs are set to the desired values.
  absl::Status WaitForInput(absl::string_view input_block_name,
                            const DigitalBlockMask& input_mask,
                            const DigitalBlockValues& values,
                            absl::Duration timeout) override;

 private:
  absl::Status SetOutputs(
      const absl::btree_map<std::string, icon::ADIOActionInfo::FixedParams>&
          part_name_and_action_parameters);

  // Creates a map of part names to ADIOActionInfo::FixedParams. btree_map,
  // which is an ordered map, is used to ensure that the output blocks are set
  // in a deterministic order. The order is by part_name.
  absl::StatusOr<
      absl::btree_map<std::string, icon::ADIOActionInfo::FixedParams>>
  CreateSetValueParams(
      absl::Span<const AnalogDigitalIOInterface::DigitalOutputBlock>
          digital_output_blocks,
      absl::Span<const AnalogDigitalIOInterface::AnalogOutputBlock>
          analog_output_blocks);
  std::shared_ptr<ChannelInterface> icon_channel_;
  absl::flat_hash_map<std::string, std::string> block_to_part_name_;
  size_t action_instance_id_ = 0;
};

// Implements the AnalogDigitalIOInterface using the GPIO API.
//
// Note: this implementation is intended to be very temporary - since we
// expect to migrate callers of the AnalogDigitalIOInterface to work more
// directly with the GPIO API. Accordingly, there's a lot of boilerplate-y
// packing and unpacking involved in the implementation of this class.
class GpioAnalogDigitalIO : public AnalogDigitalIOInterface {
 public:
  struct GpioRequests {
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest initial_request;
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_request;
  };

  // Creates an instance of the class and establishes a connection with the
  // specified piece of DIO equipment.
  static absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> Create(
      const intrinsic_proto::gpio::GPIOServiceTarget& gpio_config,
      absl::string_view gpio_instance_name = "");

  absl::Status SetDigitalOutputs(
      absl::Span<const DigitalOutputBlock> output_blocks) override;

  absl::Status SetAnalogOutputs(
      absl::Span<const AnalogOutputBlock> output_blocks) override;

  absl::Status WaitForInput(absl::string_view input_block_name,
                            const DigitalBlockMask& input_mask,
                            const DigitalBlockValues& values,
                            absl::Duration timeout) override;

 private:
  absl::Status SetOutputs(const GpioRequests& requests);

  std::unique_ptr<intrinsic_proto::gpio::v1::GPIOService::StubInterface> stub_;

  // This value is passed as x-gpio-instance-name in gRPC client contexts to
  // allow access to GPIO API instances behind an ingress.
  std::string gpio_instance_name_;
};

}  // namespace intrinsic::skills

#endif  // INTRINSIC_ICON_SKILLS_UTIL_ANALOG_DIGITAL_IO_H_
