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

"""trajectory_plotter can plot a trajectory exported from a test."""

import dataclasses
import logging
import os
from typing import Any
from typing import Optional
from typing import Union

from absl import app
from absl import flags
from matplotlib.lines import Line2D
import matplotlib.pyplot as plt
import numpy as np

_TEST_DATA_DIR = '/tmp/trajectory_plotter'
_FIGURE_SIZE = (20, 15)
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
_TRAJECTORY_NAME = flags.DEFINE_string(
    'trajectory_name', None, 'Name of the trajectory', required=False
)
_TRAJECTORY_NAMES = flags.DEFINE_list(
    'trajectory_names',
    None,
    'Names of multiple trajectories to plot.',
    required=False,
)
_PLOT_NAME = flags.DEFINE_string(
    'plot_name', None, 'Name of the overall plot.', required=False
)
_SAME_COLOR_PER_TRAJECTORY = flags.DEFINE_bool(
    'same_color_per_trajectory',
    False,
    'If true, all DOFs of the same trajectory share the same color, while '
    'different trajectories have different colors.',
)
_TRAJECTORY_TYPE = flags.DEFINE_enum(
    'trajectory_type',
    None,
    ['cart', 'joint'],
    'Type of the trajectory',
    required=True,
)
_MARKER_TYPE = flags.DEFINE_enum(
    'marker_type',
    None,
    ['', '|', '.'],
    'Marker type to use for the trajectory',
    required=True,
)


def load_csv_file(path: str) -> Optional[Union[list[Any], Any]]:
  if os.path.isfile(path):
    return np.genfromtxt(path, delimiter=',')
  return None


@dataclasses.dataclass(frozen=True)
class Trajectory:
  positions: np.ndarray
  velocities: np.ndarray
  accelerations: np.ndarray
  timestamps: np.ndarray
  jerks: Optional[np.ndarray] = None
  torques: Optional[np.ndarray] = None
  advance_timestamp: Optional[float] = None


def load_data(
    data_dir: str, trajectory_name: str, plot_name: Optional[str] = None
) -> Trajectory:
  """Loads a trajectory saved by the trajectory_plot cpp package."""
  filenames = {
      'positions': 'positions.csv',
      'velocities': 'velocities.csv',
      'accelerations': 'accelerations.csv',
      'timestamps': 'timestamps.csv',
      'jerks': 'jerks.csv',
      'torques': 'torques.csv',
      'advance_timestamp': 'advance_timestamp.csv',
  }
  args = {}
  for key, filename_name in filenames.items():
    if plot_name:
      path = os.path.join(data_dir, plot_name, trajectory_name, filename_name)
    else:
      path = os.path.join(data_dir, trajectory_name, filename_name)
    if os.path.isfile(path):
      loaded = load_csv_file(path)
      if key == 'advance_timestamp' and loaded is not None:
        args[key] = float(np.asarray(loaded).ravel()[0])
      else:
        args[key] = loaded
  return Trajectory(**args)


def as_2d(arr: Optional[np.ndarray]) -> Optional[np.ndarray]:
  if arr is None:
    return None
  return arr if arr.ndim > 1 else arr[:, np.newaxis]


def plot_series(
    ax: plt.Axes,
    t: np.ndarray,
    data: np.ndarray,
    dim: int,
    label: Optional[str],
    color: Optional[str],
    t_adv: Optional[float],
) -> None:
  """Plots a curve on `ax`, splitting into solid/dashed if t_adv is provided."""
  val = data[:, dim]
  if t_adv is not None and len(t) > 0 and t[0] <= t_adv < t[-1]:
    split = int(np.searchsorted(t, t_adv, side='right'))
    ax.plot(
        t[: split + 1],
        val[: split + 1],
        label=label,
        color=color,
        linestyle='-',
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )
    ax.plot(
        t[split:],
        val[split:],
        color=color,
        linestyle='--',
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )
    ax.plot(
        t[0],
        val[0],
        marker='o',
        color=color,
        markeredgecolor='k',
        markersize=6,
    )
    if split < len(t):
      ax.plot(
          t[split],
          val[split],
          marker='D',
          color=color,
          markeredgecolor='k',
          markersize=6,
      )
    ax.plot(
        t[-1],
        val[-1],
        marker='s',
        color=color,
        markeredgecolor='k',
        markersize=6,
    )
  else:
    ax.plot(
        t,
        val,
        label=label,
        color=color,
        linestyle='-',
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )


