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

"""Limit recommendations for GRPC connections."""

# Metadata limit, this includes, for example, the size of the information
# gathered from an absl::Status and ExtendedStatus on error. Default is 8KB.
# We use a rather large limit because this in particular contains
# ExtendedStatus information which collects traces and can be signifantly
# larger than the default. Any request with metadata larger than the hard
# limit is rejected. Between the soft limit and the hard limit, some requests
# will be rejected.

GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT = 512 * 1024  # Bytes
GRPC_RECOMMENDED_MAX_METADATA_HARD_LIMIT = (
    GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT
    + GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT / 4
)
