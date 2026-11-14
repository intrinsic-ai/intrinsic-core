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
from intrinsic.icon.equipment import resolved_dependency_utils
from intrinsic.icon.proto.v1 import service_pb2
from intrinsic.icon.proto.v1 import types_pb2
from intrinsic.icon.python import errors
from intrinsic.icon.python import icon_api
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


if __name__ == "__main__":
  absltest.main()
