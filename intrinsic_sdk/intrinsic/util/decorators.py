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

"""Helper decorators for Python files."""


def overrides(interface):
  """Overrides decorator to annotate method overrides parent's."""

  def overrider(method):
    assert hasattr(
        interface, method.__name__
    ), 'method %s declared to be @overrides is not defined in %s' % (
        method.__name__,
        interface.__name__,
    )
    return method

  return overrider
