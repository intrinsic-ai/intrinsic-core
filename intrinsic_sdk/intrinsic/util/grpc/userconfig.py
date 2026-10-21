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

"""Provides methods to handle user configuration files."""

import json
import os
import platform
from typing import Dict

_CONFIG_FOLDER = "intrinsic"
_CONFIG_FILE = "user.config"

SELECTED_ORGANIZATION = "selectedOrganization"
SELECTED_SOLUTION = "selectedSolution"
SELECTED_CLUSTER = "selectedCluster"
SELECTED_ADDRESS = "selectedAddress"


class NotFoundError(Exception):
  """Thrown when the user config cannot be found."""


def get_user_config_dir() -> str:
  """Returns the users config directory.

  Depends on the underlying OS (e.g. $HOME/.config) for Linux.

  TODO(bschimke): Add Windows and Mac support once required.

  Raises:
    NotImplementedError: if OS is unknown
    RuntimeError: if required env variables are missing.
  """
  if platform.system() != "Linux":
    raise NotImplementedError(f"OS '{platform.system()}' is not supported!")

  config_dir = os.environ.get("XDG_CONFIG_HOME")
  if config_dir:
    return config_dir

  home_dir = os.environ.get("HOME")
  if home_dir:
    return os.path.join(home_dir, ".config")

  raise RuntimeError("Neither $XDG_CONFIG_HOME nor $HOME are defined!")


def read() -> Dict[str, str]:
  """Returns the user config.

  Used to persist configuration across sessions, tools etc.

  Raises:
    ValueError: if config not found
  """
  env_path = os.path.join(get_user_config_dir(), _CONFIG_FOLDER, _CONFIG_FILE)
  try:
    with open(env_path, "r") as env_file:
      return json.load(env_file)
  except FileNotFoundError as e:
    raise NotFoundError(f"User config file ({env_path}) not found!") from e