def setup_legend(ax: plt.Axes, has_advance: bool) -> None:
  """Sets up legend handles and labels."""
  handles, labels = ax.get_legend_handles_labels()
  if not has_advance:
    ax.legend()
    return

  seen = set()
  unique_handles = []
  unique_labels = []
  for h, l in zip(handles, labels):
    if l and l not in seen and not l.startswith('_'):
      seen.add(l)
      unique_handles.append(h)
      unique_labels.append(l)

  unique_handles.extend([
      Line2D([0], [0], color='black', linestyle='-', linewidth=2),
      Line2D([0], [0], color='black', linestyle='--', linewidth=1.5),
      Line2D(
          [0],
          [0],
          marker='o',
          color='w',
          markerfacecolor='k',
          markeredgecolor='k',
          markersize=6,
      ),
      Line2D(
          [0],
          [0],
          marker='D',
          color='w',
          markerfacecolor='k',
          markeredgecolor='k',
          markersize=6,
      ),
      Line2D(
          [0],
          [0],
          marker='s',
          color='w',
          markerfacecolor='k',
          markeredgecolor='k',
          markersize=6,
      ),
  ])
  unique_labels.extend([
      'Executed segment',
      'Predicted tail',
      'Horizon start (o)',
      'Horizon switch (D)',
      'Horizon end (s)',
  ])

  ax.legend(
      unique_handles,
      unique_labels,
      loc='upper right',
      bbox_to_anchor=(1.22, 1.05),
      fontsize=9,
  )


def plot_joint(
    trajs: list[Trajectory],
    trajectory_names: list[str],
    plot_title: str,
    same_color_per_trajectory: bool = False,
) -> None:
  """Plots joint trajectories."""
  has_jerks = any(traj.jerks is not None for traj in trajs)
  has_torques = any(traj.torques is not None for traj in trajs)
  num_subplots = 3 + (1 if has_jerks else 0) + (1 if has_torques else 0)

  _, axs = plt.subplots(num_subplots, 1, figsize=_FIGURE_SIZE, sharex=True)
  if num_subplots == 1:
    axs = [axs]

  prop_cycle = plt.rcParams['axes.prop_cycle']
  colors = prop_cycle.by_key().get('color', ['b', 'g', 'r', 'c', 'm', 'y', 'k'])

  advance_times = []
  for traj_idx, (traj, name) in enumerate(zip(trajs, trajectory_names)):
    if traj.positions is None:
      continue

    pos = as_2d(traj.positions)
    vel = as_2d(traj.velocities)
    acc = as_2d(traj.accelerations)
    jerk = as_2d(traj.jerks)
    trq = as_2d(traj.torques)
    t = (
        np.asarray(traj.timestamps).ravel()
        if traj.timestamps is not None
        else np.array([])
    )
    t_adv = traj.advance_timestamp

    if t_adv is not None and len(t) > 0 and t[0] <= t_adv < t[-1]:
      advance_times.append(t_adv)

    traj_color = (
        colors[traj_idx % len(colors)] if same_color_per_trajectory else None
    )

    for dim in range(pos.shape[1]):
      if same_color_per_trajectory:
        c = traj_color
        label = name if dim == 0 else None
      elif len(trajs) == 1:
        c = colors[dim % len(colors)]
        label = str(dim)
      else:
        c = colors[(traj_idx * pos.shape[1] + dim) % len(colors)]
        label = f'{name}_{dim}'

      plot_series(axs[0], t, pos, dim, label, c, t_adv)
      plot_series(axs[1], t, vel, dim, label, c, t_adv)
      plot_series(axs[2], t, acc, dim, label, c, t_adv)

      plot_id = 3
      if has_jerks:
        if jerk is not None:
          plot_series(axs[plot_id], t, jerk, dim, label, c, t_adv)
        plot_id += 1
      if has_torques:
        if trq is not None:
          plot_series(axs[plot_id], t, trq, dim, label, c, t_adv)
        plot_id += 1

  # Add vertical reference lines at advance timestamps if present.
  for t_adv in advance_times:
    for ax in axs:
      ax.axvline(
          x=t_adv, color='gray', linestyle=':', linewidth=1.2, alpha=0.75
      )

  axs[0].set_ylabel('p')
  axs[0].set_title(plot_title)
  axs[1].set_ylabel('v')
  axs[2].set_ylabel('a')

  plot_id = 3
  if has_jerks:
    axs[plot_id].set_ylabel('j')
    plot_id += 1
  if has_torques:
    axs[plot_id].set_ylabel('t')
    plot_id += 1

  for ax in axs:
    ax.grid(True, linestyle='--', alpha=0.6)

  setup_legend(
      axs[0], any(traj.advance_timestamp is not None for traj in trajs)
  )
  axs[-1].set_xlabel('t in s')


