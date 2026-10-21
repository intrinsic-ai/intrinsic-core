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

"""Canonical list of root locations during the IOC migration.

This list is used to strip include prefixes for C++ targets and to inject
PYTHONPATH root imports for Python targets so that references across google3/
and incode/ remain backwards-compatible during the migration.
"""

# LINT.IfChange(proto_includes)
GOOGLE3_OR_IOC_LOCATIONS = [
    "google3",
    "intrinsic_apis",
    "intrinsic_sdk",
    "intrinsic_runtime",
    "intrinsic_kinematics",
    "intrinsic_control",
    "intrinsic_motion_planning",
    "intrinsic_perception",
    "intrinsic_inference",
    "incode/intrinsic_simulation",
    "intrinsic_hardware",
]
# LINT.ThenChange(//.vscode/settings.json:proto_includes)
