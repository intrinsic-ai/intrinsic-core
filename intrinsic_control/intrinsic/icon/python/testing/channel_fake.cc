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

#include "intrinsic/icon/cc_client/testing/channel_fake.h"

#include <pybind11/pybind11.h>

#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/assets/proto/status_spec.pb.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_session.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/extended_status_constants.h"
#include "intrinsic/icon/server/testing/in_process_server.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_specs.h"
#include "pybind11_abseil/absl_casters.h"    // IWYU pragma: keep
#include "pybind11_abseil/status_casters.h"  // IWYU pragma: keep
#include "pybind11_protobuf/native_proto_caster.h"

namespace intrinsic {
namespace icon {

void InitExtendedStatusSpecs() {
  if (!IsExtendedStatusSpecsInitialized()) {
    ASSIGN_OR_DIE(auto specs,
                  ParseTextProto<intrinsic_proto::assets::StatusSpecs>(
                      intrinsic::icon::kStatusSpecs));
    CHECK_OK(intrinsic::InitExtendedStatusSpecs(
        intrinsic::icon::kExtendedStatusComponent, specs.status_info()));
  }
}

PYBIND11_MODULE(channel_fake, m) {
  pybind11::google::ImportStatusModule();
  pybind11_protobuf::ImportNativeProtoCasters();

  pybind11::class_<InProcessApplicationLayerServer::Options>(m, "Options")
      .def(pybind11::init<bool, int>());

  pybind11::enum_<RtclControllerFakeSession::ValidationMode>(m,
                                                             "ValidationMode")
      .value("NICE", RtclControllerFakeSession::ValidationMode::kNice)
      .value("STRICT", RtclControllerFakeSession::ValidationMode::kStrict)
      .export_values();

  pybind11::class_<ChannelInterface, std::shared_ptr<ChannelInterface>>
      channel_interface(m, "ChannelInterface");

  pybind11::class_<RtclControllerChannelFake,
                   std::shared_ptr<RtclControllerChannelFake>>(
      m, "RtclControllerChannelFake")
      .def("GetPartProperties",
           [](const RtclControllerChannelFake& self) {
             return self.GetPartPropertiesTestOnly().properties;
           })
      .def("SetPartProperties",
           [](RtclControllerChannelFake& self,
              absl::flat_hash_map<std::string,
                                  const absl::flat_hash_map<
                                      std::string, std::variant<bool, double>>>&
                  property_map) {
             return self.SetPartPropertiesTestOnly(
                 {.properties = {property_map.begin(), property_map.end()}});
           })
      .def(
          "SetRobotStatus",
          [](RtclControllerChannelFake& self,
             const intrinsic_proto::icon::v1::GetStatusResponse& robot_status) {
            return self.SetRobotStatus(robot_status);
          });

  pybind11::class_<RtclControllerChannelFake::Builder>(m, "Builder")
      .def(pybind11::init(
               [](InProcessApplicationLayerServer::Options options,
                  RtclControllerFakeSession::ValidationMode validation_mode) {
                 InitExtendedStatusSpecs();
                 return RtclControllerChannelFake::Builder(
                     validation_mode,
                     /*control_frequency_hz=*/1000, options);
               }),
           pybind11::arg("options"),
           pybind11::arg("validation_mode") =
               RtclControllerFakeSession::ValidationMode::kNice)
      .def("OnNextSession", &RtclControllerChannelFake::Builder::OnNextSession,
           pybind11::arg("session_behaviors"))
      .def("WithInitialRobotStatus",
           &RtclControllerChannelFake::Builder::WithInitialRobotStatus,
           pybind11::arg("robot_status"))
      .def("Build",
           // Need a lambda here to convert
           // StatusOr<shared_ptr<RtclControllerChannelFake>> to
           // StatusOr<shared_ptr<ChannelInterface>>.
           [](RtclControllerChannelFake::Builder& builder)
               -> absl::StatusOr<std::shared_ptr<RtclControllerChannelFake>> {
             return builder.Build(DefaultPartConfigs());
           });

  pybind11::class_<ActionWill>(m, "ActionWill")
      .def(pybind11::init<>())
      .def("ReportStateVariablesInOrder",
           &ActionWill::ReportStateVariablesInOrder,
           pybind11::arg("state_variable_map_sequence"))
      .def("ReportStateVariables", &ActionWill::ReportStateVariables,
           pybind11::arg("state_variable_map"))
      .def("SetRobotStatus", &ActionWill::SetRobotStatus,
           pybind11::arg("robot_status"))
      .def(
          "ExpectParameter",
          [](ActionWill* self,
             std::function<absl::Status(
                 const google::protobuf::Any& fixed_parameters)>
                 parameter_matcher) {
            return self->ExpectParameter(parameter_matcher);
          },
          pybind11::arg("parameter_matcher"))
      .def("OnStart", &ActionWill::OnStart, pybind11::arg("on_start"));

  pybind11::class_<SessionWill>(m, "SessionWill")
      .def(pybind11::init<>())
      .def("OnAction", &SessionWill::OnAction, pybind11::arg("action_id"),
           pybind11::arg("action_behaviors"));

  pybind11::class_<ActionInstanceId>(m, "ActionInstanceId")
      .def(pybind11::init<int>());

  m.attr("ARM_PART_NAME") = test_part_names::kArmName;
  m.attr("GRIPPER_PART_NAME") = test_part_names::kGripperName;
  m.attr("LINEAR_GRIPPER_PART_NAME") = test_part_names::kLinearGripperName;
  m.attr("FORCE_TORQUE_SENSOR_PART_NAME") =
      test_part_names::kForceTorqueSensorName;
  m.attr("ARM_FORCE_TORQUE_SENSOR_PART_NAME") =
      test_part_names::kArmForceTorqueSensorName;
  m.attr("ADIO_PART_NAME") = test_part_names::kADIOName;
}

}  // namespace icon
}  // namespace intrinsic
