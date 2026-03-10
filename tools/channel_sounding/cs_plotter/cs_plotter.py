# This file is part of the CS Plotter project.
# CS Plotter is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CS Plotter is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should find a copy of the GNU General Public License
# in the project repository. If not, see <https://www.gnu.org/licenses/>.

import multiprocessing as mp
import argparse
from SerialReadProcess import SerialReadProcess
from DataPlotter import DataPlotter
from utils import *


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        prog="CS plotter",
        description="Graphical plotter for CS data",
    )

    parser.add_argument("-s", "--serial_port", required=True)
    parser.add_argument("-b", "--baud_rate", default=115200)

    args = parser.parse_args()

    data_queue = mp.Manager().Queue()

    reading_process = SerialReadProcess(args.serial_port, args.baud_rate, data_queue)
    reading_process.start()
    plotter = DataPlotter(window_size=WINDOW_SIZE, refresh_period=PLOT_REFRESH_PERIOD_MS,
                          buffer_size=BUFFER_SIZE, data_queue=data_queue)

    plotter.start_plot()
    reading_process.join()
