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
from google.protobuf import duration_pb2

from intrinsic.icon.actions import trajectory_tracking_action_pb2
from intrinsic.icon.actions import trajectory_tracking_action_utils
from intrinsic.icon.proto import joint_space_pb2


class TrajectoryTrackingActionUtilsTest(absltest.TestCase):

  def test_create_action(self):
    trajectory = joint_space_pb2.JointTrajectoryPVA(
        state=[
            joint_space_pb2.JointStatePVA(
                position=[0.0, 0.0],
                velocity=[0.0, 0.0],
                acceleration=[0.0, 0.0],
            )
        ],
        time_since_start=[duration_pb2.Duration(seconds=2, nanos=14)],
    )

    action = trajectory_tracking_action_utils.create_trajectory_tracking_action(
        5,
        'my_arm',
        trajectory,
    )

    self.assertEqual(action.proto.action_instance_id, 5)
    self.assertEqual(action.proto.part_name, 'my_arm')
    self.assertEmpty(action.reactions)
    self.assertEqual(
        action.proto.action_type_name, 'intrinsic.trajectory_tracking'
    )

    got_params = (
        trajectory_tracking_action_pb2.TrajectoryTrackingActionFixedParams()
    )
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(got_params.trajectory, trajectory)


if __name__ == '__main__':
  absltest.main()
