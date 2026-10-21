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

"""Forwarding definitions to intrinsic_sdk."""

# Forwarding definitions were added in support of go/intrinsic-ioc-transition-tdd.

load(
    "//intrinsic_sdk/intrinsic/skills/build_defs:skill.bzl",
    _SkillInfo = "SkillInfo",
    _cc_skill = "cc_skill",
    _py_skill = "py_skill",
    _skill_manifest = "skill_manifest",
)

SkillInfo = _SkillInfo
cc_skill = _cc_skill
py_skill = _py_skill
skill_manifest = _skill_manifest
