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

#ifndef INTRINSIC_ICON_CONTROL_ACTION_FACTORY_CONTEXT_H_
#define INTRINSIC_ICON_CONTROL_ACTION_FACTORY_CONTEXT_H_

#include <any>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_registry.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

// ActionFactoryContext holds all information about an Action's environment
// (Slots, streaming I/O channels, etc.). RTCL Action factories receive an
// ActionFactoryContext that they can use to collect all information they need
// to create an RTCL Action instance.
//
// As an added safety measure, ActionFactoryContext keeps track of which
// resources have been accessed, and provides a `Validate()` method that checks
// whether all resources that appear in the Action type's signature are actually
// used.
class ActionFactoryContext {
 public:
  // Holds a reference to `streaming_io_storage`, and so must not outlive the
  // referenced variable. Holds a reference to `trajectory_map`, and therefore
  // must not outlive the referenced variable.
  ActionFactoryContext(
      const ::intrinsic_proto::icon::v1::ServerConfig& server_config,
      const ::intrinsic_proto::icon::v1::ActionSignature& signature,
      const absl::flat_hash_map<std::string, SlotInfo>& slot_name_to_slot_info,
      const absl::flat_hash_map<std::string, RealtimeSignalId>&
          realtime_signal_name_to_signal_id,
      StreamingIoStorage& streaming_io_storage ABSL_ATTRIBUTE_LIFETIME_BOUND,
      ActionInstanceId action_instance_id,
      absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>& trajectory_map)
      : server_config_(server_config),
        signature_(signature),
        slot_name_to_slot_info_(slot_name_to_slot_info),
        realtime_signal_name_to_signal_id_(realtime_signal_name_to_signal_id),
        streaming_io_registry_(streaming_io_storage),
        action_instance_id_(action_instance_id),
        trajectory_map_(trajectory_map) {}

  const ::intrinsic_proto::icon::v1::ServerConfig& ServerConfig() const;

  // Returns a SlotInfo object for `slot_name`.
  // Returns an error if the Action's signature does not contain a slot with
  // that name, or if the slot is marked `is_optional` in the signature.
  //
  // The SlotInfo object contains
  // * Static configuration data for the slot, which a factory can use for
  //   initialization (for example, to allocate memory depending on the number
  //   of joints of a Slot).
  // * A RealtimeSlotId, which the Action uses during cyclic realtime operation
  //   to access the Slot. Notably, the factory does NOT get access to any
  //   references or pointers to the Slot itself. This is because the factory
  //   runs in a non-realtime thread and must not be able to disturb realtime
  //   operation.
  //
  // NOTE: It is an error for the factory to not access the SlotInfo objects for
  // *all* Slots in its Action's signature!
  absl::StatusOr<SlotInfo> GetSlotInfo(absl::string_view slot_name);

  // Returns a SlotInfo object for the optional slot `slot_name`, or nullopt if
  // the slot is not available. An optional slot is one whose `is_optional`
  // field in the action's signature `PartSlotInfo` is true (registered via
  // `AddOptionalPartSlot`).
  //
  // The SlotInfo object contains
  // * Static configuration data for the slot, which a factory can use for
  //   initialization (for example, to allocate memory depending on the number
  //   of joints of a Slot).
  // * A RealtimeSlotId, which the Action uses during cyclic realtime operation
  //   to access the Slot. Notably, the factory does NOT get access to any
  //   references or pointers to the Slot itself. This is because the factory
  //   runs in a non-realtime thread and must not be able to disturb realtime
  //   operation.
  //
  // NOTE: It is an error for the factory to not access the SlotInfo objects for
  // *all* Slots (including optional ones!) in its Action's signature!
  absl::StatusOr<std::optional<SlotInfo>> GetOptionalSlotInfo(
      absl::string_view slot_name);

  // Returns the RealtimeSignalId corresponding to `realtime_signal_name`.
  //
  // Use this ID to refer to the real-time signal from the Action's Sense()
  // method.
  //
  // Returns NotFoundError if `realtime_signal_name` is not a valid signal name.
  // The most likely cause of this is that your action did not register the
  // signal in its signature.
  absl::StatusOr<RealtimeSignalId> GetRealtimeSignalId(
      absl::string_view realtime_signal_name);

  // Registers `parser` to convert data for the streaming input `input_name`
  // from `ProtoT` to `RealtimeT`.
  //
  // Actions use the StreamingInputId that this returns to access streaming
  // input values in realtime functions.
  //
  // Returns an error if there's already a streaming input parser registered
  // for `input_name`.
  //
  // NOTE: It is an error for the factory to not register parsers for *all*
  // streaming inputs in its Action's signature!
  template <typename ProtoT, typename RealtimeT,
            typename = std::enable_if_t<
                std::is_base_of_v<::google::protobuf::Message, ProtoT>>>
  absl::StatusOr<StreamingInputId> AddStreamingInputParser(
      absl::string_view input_name,
      std::function<absl::StatusOr<RealtimeT>(const ProtoT& streaming_input)>
          parser);

