# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from absl.testing import absltest
from absl.testing import parameterized

from intrinsic.hardware.gpio import gpio_service_equipment_pb2
from intrinsic.icon.equipment import equipment_utils
from intrinsic.icon.equipment import icon_equipment_pb2
from intrinsic.icon.python.testing import icon_fake
from intrinsic.resources.proto import resource_handle_pb2


class MakeIconResourceSelectorTest(absltest.TestCase):

  def test_no_parts_only_requests_connection(self):
    selector = equipment_utils.make_icon_resource_selector()
    self.assertSequenceEqual(
        selector.capability_names, ["intrinsic_proto.icon.v1.IconApi"]
    )

  def test_position_part_only(self):
    selector = equipment_utils.make_icon_resource_selector(
        with_position_controlled_part=True
    )
    self.assertSequenceEqual(
        selector.capability_names,
        ["intrinsic_proto.icon.v1.IconApi", "Icon2PositionPart"],
    )

  def test_gripper_part_only(self):
    selector = equipment_utils.make_icon_resource_selector(
        with_gripper_part=True
    )
    self.assertSequenceEqual(
        selector.capability_names,
        ["intrinsic_proto.icon.v1.IconApi", "Icon2GripperPart"],
    )

  def test_adio_part_only(self):
    selector = equipment_utils.make_icon_resource_selector(with_adio_part=True)
    self.assertSequenceEqual(
        selector.capability_names,
        ["intrinsic_proto.icon.v1.IconApi", "Icon2AdioPart"],
    )

  def test_observation_stream_only(self):
    selector = equipment_utils.make_icon_resource_selector(
        with_observation_stream=True
    )
    self.assertSequenceEqual(
        selector.capability_names,
        ["intrinsic_proto.icon.v1.IconApi", "IconRobotObservationStreamParams"],
    )

  def test_force_torque_sensor_part_only(self):
    selector = equipment_utils.make_icon_resource_selector(
        with_force_torque_sensor_part=True
    )
    self.assertSequenceEqual(
        selector.capability_names,
        ["intrinsic_proto.icon.v1.IconApi", "Icon2ForceTorqueSensorPart"],
    )

  def test_rangefinder_part_only(self):
    selector = equipment_utils.make_icon_resource_selector(
        with_rangefinder_part=True
    )
    self.assertSequenceEqual(
        selector.capability_names,
        ["intrinsic_proto.icon.v1.IconApi", "Icon2RangefinderPart"],
    )


class InitIconClientTest(absltest.TestCase):

  def test_init_icon_client_with_no_information(self):
    f = icon_fake.IconFake()
    f.start()

    with self.assertRaises(KeyError):
      equipment_utils.init_icon_client(resource_handle_pb2.ResourceHandle())

  def test_init_icon_client_only_connection_config_raises_key_error(self):
    f = icon_fake.IconFake()
    f.start()

    handle = resource_handle_pb2.ResourceHandle()
    handle.resource_data.get_or_create("intrinsic_proto.icon.v1.IconApi")

    with self.assertRaises(KeyError):
      equipment_utils.init_icon_client(handle)

  def test_init_icon_client_with_connection_info(self):
    f = icon_fake.IconFake()
    port = f.start()

    handle = resource_handle_pb2.ResourceHandle(
        connection_info=resource_handle_pb2.ResourceConnectionInfo(
            grpc=resource_handle_pb2.ResourceGrpcConnectionInfo(
                address=f"localhost:{port}"
            )
        )
    )
    handle.resource_data.get_or_create("intrinsic_proto.icon.v1.IconApi")

    equipment_utils.init_icon_client(handle)

  def test_init_icon_client_with_data_and_connection_info(self):
    f = icon_fake.IconFake()
    port = f.start()

    handle = resource_handle_pb2.ResourceHandle(
        connection_info=resource_handle_pb2.ResourceConnectionInfo(
            grpc=resource_handle_pb2.ResourceGrpcConnectionInfo(
                address=f"localhost:{port}"
            )
        )
    )
    handle.resource_data.get_or_create("intrinsic_proto.icon.v1.IconApi")

    equipment_utils.init_icon_client(handle)


class EquipmentUtilsTest(parameterized.TestCase):

  @parameterized.named_parameters(
      ("get_position_part_name", equipment_utils.get_position_part_name),
      (
          "get_force_torque_sensor_part_name",
          equipment_utils.get_force_torque_sensor_part_name,
      ),
      ("get_gripper_part_name", equipment_utils.get_gripper_part_name),
      ("get_adio_part_name", equipment_utils.get_adio_part_name),
      ("get_rangefinder_part_name", equipment_utils.get_rangefinder_part_name),
  )
  def test_get_part_name_raises_keyerror_if_part_is_missing(self, getter_func):
    handle = resource_handle_pb2.ResourceHandle(
        connection_info=resource_handle_pb2.ResourceConnectionInfo()
    )

    with self.assertRaises(KeyError):
      getter_func(handle)

  @parameterized.named_parameters(
      (
          "get_position_part_name",
          equipment_utils.get_position_part_name,
          "Icon2PositionPart",
          icon_equipment_pb2.Icon2PositionPart(part_name="part_name"),
      ),
      (
          "get_gripper_part_name",
          equipment_utils.get_gripper_part_name,
          "Icon2GripperPart",
          icon_equipment_pb2.Icon2GripperPart(part_name="part_name"),
      ),
      (
          "get_force_torque_sensor_part_name",
          equipment_utils.get_force_torque_sensor_part_name,
          "Icon2ForceTorqueSensorPart",
          icon_equipment_pb2.Icon2ForceTorqueSensorPart(part_name="part_name"),
      ),
      (
          "get_adio_part_name",
          equipment_utils.get_adio_part_name,
          "Icon2AdioPart",
          icon_equipment_pb2.Icon2AdioPart(
              icon_target=icon_equipment_pb2.Icon2AdioPart.IconTarget(
                  part_name="part_name"
              )
          ),
      ),
      (
          "get_rangefinder_part_name",
          equipment_utils.get_rangefinder_part_name,
          "Icon2RangefinderPart",
          icon_equipment_pb2.Icon2RangefinderPart(part_name="part_name"),
      ),
  )
  def test_get_part_name_returns_correct_part_name(
      self, getter_func, part_type_name, equipment_contents
  ):
    part_name = "part_name"
    handle = resource_handle_pb2.ResourceHandle(
        connection_info=resource_handle_pb2.ResourceConnectionInfo()
    )

    handle.resource_data[part_type_name].contents.Pack(equipment_contents)

    self.assertEqual(getter_func(handle), part_name)

  def test_get_adio_part_name_raises_value_error(self):
    handle = resource_handle_pb2.ResourceHandle(
        connection_info=resource_handle_pb2.ResourceConnectionInfo()
    )

    handle.resource_data["Icon2AdioPart"].contents.Pack(
        icon_equipment_pb2.Icon2AdioPart(
            gpio_service_target=gpio_service_equipment_pb2.GPIOServiceTarget(
                gpio_service_grpc_target="bar"
            )
        )
    )

    with self.assertRaises(ValueError):
      equipment_utils.get_adio_part_name(handle)


if __name__ == "__main__":
  absltest.main()
