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

#include "intrinsic/hardware/gripper/service_asset/gripper_service_utils.h"

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/hardware/gpio/icon_gpio_service_config.pb.h"
#include "intrinsic/hardware/gpio/opcua_gpio_service_config.pb.h"
#include "intrinsic/hardware/gpio/v1/signal.pb.h"
#include "intrinsic/hardware/gripper/eoat/eoat_service.pb.h"
#include "intrinsic/hardware/gripper/eoat/gripper_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/gripper_service_signals.pb.h"
#include "intrinsic/hardware/gripper/service_asset/pinch_gripper_opcua_service_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/pinch_gripper_realtime_control_service_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/suction_gripper_opcua_service_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/suction_gripper_realtime_control_service_config.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace gripper {

constexpr char kIconAddress[] =
    "istio-ingressgateway.app-ingress.svc.cluster.local:80";

using ::intrinsic_proto::gripper_service::PinchGripperOpcuaServiceConfig;
using ::intrinsic_proto::gripper_service::
    PinchGripperRealtimeControlServiceConfig;
using ::intrinsic_proto::gripper_service::PinchGripperSignals;
using ::intrinsic_proto::gripper_service::Signals;
using ::intrinsic_proto::gripper_service::SuctionGripperOpcuaServiceConfig;
using ::intrinsic_proto::gripper_service::
    SuctionGripperRealtimeControlServiceConfig;
using ::intrinsic_proto::gripper_service::SuctionGripperSignals;

namespace {

// Converts public-facing signals proto to a version that is used internally.
// Each processed signal name is added to the ids parameter.
absl::StatusOr<::intrinsic_proto::eoat::SignalConfig> ConvertSignals(
    const Signals& signals, std::vector<std::string>& ids) {
  ::intrinsic_proto::eoat::SignalConfig signal_config;
  for (const auto& [key, value] : signals.bool_values()) {
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    signal_value.set_bool_value(value);
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  for (const auto& [key, value] : signals.int_values()) {
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    signal_value.set_int_value(value);
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  for (const auto& [key, value] : signals.uint_values()) {
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    signal_value.set_unsigned_int_value(value);
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  for (const auto& [key, value] : signals.float_values()) {
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    signal_value.set_float_value(value);
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  for (const auto& [key, value] : signals.double_values()) {
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    signal_value.set_double_value(value);
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  for (const auto& [key, value] : signals.int8_values()) {
    ::intrinsic_proto::gpio::v1::Int8 int8;
    int8.set_value(value);
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    *signal_value.mutable_int8_value() = int8;
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  for (const auto& [key, value] : signals.uint8_values()) {
    ::intrinsic_proto::gpio::v1::Uint8 uint8;
    uint8.set_value(value);
    ::intrinsic_proto::gpio::v1::SignalValue signal_value;
    *signal_value.mutable_unsigned_int8_value() = uint8;
    (*signal_config.mutable_value_set()->mutable_values())[key] = signal_value;
    ids.push_back(key);
  }
  return signal_config;
}

// Converts public-facing pinch gripper signals proto to a version that is used
// internally. Each processed signal name is added to the ids parameter.
absl::StatusOr<intrinsic_proto::eoat::PinchGripperConfig>
ConvertPinchGripperConfig(const PinchGripperSignals& signals,
                          std::vector<std::string>& ids) {
  intrinsic_proto::eoat::PinchGripperConfig config;
  if (signals.has_grasp()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_grasp(),
                          ConvertSignals(signals.grasp(), ids));
  }
  if (signals.has_release()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_release(),
                          ConvertSignals(signals.release(), ids));
  }
  if (signals.has_gripping_indicated()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_gripping_indicated(),
                          ConvertSignals(signals.gripping_indicated(), ids));
  }
  return config;
}

// Converts public-facing suction gripper signals proto to a version that is
// used internally. Each processed signal name is added to the ids parameter.
absl::StatusOr<intrinsic_proto::eoat::SuctionGripperConfig>
ConvertSuctionGripperConfig(const SuctionGripperSignals& signals,
                            std::vector<std::string>& ids) {
  intrinsic_proto::eoat::SuctionGripperConfig config;
  if (signals.has_grasp()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_grasp(),
                          ConvertSignals(signals.grasp(), ids));
  }
  if (signals.has_release()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_release(),
                          ConvertSignals(signals.release(), ids));
  }
  if (signals.has_blowoff_on()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_blowoff_on(),
                          ConvertSignals(signals.blowoff_on(), ids));
  }
  if (signals.has_blowoff_off()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_blowoff_off(),
                          ConvertSignals(signals.blowoff_off(), ids));
  }
  if (signals.has_gripping_indicated()) {
    INTR_ASSIGN_OR_RETURN(*config.mutable_gripping_indicated(),
                          ConvertSignals(signals.gripping_indicated(), ids));
  }
  return config;
}

}  // namespace

