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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_PART_TEST_HELPER_H_
#define INTRINSIC_ICON_CONTROL_PARTS_PART_TEST_HELPER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/parts/realtime_part.h"
#include "intrinsic/icon/control/parts/realtime_part_factory_common.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/parts/realtime_part_property_access.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

// Allows a non-realtime thread to access and modify the properties of a single
// part by name.
//
// This class is short-lived: It springs into life at the beginning of a
// GetPartProperties or SetPartProperties RPC, taking references to the two
// FixedArrays, and is discarded at the end of the call.
//
// In practice, the FixedArray references come from two AsyncBuffers that
// mediate communication between the realtime and non-realtime thread. The
// surrounding code is responsible for getting and committing the corresponding
// pointers from and into those AsyncBuffers.
class PartPropertyAccess {
 public:
  PartPropertyAccess(
      const absl::flat_hash_map<std::string, size_t>& property_name_to_index,
      absl::FixedArray<PartPropertyValue>& non_rt_to_rt_values
          ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const absl::FixedArray<PartPropertyValue>& rt_to_non_rt_values
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : property_name_to_index_(property_name_to_index),
        non_rt_to_rt_values_(non_rt_to_rt_values),
        rt_to_non_rt_values_(rt_to_non_rt_values) {}

  // Returns the current value of the boolean property `name` as last reported
  // by the realtime thread. Note that this *does not* get updated when you
  // call SetBoolProperty() below, but only when the part explicitly writes it
  // back and you call ReadStatus afterward.
  //
  // Returns an error if
  // * there is no property 'name'
  // * the property exists, but is not a boolean
  absl::StatusOr<bool> GetBoolProperty(absl::string_view name) const;

  // Returns the current value of the double property `name` as last reported
  // by the realtime thread. Note that this *does not* get updated when you
  // call SetDoubleProperty() below, but only when the part explicitly writes it
  // back and you call ReadStatus afterward.
  //
  // Returns an error if
  // * there is no property 'name'
  // * the property exists, but is not a double
  absl::StatusOr<double> GetDoubleProperty(absl::string_view name) const;

  // Sets the value of the boolean property `name`. Note that this change does
  // not become visible to the realtime thread until the underlying storage is
  // committed back to its AsyncBuffer.
  //
  // Returns an error if
  // * there is no property 'name'
  // * the property exists, but is not a boolean
  absl::Status SetBoolProperty(absl::string_view name, bool value);

  // Sets the value of the double property `name`. Note that this change does
  // not become visible to the realtime thread until the underlying storage is
  // committed back to its AsyncBuffer.
  //
  // Returns an error if
  // * there is no property 'name'
  // * the property exists, but is not a double
  absl::Status SetDoubleProperty(absl::string_view name, double value);

 private:
  const absl::flat_hash_map<std::string, size_t>& property_name_to_index_;
  absl::FixedArray<PartPropertyValue>& non_rt_to_rt_values_;
  const absl::FixedArray<PartPropertyValue>& rt_to_non_rt_values_;
};

class PartTestHelper {
 public:
  struct Parameters {
    std::optional<std::string> part_name = std::nullopt;
    std::optional<Context> context = std::nullopt;
    double control_frequency_hz = 0;
    // Use to test part behaviors that expect a hardware resource name (for
    // example to retrieve a kinematics model from the World).
    std::optional<std::string> hardware_resource_name = std::nullopt;
  };

  static absl::StatusOr<std::unique_ptr<PartTestHelper>> CreateFromProtoFactory(
      Parameters helper_parameters, absl::string_view part_type_name,
      const google::protobuf::Message& config);

  // Returns the FeatureInterfaceRegistry for the part under test. Use this to
  // interact with the part.
  FeatureInterfaceRegistry& GetPartFeatureInterfaces();
  const FeatureInterfaceRegistry& GetPartFeatureInterfaces() const;

  // Returns a PartPropertyAccess object that can read and write the part
  // properties of the part under test.
  // NOTE: The return value MUST NOT outlive the PartTestHelper object that
  // created it!
  PartPropertyAccess GetPartPropertyAccess();

  // Calls ReadStatus on the RealtimePart, checking that no malloc operations
  // take place. Inline (and defined in the header) to allow enabling/disabling
  // of the malloc checking macros.
  inline RealtimeStatus ReadStatus();

  // Calls ApplyCommand on the part under test, and resets the `modified` bit of
  // part properties (in non-RT -> RT direction).
  // Also checks that no malloc operations take place, unless
  // `allow_malloc_tests` is false. Inline (and defined in the header) to allow
  // enabling/disabling of the malloc checking macros.
  inline RealtimeStatus ApplyCommand(bool allow_malloc_tests = true);

  // Returns the (static) configuration proto for the part under test, as
  // returned by its factory.
  intrinsic_proto::icon::v1::PartConfig part_config() const {
    return part_config_;
  }

  RealtimePart& part() { return part_; }
  const RealtimePart& part() const { return part_; }

  // Returns the current SafetyStatus. PartTestHelper passes this to the part's
  // ReadStatus() and ApplyCommand() methods.
  SafetyStatus GetSafetyStatus() const;

