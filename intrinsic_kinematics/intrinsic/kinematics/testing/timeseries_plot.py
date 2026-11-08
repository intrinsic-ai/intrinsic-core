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

"""timeseries_plot can plot a timeseries exported from a test."""

import logging

from absl import app
from absl import flags
import matplotlib.pyplot as plt
import pandas as pd

_DATA_PATH = flags.DEFINE_string(
    'csv_file_path', None, 'Path to the data csv file.', required=True
)
_FIG_OUTPUT_PATH = flags.DEFINE_string(
    'fig_output_path',
    None,
    'The created figure is saved in that directory.',
    required=True,
)


PLOT_ROWS = 1
PLOT_COLS = 1


def main(_):
  logging.info('Creating plot for %s', _DATA_PATH.value)
  data = pd.read_csv(_DATA_PATH.value)

  plt.figure()
  data.plot(subplots=True)

  plt.savefig(_FIG_OUTPUT_PATH.value)


if __name__ == '__main__':
  app.run(main)
