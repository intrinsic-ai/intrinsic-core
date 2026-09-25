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

"""
References to the current platform release versions.
"nightly" refers to the latest release via the autopush rollout group.
"prod" refers to the latest release via the prod rollout group.

Usage of these references on the release branches is not recommended since
any change to these contents would require creating another release
candidate, thus rendering these references immediately stale. Instead use the
`semver` action, for example:
https://github.com/intrinsic-ai/intrinsic-core/blob/b6962e49811f366faa8ea815bfa858ecd0ea1a0a/.github/workflows/release-charts.yml#L38
"""

# WARNING: This string is currently updated manually by the RelEng team. It
# may not represent the absolute most recent release and is only maintained at
# head and not on release branches themselves.
# TODO(b/424923989): when this is updated nightly remove the warning not to
# rely on it being update to date.
PLATFORM_RELEASE_NIGHTLY = "0.20260925.0-RC00"

# WARNING: This string is currently updated manually by the RelEng team. It
# may not represent the absolute most recent prod release and is only
# maintained at head and not on Rapid release branches themselves.
PLATFORM_RELEASE_PROD = "intrinsic.platform.20260720.RC09"