  // Updates the safety status with `new_mode`. The rest of the safety status
  // remains untouched.
  void SetModeOfSafeOperation(::intrinsic_fbs::ModeOfSafeOperation new_mode);

  // Updates the safety status with a new status for the "enable" button. The
  // rest of the safety status remains untouched.
  void SetEnableButtonStatus(::intrinsic_fbs::ButtonStatus new_button_status);

  // Updates the safety status with a new status for the E-Stop button. The rest
  // of the safety status remains untouched.
  void SetEstopButtonStatus(::intrinsic_fbs::ButtonStatus new_button_status);

  // Updates the safety status with a new requested behavior. The rest of the
  // safety status remains untouched.
  void SetRequestedBehavior(::intrinsic_fbs::RequestedBehavior new_behavior);

 private:
  static absl::StatusOr<std::unique_ptr<PartTestHelper>> CreateFromFactory(
      Parameters helper_parameters,
      absl::FunctionRef<
          absl::StatusOr<PartPtrAndPartConfig>(PartFactoryContext context)>
          factory);

  PartTestHelper(
      Context context,
      absl::flat_hash_map<std::string, size_t> property_name_to_index,
      absl::string_view part_name, std::unique_ptr<RealtimePartInterface> part,
      intrinsic_proto::icon::v1::PartConfig part_config,
      absl::Span<const PartPropertyValue> non_rt_to_rt_values,
      absl::Span<const PartPropertyValue> rt_to_non_rt_values)
      : context_(std::move(context)),
        property_name_to_index_(std::move(property_name_to_index)),
        non_rt_to_rt_buffer_(non_rt_to_rt_values.begin(),
                             non_rt_to_rt_values.end()),
        rt_to_non_rt_buffer_(rt_to_non_rt_values.begin(),
                             rt_to_non_rt_values.end()),
        part_(part_name, std::move(part)),
        part_config_(std::move(part_config)) {}

  Context context_;
  absl::flat_hash_map<std::string, size_t> property_name_to_index_;
  AsyncBuffer<absl::FixedArray<PartPropertyValue>> non_rt_to_rt_buffer_;
  AsyncBuffer<absl::FixedArray<PartPropertyValue>> rt_to_non_rt_buffer_;
  RealtimePart part_;
  intrinsic_proto::icon::v1::PartConfig part_config_;
  SafetyStatus safety_status_;
};

inline RealtimeStatus PartTestHelper::ReadStatus() {
  non_rt_to_rt_buffer_.CommitFreeBuffer();
  absl::FixedArray<PartPropertyValue>* non_rt_to_rt_values_read;
  non_rt_to_rt_buffer_.GetActiveBuffer(&non_rt_to_rt_values_read);
  absl::FixedArray<PartPropertyValue>* rt_to_non_rt_values_write =
      rt_to_non_rt_buffer_.GetFreeBuffer();
  RealtimePartPropertyAccess part_properties(
      /*non_rt_to_rt_values=*/*non_rt_to_rt_values_read,
      /*rt_to_non_rt_values=*/*rt_to_non_rt_values_write);
  // This macro is the reason we define this function in a header. We use
  // preprocessor defines to decide whether or not to execute malloc checks. We
  // set those defines by using the `local_defines` feature ob bazel/bazel.
  // Since `local_defines` does not propagate to dependencies of the target it
  // is set on, we must put the macro in a header, where it gets compiled as
  // part of the dependee target itself.
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus read_status_status =
      part_.ReadStatus(RealtimePartInterface::ReadStatusParameters{
          .part_properties = part_properties, .safety_status = safety_status_});
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  LOG(INFO) << "ReadStatus finished, committing buffer "
            << rt_to_non_rt_values_write;
  rt_to_non_rt_buffer_.CommitFreeBuffer();
  return read_status_status;
}

inline RealtimeStatus PartTestHelper::ApplyCommand(bool allow_malloc_tests) {
  non_rt_to_rt_buffer_.CommitFreeBuffer();
  absl::FixedArray<PartPropertyValue>* non_rt_to_rt_values_read;
  non_rt_to_rt_buffer_.GetActiveBuffer(&non_rt_to_rt_values_read);
  absl::FixedArray<PartPropertyValue>* rt_to_non_rt_values_write =
      rt_to_non_rt_buffer_.GetFreeBuffer();
  RealtimePartPropertyAccess part_properties(*non_rt_to_rt_values_read,
                                             *rt_to_non_rt_values_write);
  // This macro is the reason we define this function in a header. We use
  // preprocessor defines to decide whether or not to execute malloc checks. We
  // set those defines by using the `local_defines` feature ob bazel/bazel.
  // Since `local_defines` does not propagate to dependencies of the target it
  // is set on, we must put the macro in a header, where it gets compiled as
  // part of the dependee target itself.
  if (allow_malloc_tests) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  }
  RealtimeStatus apply_command_status =
      part_.ApplyCommand(RealtimePartInterface::ApplyCommandParameters{
          .part_properties = part_properties, .safety_status = safety_status_});
  if (allow_malloc_tests) {
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }

  INTRINSIC_RT_RETURN_IF_ERROR(apply_command_status);
  rt_to_non_rt_buffer_.CommitFreeBuffer();
  return OkStatus();
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_PARTS_PART_TEST_HELPER_H_
