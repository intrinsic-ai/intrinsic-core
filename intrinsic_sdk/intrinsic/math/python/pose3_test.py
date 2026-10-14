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
import numpy as np

from intrinsic.math.python import math_test
from intrinsic.math.python import pose3
from intrinsic.math.python import quaternion
from intrinsic.math.python import rotation3
from intrinsic.math.python import vector_util

_TEST_NAMED_QUATERNIONS = math_test.make_named_unit_quaternions()
_TEST_NAMED_VECTORS = math_test.make_named_vectors()
_TEST_NAMED_UNIT_VECTORS = math_test.make_named_unit_vectors()
_TEST_NAMED_POSE_INPUTS = math_test.make_named_pose_inputs()
_TEST_NAMED_POSES = math_test.make_named_poses()
_TEST_NAMED_TINY_POSES = math_test.make_named_tiny_poses()


class Pose3Test(parameterized.TestCase, math_test.TestCase):

  @parameterized.named_parameters(*_TEST_NAMED_POSE_INPUTS)
  def test_init(self, rotation, translation):
    pose = pose3.Pose3(rotation=rotation, translation=translation)
    self.assertEqual(pose.rotation, rotation)
    self.assert_all_equal(pose.translation, translation)

  @parameterized.named_parameters(*_TEST_NAMED_QUATERNIONS)
  def test_rotation(self, quat):
    rotation = rotation3.Rotation3(quat)
    pose = pose3.Pose3(rotation=rotation)
    self.assertEqual(pose.rotation, rotation3.Rotation3(quat))
    self.assertEqual(pose.rotation, rotation3.Rotation3(-quat))
    self.assertEqual(pose.quaternion, quat)
    self.assertNotEqual(pose.quaternion, -quat)
    for name, vector in _TEST_NAMED_VECTORS:
      with self.subTest(name=name, vector=vector):
        self.assert_all_close(
            pose.transform_point(vector), rotation.rotate_point(vector)
        )

  @parameterized.named_parameters(*_TEST_NAMED_VECTORS)
  def test_translation(self, translation):
    pose = pose3.Pose3(translation=translation)
    self.assert_all_equal(pose.translation, translation)
    for name, vector in _TEST_NAMED_VECTORS:
      with self.subTest(name=name, vector=vector):
        self.assert_all_close(
            pose.transform_point(vector), vector + translation
        )

  @parameterized.named_parameters(*_TEST_NAMED_VECTORS)
  def test_translation_property(self, translation):
    initial_translation = translation.copy()
    pose = pose3.Pose3(translation=translation)
    pose_copy = pose3.Pose3(translation=initial_translation)
    self.assertEqual(pose, pose_copy)
    self.assert_all_equal(pose.translation, initial_translation)
    self.assert_all_equal(pose_copy.translation, initial_translation)
    pose.translation[0] = 123456
    pose_copy.translation[1] = 123456
    self.assertEqual(pose, pose_copy)
    self.assert_all_equal(pose.translation, initial_translation)
    self.assert_all_equal(pose_copy.translation, initial_translation)
    initial_translation[2] = 123456
    self.assertEqual(pose, pose_copy)

  @parameterized.named_parameters(*_TEST_NAMED_POSE_INPUTS)
  def test_transform_point(self, rotation, translation):
    pose = pose3.Pose3(rotation, translation)
    self.assertEqual(pose.rotation, rotation)
    self.assert_all_equal(pose.translation, translation)
    for name, vector in _TEST_NAMED_VECTORS:
      with self.subTest(name=name, vector=vector):
        self.assert_all_close(
            pose.transform_point(vector),
            rotation.rotate_point(vector) + translation,
        )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_inverse(self, pose):
    pose_identity = pose3.Pose3()
    pose_inverse = pose.inverse()
    self.assert_pose_close(pose_identity, pose * pose_inverse)
    self.assert_pose_close(pose_identity, pose_inverse * pose)
    for name, vector in _TEST_NAMED_VECTORS:
      with self.subTest(name=name, vector=vector):
        self.assert_all_close(
            pose_inverse.transform_point(pose.transform_point(vector)), vector
        )
        self.assert_all_close(
            pose.transform_point(pose_inverse.transform_point(vector)), vector
        )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_multiply(self, pose1):
    for pose2_name, pose2 in _TEST_NAMED_POSES:
      with self.subTest(pose2=pose2_name):
        for name, vector in _TEST_NAMED_VECTORS:
          self.assert_all_close(
              pose1.transform_point(pose2.transform_point(vector)),
              (pose1 * pose2).transform_point(vector),
              err_msg='%s: %s' % (name, vector),
          )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_multiply_by_inverse(self, pose1):
    for pose2_name, pose2 in _TEST_NAMED_POSES:
      with self.subTest(pose2=pose2_name):
        pose1_inv_pose2 = pose1.multiply_by_inverse(pose2)
        self.assert_pose_close(pose1.inverse() * pose2, pose1_inv_pose2)
        for name, vector in _TEST_NAMED_VECTORS:
          self.assert_all_close(
              pose1.inverse().transform_point(pose2.transform_point(vector)),
              pose1_inv_pose2.transform_point(vector),
              err_msg='%s: %s' % (name, vector),
          )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_assert_pose_close(self, pose):
    self.assert_pose_close(pose, pose)
    for pose_epsilon_name, pose_epsilon in _TEST_NAMED_TINY_POSES:
      with self.subTest(pose_epsilon=pose_epsilon_name):
        self.assert_pose_close(pose, pose * pose_epsilon)
        self.assert_pose_close(pose, pose_epsilon * pose)
        for name, test_vector in _TEST_NAMED_UNIT_VECTORS:
          self.assert_all_close(
              pose.transform_point(test_vector),
              (pose * pose_epsilon).transform_point(test_vector),
              err_msg='%s: %s' % (name, test_vector),
          )
          self.assert_all_close(
              pose.transform_point(test_vector),
              (pose_epsilon * pose).transform_point(test_vector),
              err_msg='%s: %s' % (name, test_vector),
          )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_assert_pose_not_close(self, pose):
    rotation_i = rotation3.Rotation3(
        quat=quaternion.Quaternion(xyzw=vector_util.one_hot_vector(4, 0))
    )
    translation_z = np.array([0, 0, 1])
    for pose_delta in [
        pose3.Pose3(rotation=rotation_i),
        pose3.Pose3(translation=translation_z),
        pose3.Pose3(rotation=rotation_i, translation=translation_z),
    ]:
      with self.subTest(pose_delta=pose_delta):
        self.assertRaisesRegex(
            AssertionError,
            math_test.POSE_NOT_CLOSE_MESSAGE,
            self.assert_pose_close,
            pose,
            pose * pose_delta,
        )
        self.assertRaisesRegex(
            AssertionError,
            math_test.POSE_NOT_CLOSE_MESSAGE,
            self.assert_pose_close,
            pose,
            pose_delta * pose,
        )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_matrix4x4(self, pose):
    pose_matrix = pose.matrix4x4()
    self.assert_pose_close(pose, pose3.Pose3.from_matrix4x4(pose_matrix))
    for name, vector in _TEST_NAMED_VECTORS:
      with self.subTest(name=name, vector=vector):
        posed_vector = pose.transform_point(vector)
        vector_h = np.hstack((vector, [1.0]))
        transformed_vector = np.matmul(pose_matrix, vector_h)[:3]
        self.assert_all_close(posed_vector, transformed_vector)

  def test_from_matrix4x4_identity(self):
    self.assert_pose_close(
        pose3.Pose3.from_matrix4x4(np.identity(4)), pose3.Pose3.identity()
    )

  def test_from_matrix4x4_reflection(self):
    reflection = np.diag([-1.0, 1.0, 1.0, 1.0])
    reflection[:3, 3] = [1.0, 2.0, 3.0]
    with self.assertRaisesRegex(ValueError, 'caller context'):
      pose3.Pose3.from_matrix4x4(reflection, err_msg='caller context')
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_NOT_PROPER_ROTATION_MESSAGE,
        pose3.Pose3.from_matrix4x4,
        reflection,
    )

  def test_from_matrix4x4_errors(self):
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        np.identity(3),
    )
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        np.identity(5),
    )
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        np.zeros((3, 4)),
    )
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        np.zeros((4, 3)),
    )
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        np.zeros((4, 4, 4)),
    )
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        np.zeros((4)),
    )
    m4x4 = np.identity(4)
    m4x4[3, 3] = 2
    self.assertRaisesRegex(
        ValueError,
        pose3.HOMOGENEOUS_MATRIX_FORM,
        pose3.Pose3.from_matrix4x4,
        m4x4,
    )
    m4x4 = np.identity(4)
    m4x4[1, 2] = 2
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_NOT_ORTHOGONAL_MESSAGE,
        pose3.Pose3.from_matrix4x4,
        m4x4,
    )

  def check_equal_poses(self, pose1, pose2):
    self.check_eq_and_ne_for_equal_values(pose1, pose2)
    # The definition of equal poses is that all points are transformed
    # identically.
    for name, point in _TEST_NAMED_UNIT_VECTORS:
      with self.subTest(name=name, point=point):
        self.assert_all_equal(
            pose1.transform_point(point), pose2.transform_point(point)
        )

  @parameterized.named_parameters(*_TEST_NAMED_POSES)
  def test_eq(self, pose):
    self.check_equal_poses(pose, pose)
    self.check_equal_poses(pose, pose3.Pose3(pose.rotation, pose.translation))
    self.check_equal_poses(
        pose,
        pose3.Pose3(rotation3.Rotation3(-pose.quaternion), pose.translation),
    )

  def test_eq_other_type(self):
    self.assertNotEqual(pose3.Pose3.identity(), 'string')

  def check_unequal_poses(self, pose1, pose2):
    self.check_eq_and_ne_for_unequal_values(pose1, pose2)
    # At least one point should have a different result from the two poses.
    all_equal = True
    for _, point in _TEST_NAMED_UNIT_VECTORS:
      if not np.all(
          pose1.transform_point(point) == pose2.transform_point(point)
      ):
        all_equal = False
    self.assertFalse(all_equal, '%r != %r' % (pose1, pose2))

  @parameterized.parameters(
      ((1, 2, 3, 0, 4, 0, 0),),
      ((-1, 0, 1, 1, -1, 1, -1),),
      ((0, 0, -1, 1, 2, 3, 4),),
  )
  def test_to_and_from_vec7(self, vec7):
    pose = pose3.Pose3.from_vec7(vec7, normalize=False)
    pose.rotation.check_valid()
    self.assert_all_equal(pose.translation, vec7[:3])
    self.assert_all_equal(pose.quaternion.xyzw, vec7[3:])
    self.assert_all_equal(pose.vec7, vec7)
    self.assert_all_equal(pose.vec7, pose3.Pose3.from_vec7(pose.vec7).vec7)

  @parameterized.parameters(
      ((1, 2, 3, 0, 4, 0, 0),),
      ((-1, 0, 1, 1, -1, 1, -1),),
      ((0, 0, -1, 1, 2, 3, 4),),
  )
  def test_to_and_from_vec7_normalized(self, vec7):
    pose_normalized = pose3.Pose3.from_vec7(vec7, normalize=True)
    pose_normalized.rotation.check_valid()
    self.assert_all_equal(pose_normalized.translation, vec7[:3])
    self.assert_all_close(
        pose_normalized.quaternion.xyzw, vector_util.normalize_vector(vec7[3:])
    )
    self.assert_all_equal(
        pose_normalized.vec7, pose3.Pose3.from_vec7(pose_normalized.vec7).vec7
    )

  def test_from_vec7_wrong_components(self):
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_COMPONENTS_MESSAGE,
        pose3.Pose3.from_vec7,
        np.arange(6),
    )
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_COMPONENTS_MESSAGE,
        pose3.Pose3.from_vec7,
        np.arange(8),
    )

  def test_from_vec7_zero_quaternion(self):
    self.assertRaisesRegex(
        ValueError,
        '%s.*%s'
        % (
            quaternion.QUATERNION_ZERO_MESSAGE,
            rotation3.ROTATION3_INIT_MESSAGE,
        ),
        pose3.Pose3.from_vec7,
        np.ones(7) * 1e-20,
        True,
    )

  @parameterized.parameters(
      ((1, 2, 3, 0, 4, 0, 0),),
      ((-1, 0, 1, 1, -1, 1, -1),),
      ((0, 0, -1, 1, 2, 3, 4),),
  )
  def test_normalized_vs_not(self, vec7):
    pose_unnormalized = pose3.Pose3.from_vec7(vec7, normalize=False)
    pose_normalized = pose3.Pose3.from_vec7(vec7, normalize=True)
    for _, v in _TEST_NAMED_VECTORS:
      self.assert_all_close(
          pose_unnormalized.transform_point(v),
          pose_normalized.transform_point(v),
      )


if __name__ == '__main__':
  np.random.seed(0)
  absltest.main()
