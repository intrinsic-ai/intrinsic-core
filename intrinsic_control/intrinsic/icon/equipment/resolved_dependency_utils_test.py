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

from unittest import mock

from absl.testing import absltest
import grpc

from intrinsic.assets.proto.v1 import grpc_connection_pb2
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.icon.control.parts.hal.force_torque_sensor_part import hal_force_torque_sensor_part_config_pb2
from intrinsic.icon.equipment import force_control_settings_pb2
from intrinsic.icon.equipment import resolved_dependency_utils
from intrinsic.icon.proto.v1 import service_pb2
from intrinsic.icon.proto.v1 import types_pb2
from intrinsic.icon.python import errors
from intrinsic.icon.python import icon_api
from intrinsic.icon.python.testing import channel_fake
from intrinsic.icon.python.testing import icon_fake
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources


def _make_icon_resolved_dependency(
    address: str = "localhost:1234",
) -> resolved_dependency_pb2.ResolvedDependency:
  return resolved_dependency_pb2.ResolvedDependency(
      interfaces={
          "grpc://intrinsic_proto.icon.v1.IconApi": resolved_dependency_pb2.ResolvedDependency.Interface(
              grpc=resolved_dependency_pb2.ResolvedDependency.Interface.Grpc(
                  connection=grpc_connection_pb2.GrpcConnection(address=address)
              )
          )
      }
  )


def _pack_force_torque_sensor_config(
    part_config: types_pb2.PartConfig, excessive_force_threshold: float
) -> types_pb2.PartConfig:
  """Packs a HalForceTorqueSensorPartConfig into `part_config.config`."""
  part_config.config.Pack(
      hal_force_torque_sensor_part_config_pb2.HalForceTorqueSensorPartConfig(
          force_control_settings=force_control_settings_pb2.ForceControlSettings(
              excessive_force_threshold=excessive_force_threshold
          )
      )
  )
  return part_config


def _make_force_torque_sensor_part_config(
    name: str, excessive_force_threshold: float
) -> types_pb2.PartConfig:
  return _pack_force_torque_sensor_config(
      types_pb2.PartConfig(
          name=name,
          part_type_name=resolved_dependency_utils.HAL_FORCE_TORQUE_SENSOR_PART_TYPE_NAME,
          hardware_resource_name=name,
      ),
      excessive_force_threshold,
  )