absl::StatusOr<intrinsic_proto::eoat::GripperConfig>
MakeSuctionGripperRealtimeControlConfig(
    const SuctionGripperRealtimeControlServiceConfig& config) {
  intrinsic_proto::eoat::GripperConfig gripper_config;
  std::vector<std::string> ids;
  INTR_ASSIGN_OR_RETURN(*gripper_config.mutable_suction(),
                        ConvertSuctionGripperConfig(config.signals(), ids));

  intrinsic_proto::eoat::GpioServiceEndpoint endpoint;
  endpoint.set_grpc_address(kIconAddress);
  endpoint.set_gpio_service_name(config.service_instance_name());

  *gripper_config.mutable_gpio_service()->mutable_endpoint() = endpoint;
  return gripper_config;
}

absl::StatusOr<intrinsic_proto::eoat::GripperConfig>
MakeSuctionGripperOpcuaConfig(const SuctionGripperOpcuaServiceConfig& config) {
  intrinsic_proto::eoat::GripperConfig gripper_config;
  std::vector<std::string> opcua_node_ids;
  INTR_ASSIGN_OR_RETURN(
      *gripper_config.mutable_suction(),
      ConvertSuctionGripperConfig(config.signals(), opcua_node_ids));

  intrinsic_proto::gpio::OpcuaGpioServiceConfig opcua_gpio_config;
  opcua_gpio_config.set_opcua_server_address(config.opcua_server_address());
  for (const std::string& opcua_node_id : opcua_node_ids) {
    opcua_gpio_config.mutable_opcua_nodes()->add_node_id(opcua_node_id);
  }
  if (!gripper_config.mutable_gpio_service()->mutable_config()->PackFrom(
          opcua_gpio_config)) {
    return absl::InternalError("Failed to pack opcua gpio config.");
  }

  return gripper_config;
}

absl::StatusOr<intrinsic_proto::eoat::GripperConfig>
MakePinchGripperRealtimeControlConfig(
    const PinchGripperRealtimeControlServiceConfig& config) {
  intrinsic_proto::eoat::GripperConfig gripper_config;
  std::vector<std::string> ids;
  INTR_ASSIGN_OR_RETURN(*gripper_config.mutable_pinch(),
                        ConvertPinchGripperConfig(config.signals(), ids));

  intrinsic_proto::eoat::GpioServiceEndpoint endpoint;
  endpoint.set_grpc_address(kIconAddress);
  endpoint.set_gpio_service_name(config.service_instance_name());

  *gripper_config.mutable_gpio_service()->mutable_endpoint() = endpoint;

  gripper_config.mutable_pinch()->set_is_default_closed(
      config.is_default_closed());

  return gripper_config;
}

absl::StatusOr<intrinsic_proto::eoat::GripperConfig>
MakePinchGripperOpcuaConfig(const PinchGripperOpcuaServiceConfig& config) {
  intrinsic_proto::eoat::GripperConfig gripper_config;
  std::vector<std::string> opcua_node_ids;
  INTR_ASSIGN_OR_RETURN(
      *gripper_config.mutable_pinch(),
      ConvertPinchGripperConfig(config.signals(), opcua_node_ids));

  intrinsic_proto::gpio::OpcuaGpioServiceConfig opcua_gpio_config;
  opcua_gpio_config.set_opcua_server_address(config.opcua_server_address());
  for (const std::string& opcua_node_id : opcua_node_ids) {
    opcua_gpio_config.mutable_opcua_nodes()->add_node_id(opcua_node_id);
  }
  if (!gripper_config.mutable_gpio_service()->mutable_config()->PackFrom(
          opcua_gpio_config)) {
    return absl::InternalError("Failed to pack opcua gpio config.");
  }

  gripper_config.mutable_pinch()->set_is_default_closed(
      config.is_default_closed());

  return gripper_config;
}

}  // namespace gripper
}  // namespace intrinsic