def plot_cart(traj: Trajectory, trajectory_name: str) -> None:
  """Plots a cart trajectory."""

  labels = ['x', 'y', 'z', 'r_x', 'r_y', 'r_z']

  _, axs = plt.subplots(3, 1, figsize=_FIGURE_SIZE)

  for dim, label in zip(range(traj.positions.shape[1]), labels):
    axs[0].plot(
        traj.timestamps,
        traj.positions[:, dim],
        label=label,
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )
  axs[0].legend()
  axs[0].set_ylabel('p')
  axs[0].set_title(trajectory_name)

  for dim, label in zip(range(traj.velocities.shape[1]), labels):
    axs[1].plot(
        traj.timestamps,
        traj.velocities[:, dim],
        label=label,
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )
  axs[1].set_ylabel('v')

  for dim, label in zip(range(traj.accelerations.shape[1]), labels):
    axs[2].plot(
        traj.timestamps,
        traj.accelerations[:, dim],
        label=label,
        marker=_MARKER_TYPE.value,
        markersize=_MARKER_SIZE,
    )
  axs[2].legend()
  axs[2].set_ylabel('a')
  axs[2].set_xlabel('t in s')


def main(_):
  data_dir = _DATA_PATH.value
  plot_name = _PLOT_NAME.value
  if _TRAJECTORY_NAMES.value:
    trajectory_names = _TRAJECTORY_NAMES.value
    plot_title = (
        plot_name
        if plot_name
        else (
            _TRAJECTORY_NAME.value if _TRAJECTORY_NAME.value else 'trajectories'
        )
    )
  elif _TRAJECTORY_NAME.value:
    trajectory_names = [_TRAJECTORY_NAME.value]
    plot_title = _TRAJECTORY_NAME.value
  else:
    raise ValueError(
        'Either --trajectory_name or --trajectory_names must be specified.'
    )

  trajs = [load_data(data_dir, name, plot_name) for name in trajectory_names]

  if _TRAJECTORY_TYPE.value == 'joint':
    plot_joint(
        trajs,
        trajectory_names,
        plot_title,
        same_color_per_trajectory=_SAME_COLOR_PER_TRAJECTORY.value,
    )
  elif _TRAJECTORY_TYPE.value == 'cart':
    if len(trajs) == 1:
      plot_cart(trajs[0], plot_title)
    else:
      logging.error('Multiple cart trajectories not supported yet.')
  else:
    logging.error('Unknown trajectory type: %s', _TRAJECTORY_TYPE.value)

  plt.savefig(_FIG_OUTPUT_PATH.value, bbox_inches='tight')


if __name__ == '__main__':
  app.run(main)
