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

"""Provides utilities for working with Asset URI interfaces."""

import re

# The prefix used for gRPC service dependencies.
GRPC_URI_PREFIX = "grpc://"
# The prefix used for proto-based data dependencies.
DATA_URI_PREFIX = "data://"

_URI_REGEX = re.compile(
    r"^(grpc://|data://)([A-Za-z_][A-Za-z0-9_]*\.)+[A-Za-z_][A-Za-z0-9_]*$"
)


def ValidateInterfaceName(uri: str) -> None:
  """Validates an interface name with a protocol prefix.

  Args:
    uri: The URI to validate.

  Raises:
    ValueError: If the URI is not formatted as
    '<protocol>://<package>.<message>'.
  """
  if not _URI_REGEX.fullmatch(uri):
    raise ValueError(
        "Invalid interface name: expected URI to be formatted as"
        f" '<protocol>://<package>.<message>', got '{uri}'"
    )