class ConnectToIconResolvedDependencyTest(absltest.TestCase):

  def test_connect_to_icon_resolved_dependency_with_empty_resolved_dependency_raises_key_error(
      self,
  ):
    with self.assertRaises(KeyError):
      resolved_dependency_utils.connect_to_icon_resolved_dependency(
          resolved_dependency_pb2.ResolvedDependency()
      )

  def test_connect_to_icon_resolved_dependency_with_fake_server(self):
    f = icon_fake.IconFake()
    port = f.start()

    dep = _make_icon_resolved_dependency(f"localhost:{port}")

    icon_resolved_dependency = (
        resolved_dependency_utils.connect_to_icon_resolved_dependency(dep)
    )
    self.assertIsNotNone(icon_resolved_dependency.client.get_config())

  def test_connect_to_icon_resolved_dependency_with_custom_fake_part_configs(
      self,
  ):
    part_configs = list(channel_fake.default_part_configs())
    for part_config in part_configs:
      if part_config.name == channel_fake.FORCE_TORQUE_SENSOR_PART_NAME:
        part_config.part_type_name = (
            resolved_dependency_utils.HAL_FORCE_TORQUE_SENSOR_PART_TYPE_NAME
        )
        _pack_force_torque_sensor_config(
            part_config, excessive_force_threshold=42.0
        )
    f = icon_fake.IconFake()
    port = f.start(part_configs=part_configs)

    icon_resolved_dependency = (
        resolved_dependency_utils.connect_to_icon_resolved_dependency(
            _make_icon_resolved_dependency(f"localhost:{port}")
        )
    )

    self.assertCountEqual(
        icon_resolved_dependency.part_configs.keys(),
        [part_config.name for part_config in part_configs],
    )
    self.assertEqual(
        icon_resolved_dependency.get_force_torque_sensor_part_name(),
        channel_fake.FORCE_TORQUE_SENSOR_PART_NAME,
    )
    self.assertEqual(
        icon_resolved_dependency.get_force_control_settings(
            channel_fake.FORCE_TORQUE_SENSOR_PART_NAME
        ).excessive_force_threshold,
        42.0,
    )

  @mock.patch.object(grpc, "channel_ready_future", autospec=True)
  def test_connect_to_icon_resolved_dependency_raises_server_error_on_timeout(
      self, mock_ready_future
  ):
    mock_ready_future.return_value.result.side_effect = (
        grpc.FutureTimeoutError()
    )
    dep = _make_icon_resolved_dependency()

    with self.assertRaises(errors.Client.ServerError):
      resolved_dependency_utils.connect_to_icon_resolved_dependency(
          dep, connect_timeout=0.5
      )
    mock_ready_future.return_value.result.assert_called_once_with(timeout=0.5)

  @mock.patch.object(grpc, "channel_ready_future", autospec=True)
  @mock.patch.object(icon_api, "Client", autospec=True)
  def test_connect_to_icon_resolved_dependency_populates_parts(
      self, mock_client_cls, mock_ready_future
  ):
    mock_client = mock_client_cls.return_value
    mock_client.get_config.return_value = service_pb2.GetConfigResponse(
        part_configs=[
            types_pb2.PartConfig(
                name="robot_arm_2",
                part_type_name="HalArmPart",
                hardware_resource_name="robot_arm_2",
            ),
            types_pb2.PartConfig(
                name="robot_arm_1",
                part_type_name="HalArmPart",
                hardware_resource_name="robot_arm_1",
            ),
            types_pb2.PartConfig(
                name="gripper_1",
                part_type_name="HalLinearGripperPart",
                hardware_resource_name="gripper_1",
            ),
            types_pb2.PartConfig(
                name="adio_1",
                part_type_name="HalADIOPart",
                hardware_resource_name="adio_1",
            ),
            types_pb2.PartConfig(
                name="ft_sensor_1",
                part_type_name="HalForceTorqueSensorPart",
                hardware_resource_name="ft_sensor_1",
            ),
            types_pb2.PartConfig(
                name="rangefinder_1",
                part_type_name="HalRangefinderPart",
                hardware_resource_name="rangefinder_1",
            ),
            types_pb2.PartConfig(
                name="custom_part_1",
                part_type_name="SomeOtherPart",
                hardware_resource_name="custom_part_1",
            ),
        ]
    )

    dep = _make_icon_resolved_dependency()
    icon_resolved_dependency = (
        resolved_dependency_utils.connect_to_icon_resolved_dependency(dep)
    )

    mock_ready_future.return_value.result.assert_called_once_with(timeout=5)
    self.assertEqual(icon_resolved_dependency.client, mock_client)
    self.assertEqual(
        icon_resolved_dependency.parts,
        {
            "HalArmPart": {
                "robot_arm_1": "robot_arm_1",
                "robot_arm_2": "robot_arm_2",
            },
            "HalLinearGripperPart": {"gripper_1": "gripper_1"},
            "HalADIOPart": {"adio_1": "adio_1"},
            "HalForceTorqueSensorPart": {"ft_sensor_1": "ft_sensor_1"},
            "HalRangefinderPart": {"rangefinder_1": "rangefinder_1"},
            "SomeOtherPart": {"custom_part_1": "custom_part_1"},
        },
    )
    self.assertEqual(
        icon_resolved_dependency.part_configs,
        {
            part_config.name: part_config
            for part_config in mock_client.get_config.return_value.part_configs
        },
    )

  @mock.patch.object(grpc, "channel_ready_future", autospec=True)
  @mock.patch.object(icon_api, "Client", autospec=True)
  def test_connect_to_icon_resolved_dependency_raises_server_error_on_empty_part_type_name(
      self, mock_client_cls, mock_ready_future
  ):
    del mock_ready_future
    mock_client = mock_client_cls.return_value
    mock_client.get_config.return_value = service_pb2.GetConfigResponse(
        part_configs=[
            types_pb2.PartConfig(
                name="robot_arm_1",
                hardware_resource_name="robot_arm_1",
            ),
        ]
    )

    dep = _make_icon_resolved_dependency()

    with self.assertRaises(errors.Client.ServerError):
      resolved_dependency_utils.connect_to_icon_resolved_dependency(dep)

  @mock.patch.object(grpc, "channel_ready_future", autospec=True)
  @mock.patch.object(icon_api, "Client", autospec=True)
  def test_connect_to_icon_resolved_dependency_raises_server_error_on_empty_hardware_resource_name(
      self, mock_client_cls, mock_ready_future
  ):
    del mock_ready_future
    mock_client = mock_client_cls.return_value
    mock_client.get_config.return_value = service_pb2.GetConfigResponse(
        part_configs=[
            types_pb2.PartConfig(
                name="robot_arm_1",
                part_type_name="HalArmPart",
            ),
        ]
    )

    dep = _make_icon_resolved_dependency()

    with self.assertRaises(errors.Client.ServerError):
      resolved_dependency_utils.connect_to_icon_resolved_dependency(dep)


