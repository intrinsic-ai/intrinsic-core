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

"""xy_plotter can plot X-Y curves exported from a test."""

import dataclasses
import os
from typing import Any
from typing import Optional
from typing import Union

from absl import app
from absl import flags
import matplotlib.pyplot as plt
import numpy as np

_TEST_DATA_DIR = '/tmp/xy_plotter'
_MARKER_SIZE = 8

_DATA_PATH = flags.DEFINE_string(
    'data_path',
    None,
    'Path to the data dir containing the trajectories.',
    required=True,
)
_FIG_OUTPUT_PATH = flags.DEFINE_string(
    'fig_output_path',
    _TEST_DATA_DIR,
    'The created figure is saved in that directory.',
)
_PLOT_NAME = flags.DEFINE_string(
    'plot_name', None, 'Name of the plot', required=True
)
_PATH_NAMES = flags.DEFINE_list(
    'path_names',
    None,
    'Comma-separated list of names of the paths to plot. Has to be the same '
    'length as --line_styles.',
    required=True,
)
_LINE_STYLES = flags.DEFINE_list(
    'line_styles',
    None,
    'Comma-separated list of line styles to use for each path plot. Has to be '
    'the same length as --path_names.',
    required=True,
)
_MARKER_TYPE = flags.DEFINE_enum(
    'marker_type',
    None,
    ['', '|', '.'],
    'Marker type to use for all paths in the plot.',
    required=True,
)

flags.register_validator(
    'path_names',
    lambda value: (value),
    message='--path_names must be of length > 0!',
)
flags.register_validator(
    'line_styles',
    lambda value: (value),
    message='--line_styles must be of length > 0!',
)


def load_csv_file(path: str) -> Optional[Union[list[Any], Any]]:
  if os.path.isfile(path):
    return np.genfromtxt(path, delimiter=',')
  return None


@dataclasses.dataclass(frozen=True)
class XYPathAndLineStyle:
  path_name: str
  xy_path: np.ndarray
  line_style: str


def load_data(
    data_dir: str, plot_name: str, path_names: list[str], line_styles: list[str]
) -> list[XYPathAndLineStyle]:
  """Loads a list of [XY paths and line styles] saved by the trajectory_plot cpp package."""
  xy_paths_and_line_styles = []
  for path_name, line_style in zip(path_names, line_styles):
    path = os.path.join(data_dir, plot_name, path_name + '.csv')
    if os.path.isfile(path):
      xy_path = load_csv_file(path)
      xy_paths_and_line_styles.append(
          XYPathAndLineStyle(
              path_name=path_name,
              xy_path=xy_path,
              line_style=line_style,
          )
      )
    else:
      raise ValueError(f'Path {path} does not exist.')
  return xy_paths_and_line_styles


def plot_xy_paths(
    plot_name: str, xy_paths_and_line_styles: list[XYPathAndLineStyle]
) -> None:
  """Plots the given XY paths."""
  for xy_path_and_line_style in xy_paths_and_line_styles:
    plt.plot(
        xy_path_and_line_style.xy_path[:, 0],
        xy_path_and_line_style.xy_path[:, 1],
        label=xy_path_and_line_style.path_name,
        linestyle=xy_path_and_line_style.line_style,
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )
  plt.title(plot_name)
  plt.xlabel('x')
  plt.ylabel('y')
  plt.legend()


def main(_):
  data_dir = _DATA_PATH.value
  plot_name = _PLOT_NAME.value
  xy_paths_and_line_styles = load_data(
      data_dir, plot_name, _PATH_NAMES.value, _LINE_STYLES.value
  )
  plot_xy_paths(plot_name, xy_paths_and_line_styles)
  plt.savefig(_FIG_OUTPUT_PATH.value)


if __name__ == '__main__':
  app.run(main)
