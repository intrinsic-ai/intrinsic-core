#!/bin/bash

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

# Tests that sdf_to_scene_object fails when validation is enabled and geometry is invalid.

set -x

SDF_TO_SCENE_OBJECT=$1
INVALID_SDF=$2
EXPECT_FAILURE=$3

OUT_PBTXT="${TEST_TMPDIR:-/tmp}/out.pbtxt"

if [ "$EXPECT_FAILURE" = "true" ]; then
  if $SDF_TO_SCENE_OBJECT --input_sdf_file $INVALID_SDF \
                          --output_scene_object_pbtxt_file "$OUT_PBTXT" \
                          --skip_validate_referenced_geos=false; then
    echo "Expected failure but command succeeded"
    exit 1
  else
    echo "Command failed as expected"
    exit 0
  fi
else
  if $SDF_TO_SCENE_OBJECT --input_sdf_file $INVALID_SDF \
                          --output_scene_object_pbtxt_file "$OUT_PBTXT" \
                          --skip_validate_referenced_geos=true; then
    echo "Command succeeded as expected"
    exit 0
  else
    echo "Expected success but command failed"
    exit 1
  fi
fi