class IconResolvedDependencyMethodsTest(absltest.TestCase):

  def test_get_position_part_and_kinematic_object_single_part_fallback(self):
    mock_world = mock.create_autospec(
        object_world_client.ObjectWorldClient, instance=True
    )
    mock_kinematic_object = mock.create_autospec(
        object_world_resources.KinematicObject, instance=True
    )
    mock_kinematic_object.name = "my_robot_1"
    mock_world.get_kinematic_object.return_value = mock_kinematic_object

    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={"HalArmPart": {"robot_arm_1": "my_robot_1"}},
    )

    result = icon_resolved_dependency.get_position_part_and_kinematic_object(
        mock_world
    )

    self.assertEqual(result.part_name, "robot_arm_1")
    self.assertEqual(result.kinematic_object, mock_kinematic_object)
    mock_world.get_kinematic_object.assert_called_once_with(
        object_world_ids.WorldObjectName("my_robot_1")
    )

  def test_get_position_part_and_kinematic_object_selects_matching_arm_part(
      self,
  ):
    mock_world = mock.create_autospec(
        object_world_client.ObjectWorldClient, instance=True
    )
    mock_kinematic_object = mock.create_autospec(
        object_world_resources.KinematicObject, instance=True
    )
    mock_kinematic_object.name = "my_robot_2"
    mock_world.get_kinematic_object.return_value = mock_kinematic_object

    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalArmPart": {
                "robot_arm_1": "my_robot_1",
                "robot_arm_2": "my_robot_2",
            }
        },
    )
    arm_part = object_world_refs_pb2.ObjectReference(
        by_name=object_world_refs_pb2.ObjectReferenceByName(
            object_name="my_robot_2"
        )
    )

    result = icon_resolved_dependency.get_position_part_and_kinematic_object(
        mock_world, arm_part
    )

    self.assertEqual(result.part_name, "robot_arm_2")
    self.assertEqual(result.kinematic_object, mock_kinematic_object)
    mock_world.get_kinematic_object.assert_called_once_with(arm_part)

  def test_get_position_part_and_kinematic_object_raises_value_error_when_empty_position_parts(
      self,
  ):
    mock_world = mock.create_autospec(
        object_world_client.ObjectWorldClient, instance=True
    )
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={},
    )

    with self.assertRaisesRegex(
        ValueError, "The given ICON instance does not have any position parts"
    ):
      icon_resolved_dependency.get_position_part_and_kinematic_object(
          mock_world
      )

  def test_get_position_part_and_kinematic_object_raises_value_error_when_multiple_position_parts_and_no_arm_part(
      self,
  ):
    mock_world = mock.create_autospec(
        object_world_client.ObjectWorldClient, instance=True
    )
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalArmPart": {
                "robot_arm_1": "my_robot_1",
                "robot_arm_2": "my_robot_2",
            }
        },
    )

    with self.assertRaisesRegex(
        ValueError,
        r"No arm part object reference was given, and the given ICON instance"
        r" has multiple.*Available \(part_name, object_name\) pairs for the"
        r" selected ICON instance are: \(robot_arm_1, my_robot_1\),"
        r" \(robot_arm_2, my_robot_2\)\.",
    ):
      icon_resolved_dependency.get_position_part_and_kinematic_object(
          mock_world
      )

  def test_get_position_part_and_kinematic_object_raises_value_error_when_arm_part_not_in_position_parts(
      self,
  ):
    mock_world = mock.create_autospec(
        object_world_client.ObjectWorldClient, instance=True
    )
    mock_kinematic_object = mock.create_autospec(
        object_world_resources.KinematicObject, instance=True
    )
    mock_kinematic_object.name = "other_robot"
    mock_world.get_kinematic_object.return_value = mock_kinematic_object

    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalArmPart": {
                "robot_arm_1": "my_robot_1",
                "robot_arm_2": "my_robot_2",
            }
        },
    )
    arm_part = object_world_refs_pb2.ObjectReference(
        by_name=object_world_refs_pb2.ObjectReferenceByName(
            object_name="other_robot"
        )
    )

    with self.assertRaisesRegex(
        ValueError,
        r"Could not find a position part corresponding to object with name"
        r" 'other_robot'\. Please check you have the right object and ICON"
        r" instance selected\. Available \(part_name, object_name\) pairs for"
        r" the selected ICON instance are: \(robot_arm_1, my_robot_1\),"
        r" \(robot_arm_2, my_robot_2\)\.",
    ):
      icon_resolved_dependency.get_position_part_and_kinematic_object(
          mock_world, arm_part
      )

  def test_get_adio_part_names_returns_sorted_names(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalADIOPart": {
                "adio_2": "adio_2",
                "adio_1": "adio_1",
            },
            # Parts of other types must not be returned.
            "HalArmPart": {"robot_arm": "my_robot"},
        },
    )

    self.assertEqual(
        icon_resolved_dependency.get_adio_part_names(), ["adio_1", "adio_2"]
    )

  def test_get_adio_part_names_raises_value_error_when_empty(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={},
    )

    with self.assertRaisesRegex(
        ValueError, "The given ICON instance does not have any ADIO parts"
    ):
      icon_resolved_dependency.get_adio_part_names()

  def test_get_gripper_part_name_returns_part_name(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={"HalLinearGripperPart": {"gripper_1": "my_gripper_1"}},
    )

    result = icon_resolved_dependency.get_gripper_part_name()

    self.assertEqual(result, "gripper_1")

  def test_get_gripper_part_name_raises_value_error_when_empty_gripper_parts(
      self,
  ):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={},
    )

    with self.assertRaisesRegex(
        ValueError, "The given ICON instance does not have any gripper parts"
    ):
      icon_resolved_dependency.get_gripper_part_name()

  def test_get_gripper_part_name_raises_value_error_when_multiple_gripper_parts(
      self,
  ):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalLinearGripperPart": {
                "gripper_1": "my_gripper_1",
                "gripper_2": "my_gripper_2",
            }
        },
    )

    with self.assertRaisesRegex(
        ValueError,
        r"The given ICON instance has multiple gripper parts\. Please specify"
        r" which gripper part to use\. Available gripper parts for the selected"
        r" ICON instance are: gripper_1, gripper_2\.",
    ):
      icon_resolved_dependency.get_gripper_part_name()

  def test_get_adio_part_name_returns_part_name(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={"HalADIOPart": {"adio_1": "my_adio_1"}},
    )

    self.assertEqual(icon_resolved_dependency.get_adio_part_name(), "adio_1")

  def test_get_adio_part_name_raises_value_error_when_empty(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={},
    )

    with self.assertRaisesRegex(
        ValueError, "The given ICON instance does not have any ADIO parts"
    ):
      icon_resolved_dependency.get_adio_part_name()

  def test_get_adio_part_name_raises_value_error_when_multiple(
      self,
  ):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalADIOPart": {
                "adio_1": "my_adio_1",
                "adio_2": "my_adio_2",
            }
        },
    )

    with self.assertRaisesRegex(
        ValueError,
        r"The given ICON instance has multiple ADIO parts\. Please specify"
        r" which ADIO part to use\. Available ADIO parts for the selected ICON"
        r" instance are: adio_1, adio_2\.",
    ):
      icon_resolved_dependency.get_adio_part_name()

  def test_get_force_torque_sensor_part_name_returns_part_name(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={"HalForceTorqueSensorPart": {"ft_sensor_1": "my_ft_sensor_1"}},
    )

    self.assertEqual(
        icon_resolved_dependency.get_force_torque_sensor_part_name(),
        "ft_sensor_1",
    )

  def test_get_force_torque_sensor_part_name_raises_when_empty(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={},
    )

    with self.assertRaisesRegex(
        ValueError,
        "The given ICON instance does not have any force-torque sensor parts",
    ):
      icon_resolved_dependency.get_force_torque_sensor_part_name()

  def test_get_force_torque_sensor_part_name_raises_when_multiple(
      self,
  ):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalForceTorqueSensorPart": {
                "ft_sensor_1": "my_ft_sensor_1",
                "ft_sensor_2": "my_ft_sensor_2",
            }
        },
    )

    with self.assertRaisesRegex(
        ValueError,
        r"The given ICON instance has multiple force-torque sensor parts\."
        r" Please specify which force-torque sensor part to use\. Available"
        r" force-torque sensor parts for the selected ICON instance are:"
        r" ft_sensor_1, ft_sensor_2\.",
    ):
      icon_resolved_dependency.get_force_torque_sensor_part_name()

  def test_get_rangefinder_part_name_returns_part_name(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={"HalRangefinderPart": {"rangefinder_1": "my_rangefinder_1"}},
    )

    self.assertEqual(
        icon_resolved_dependency.get_rangefinder_part_name(), "rangefinder_1"
    )

  def test_get_rangefinder_part_name_raises_when_empty(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={},
    )

    with self.assertRaisesRegex(
        ValueError,
        "The given ICON instance does not have any rangefinder parts",
    ):
      icon_resolved_dependency.get_rangefinder_part_name()

  def test_get_rangefinder_part_name_raises_when_multiple(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        parts={
            "HalRangefinderPart": {
                "rangefinder_1": "my_rangefinder_1",
                "rangefinder_2": "my_rangefinder_2",
            }
        },
    )

    with self.assertRaisesRegex(
        ValueError,
        r"The given ICON instance has multiple rangefinder parts\. Please"
        r" specify which rangefinder part to use\. Available rangefinder parts"
        r" for the selected ICON instance are: rangefinder_1, rangefinder_2\.",
    ):
      icon_resolved_dependency.get_rangefinder_part_name()

  def test_get_force_control_settings_returns_force_control_settings(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        part_configs={
            "ft_sensor_1": _make_force_torque_sensor_part_config(
                "ft_sensor_1", excessive_force_threshold=10.0
            ),
            "ft_sensor_2": _make_force_torque_sensor_part_config(
                "ft_sensor_2", excessive_force_threshold=20.0
            ),
        },
    )

    self.assertEqual(
        icon_resolved_dependency.get_force_control_settings("ft_sensor_2"),
        force_control_settings_pb2.ForceControlSettings(
            excessive_force_threshold=20.0
        ),
    )

  def test_get_force_control_settings_raises_when_part_not_found(self):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        part_configs={},
    )

    with self.assertRaisesRegex(
        ValueError,
        r"Could not find a part with name 'other_part'\. The selected ICON"
        r" instance does not have any force-torque sensor parts\.",
    ):
      icon_resolved_dependency.get_force_control_settings("other_part")

  def test_get_force_control_settings_raises_when_part_not_found_lists_only_compatible_parts(
      self,
  ):
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        part_configs={
            "ft_sensor_2": _make_force_torque_sensor_part_config(
                "ft_sensor_2", excessive_force_threshold=20.0
            ),
            "ft_sensor_1": _make_force_torque_sensor_part_config(
                "ft_sensor_1", excessive_force_threshold=10.0
            ),
            "robot_arm": types_pb2.PartConfig(name="robot_arm"),
        },
    )

    with self.assertRaisesRegex(
        ValueError,
        r"Could not find a part with name 'other_part'\. Please check you have"
        r" the right part name and ICON instance selected\. Available"
        r" force-torque sensor parts for the selected ICON instance are:"
        r" ft_sensor_1, ft_sensor_2\.",
    ):
      icon_resolved_dependency.get_force_control_settings("other_part")

  def test_get_force_control_settings_raises_when_config_is_not_ft_config(
      self,
  ):
    arm_config = types_pb2.PartConfig(name="robot_arm")
    arm_config.config.Pack(force_control_settings_pb2.ForceControlSettings())
    icon_resolved_dependency = resolved_dependency_utils.IconResolvedDependency(
        client=mock.create_autospec(icon_api.Client, instance=True),
        part_configs={"robot_arm": arm_config},
    )

    with self.assertRaisesRegex(
        ValueError,
        r"Part 'robot_arm' does not have a HalForceTorqueSensorPartConfig"
        r" \(config type URL:"
        r" 'type\.googleapis\.com/intrinsic_proto\.icon\.ForceControlSettings'\)\."
        r" Please check that you selected a force-torque sensor part\.",
    ):
      icon_resolved_dependency.get_force_control_settings("robot_arm")


if __name__ == "__main__":
  absltest.main()