  // A variant of AddStreamingInputParser for parsers that take
  // ::google::protobuf::Any and produce absl::any. Most callers should be using
  // AddStreamingInputParser, which takes care of packing / unpacking the
  // output / input automatically.
  //
  // Returns an error if `proto_type_name` does not match the input message
  // type for `input_name` in our `signature_`.
  //
  // RtclActions can use the StreamingInputId that this returns to access
  // streaming input values in realtime functions.
  //
  // Returns an error if there's already a streaming input parser registered for
  // `input_name`.
  absl::StatusOr<StreamingInputId> AddStreamingInputParserAny(
      absl::string_view input_name, absl::string_view proto_type_name,
      std::function<absl::StatusOr<std::any>(
          const ::google::protobuf::Any& streaming_input)>
          parser);

  // Registers `converter` to convert data for the Action's streaming output
  // from `RealtimeT` to `ProtoT`.
  //
  // Returns an error if ProtoT does not match the output message type in our
  // `signature_`.
  //
  // Returns an error if called more than once.
  //
  // NOTE: It is an error for the factory to not register a converter if there
  // is a streaming output in its Action's signature!
  template <typename RealtimeT, typename ProtoT,
            typename = std::enable_if_t<
                std::is_base_of_v<::google::protobuf::Message, ProtoT>>>
  absl::Status AddStreamingOutputConverter(
      std::function<absl::StatusOr<ProtoT>(const RealtimeT& streaming_output)>
          converter);

  // A variant of AddStreamingOutputConverter for converters that a reference to
  // a std::array<char, kMaxStreamingOutputSizeBytes> and produce
  // ::google::protobuf::Any. Most callers should be using
  // AddStreamingOutputConverter, which
  // * takes care of packing / unpacking the output / input automatically
  // * can use its template parameters to
  //   * provide a nicer converter API
  //   * avoid copying unnecessary bytes
  //
  // Returns an error if `proto_type_name` does not match the output message
  // type in our `signature_`.
  //
  // Returns an error if `realtime_type_size` (the size of the data packed into
  // the converter's absl::any input value) is larger than
  // kMaxStreamingOutputSizeBytes.
  //
  // Returns an error if there is already a streaming output converter
  // registered.
  absl::Status AddStreamingOutputConverterAny(
      absl::string_view proto_type_name, size_t realtime_type_size,
      std::function<absl::StatusOr<::google::protobuf::Any>(
          const std::array<char,
                           StreamingIoRegistry::kMaxStreamingOutputSizeBytes>&
              streaming_output)>
          converter);

  // Stores `trajectory` in a flat_hash_map using this Action's ActionInstanceId
  // as key. Overwrites previously stored trajectories for every call to
  // `StoreTrajectory`.
  void StoreTrajectory(const JointTrajectoryPVA& trajectory);

  // Checks that all expected Parts and Streaming I/O values have been accessed.
  absl::Status Validate() const;

 private:
  const ::intrinsic_proto::icon::v1::ServerConfig server_config_;
  const ::intrinsic_proto::icon::v1::ActionSignature signature_;
  const absl::flat_hash_map<std::string, SlotInfo> slot_name_to_slot_info_;
  const absl::flat_hash_map<std::string, RealtimeSignalId>
      realtime_signal_name_to_signal_id_;
  StreamingIoRegistry streaming_io_registry_;

  absl::flat_hash_set<std::string> used_slot_names_;
  absl::flat_hash_set<std::string> used_streaming_inputs_;
  absl::flat_hash_set<std::string> used_realtime_signals_;
  bool streaming_output_set_ = false;

  ActionInstanceId action_instance_id_;
  absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>& trajectory_map_;
};

template <typename ProtoT, typename RealtimeT, typename>
absl::StatusOr<StreamingInputId> ActionFactoryContext::AddStreamingInputParser(
    absl::string_view input_name,
    std::function<absl::StatusOr<RealtimeT>(const ProtoT& streaming_input)>
        parser) {
  INTR_ASSIGN_OR_RETURN(StreamingInputId id,
                        streaming_io_registry_.AddStreamingInputParser(
                            input_name, std::move(parser)));
  used_streaming_inputs_.insert(std::string(input_name));
  return id;
}

template <typename RealtimeT, typename ProtoT, typename>
absl::Status ActionFactoryContext::AddStreamingOutputConverter(
    std::function<absl::StatusOr<ProtoT>(const RealtimeT& streaming_output)>
        converter) {
  INTR_RETURN_IF_ERROR(
      streaming_io_registry_.AddStreamingOutputConverter(std::move(converter)));
  streaming_output_set_ = true;
  return absl::OkStatus();
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTION_FACTORY_CONTEXT_H_
