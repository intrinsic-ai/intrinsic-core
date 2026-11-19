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

"""Tests for clear_motion_planner_service_cache skill."""

from typing import cast
from unittest import mock

from absl import logging
from absl.testing import absltest
from python.runfiles import runfiles

from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.motion_planning import motion_planner_client
from intrinsic.motion_planning.skills import clear_motion_planner_service_cache as clear_mps_cache
from intrinsic.motion_planning.skills import clear_motion_planner_service_cache_pb2 as clear_mps_cache_pb2
from intrinsic.skills.internal import preview_context_impl
from intrinsic.skills.testing import skill_test_utils


class ClearMotionPlannerServiceCacheParamsTest(absltest.TestCase):

  @classmethod
  def setUpClass(cls):
    super().setUpClass()

  def setUp(self):
    super().setUp()

    self._skill = clear_mps_cache.ClearMotionPlannerServiceCache()
    self._params = clear_mps_cache_pb2.ClearMotionPlannerServiceCacheParams()

    self._motion_planner_service_stub = mock.MagicMock()
    self._motion_planner = motion_planner_client.MotionPlannerClient(
        world_id='world',
        stub=self._motion_planner_service_stub,
    )

  def test_manifest_contains_expected_values(self):
    manifest_path = runfiles.Create().Rlocation(
        'intrinsic-core/intrinsic_motion_planning/intrinsic/motion_planning/skills/clear_motion_planner_service_cache_manifest.pbbin'
    )
    manifest = skill_test_utils.get_skill_manifest(manifest_path)
    self.assertEqual(manifest.id.name, 'clear_motion_planner_service_cache')

  def test_execute_works_fallback(self):
    request = skill_test_utils.make_test_execute_request(params=self._params)
    execute_context = skill_test_utils.make_test_execute_context(
        motion_planner=self._motion_planner,
    )
    with self.assertLogs(level='INFO') as l:
      self._skill.execute(request, execute_context)
    self.assertTrue(
        any(
            'Using Motion Planner Client from context.' in line
            for line in l.output
        )
    )
    self.assertTrue(
        any('Clearing all MPS caches.' in line for line in l.output)
    )
    self._motion_planner_service_stub.ClearCache.assert_called_once()

  @mock.patch.object(
      motion_planner_client, 'get_motion_planner_service_asset_client'
  )
  def test_execute_with_motion_planner_service_asset(
      self, mock_get_motion_planner_service_asset_client
  ):
    mock_asset_client = mock.MagicMock()
    mock_get_motion_planner_service_asset_client.return_value = (
        mock_asset_client
    )

    motion_planner_service = resolved_dependency_pb2.ResolvedDependency()
    params = clear_mps_cache_pb2.ClearMotionPlannerServiceCacheParams(
        motion_planner_service=motion_planner_service
    )

    with self.assertLogs(level='INFO') as l:
      self._skill.execute(
          request=skill_test_utils.make_test_execute_request(params=params),
          context=skill_test_utils.make_test_execute_context(),
      )

    self.assertTrue(
        any(
            'Using Motion Planner Client from asset definition.' in line
            for line in l.output
        )
    )
    self.assertTrue(
        any('Clearing all MPS caches.' in line for line in l.output)
    )
    mock_get_motion_planner_service_asset_client.assert_called_once_with(
        'world',  # Default _TEST_WORLD_ID in skill_test_utils
        motion_planner_service,
    )
    mock_asset_client.clear_cache.assert_called_once()

  @mock.patch.object(
      motion_planner_client, 'get_motion_planner_service_asset_client'
  )
  def test_preview_with_node_in_bounding_box(
      self, mock_get_motion_planner_service_asset_client
  ):
    mock_asset_client = mock.MagicMock()
    mock_get_motion_planner_service_asset_client.return_value = (
        mock_asset_client
    )

    motion_planner_service = resolved_dependency_pb2.ResolvedDependency()
    params = clear_mps_cache_pb2.ClearMotionPlannerServiceCacheParams(
        motion_planner_service=motion_planner_service
    )

    # Preview is implemented with preview_via_execute.
    request = skill_test_utils.make_test_preview_request(params=params)
    context = cast(
        preview_context_impl.PreviewContextImpl,
        skill_test_utils.make_test_preview_context(
            motion_planner=self._motion_planner
        ),
    )
    self._skill.preview(request, context)
    mock_get_motion_planner_service_asset_client.assert_called_once_with(
        'world',  # Default _TEST_WORLD_ID in skill_test_utils
        motion_planner_service,
    )
    mock_asset_client.clear_cache.assert_called_once()

  def test_footprint_locks_the_universe(self):
    request = skill_test_utils.make_test_get_footprint_request(
        params=self._params,
    )
    footprint_context = skill_test_utils.make_test_get_footprint_context()

    result = self._skill.get_footprint(request, footprint_context)

    self.assertTrue(result.lock_the_universe)


if __name__ == '__main__':
  absltest.main()
