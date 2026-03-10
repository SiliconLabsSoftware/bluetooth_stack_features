# Python class for plotting data in real time.
#
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

import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore
import signal
import collections
import time
from utils import *

class DataPlotter(QtWidgets.QMainWindow):
    def __init__(self, window_size: tuple,
                 refresh_period: int,
                 buffer_size: int,
                 data_queue):
        self._window_size = window_size
        self._refresh_period = refresh_period

        self._plot = None
        self._cs_filtered_label = None
        self._velocity_label = None
        self._velocity_widget = None

        self._cs_data_buffer = collections.deque([0], maxlen=buffer_size)
        self._raw_distance_buffer = collections.deque([0], maxlen=buffer_size)
        self._rssi_data_buffer = collections.deque([0], maxlen=buffer_size)
        self._rssi_distance_buffer = collections.deque([0], maxlen=buffer_size)
        self._velocity_buffer = collections.deque([0], maxlen=buffer_size)
        self._time_buffer = collections.deque([0], maxlen=buffer_size)
        self._data_queue = data_queue

        self._timer = QtCore.QTimer()
        self._start_time = 0
        self._previous_sample_time = 0

        # Track whether each metric has been received
        self._raw_distance_ever_received = False
        self._rssi_distance_ever_received = False
        self._velocity_ever_received = False
        # Start main app, otherwise widgets can't be added
        self._app = QtWidgets.QApplication.instance()
        if not self._app:  # Create a new instance if it doesn't exist
            self._app = QtWidgets.QApplication([])

        super().__init__()
        self._setup_window()

        # Add handler for ctrl+c. This way we can stop the timers before closing the app
        signal.signal(signal.SIGINT, self._handle_sigint)

    def _setup_window(self):
        pg.setConfigOption("background", "#F4F4F4")
        pg.setConfigOption("foreground", "#333333")

        self.setWindowTitle("CS Plotter")
        self.resize(self._window_size[0], self._window_size[1])

        # Create a central widget and set it as the main window's central widget
        central_widget = QtWidgets.QWidget()
        self.setCentralWidget(central_widget)

        # Create splitters so that we can distribute the widgets in the main widget
        upper_upper_splitter = QtWidgets.QSplitter(QtCore.Qt.Horizontal)
        upper_lower_splitter = QtWidgets.QSplitter(QtCore.Qt.Horizontal)
        main_splitter = QtWidgets.QSplitter(QtCore.Qt.Vertical)

        # Create widgets for the upper portion of the main widget
        cs_filtered_widget, self._cs_filtered_label = create_label_widget()
        self._velocity_widget, self._velocity_label = create_label_widget()

        upper_upper_splitter.addWidget(cs_filtered_widget)
        upper_upper_splitter.addWidget(self._velocity_widget)

        # Hide velocity widget initially until data is received
        self._velocity_widget.hide()

        plot_window = pg.GraphicsLayoutWidget(title="Real-Time CS Distance Plot")
        self._plot = plot_window.addPlot()
        self._plot.setLabel("left", "Distance (m)", color="#555555", size="12pt")
        self._plot.setLabel("bottom", "Time (s)", color="#555555", size="12pt")
        self._legend = self._plot.addLegend(offset=(-1,1))

        # Create CS distance curve (always visible)
        self._curve_cs_distance = self._plot.plot(pen=pg.mkPen("#3498DB", width=5), name="CS DISTANCE")
        # Create optional curves but don't add to plot yet
        self._curve_raw_distance = pg.PlotCurveItem(pen=pg.mkPen("#9B59B6", width=3))
        self._curve_rssi_distance = pg.PlotCurveItem(pen=pg.mkPen("#E74C3C", width=3))
        # Create a second y-axis on the right for velocity
        self._plot_velocity = pg.ViewBox()
        self._plot.scene().addItem(self._plot_velocity)
        self._plot.getAxis('right').linkToView(self._plot_velocity)
        self._plot_velocity.setXLink(self._plot)
        self._plot.setLabel("right", "Velocity (m/s)", color="#555555", size="12pt")
        # Hide the right axis initially until velocity data is received
        self._plot.hideAxis('right')
        # Create velocity curve but don't add to legend yet
        self._curve_velocity = pg.PlotCurveItem(pen=pg.mkPen("#E9B36C", width=3))

        # Update views when plot is resized
        def updateViews():
            self._plot_velocity.setGeometry(self._plot.vb.sceneBoundingRect())
            self._plot_velocity.linkedViewChanged(self._plot.vb, self._plot_velocity.XAxis)
        updateViews()
        self._plot.vb.sigResized.connect(updateViews)

        # Add the plot and upper split to the main splitter. Now we should have plot in the lower and values in the upper section
        main_splitter.addWidget(upper_upper_splitter)
        main_splitter.addWidget(upper_lower_splitter)
        main_splitter.addWidget(plot_window)

        layout = QtWidgets.QVBoxLayout()
        layout.addWidget(main_splitter)
        central_widget.setLayout(layout)

        # Layout is complete

    def start_plot(self):
        # Initialize the timer so that we get periodic updates to the plot
        self._start_time = time.perf_counter()
        self._previous_sample_time = self._start_time
        self._timer.timeout.connect(self._update_plot)
        self._timer.start(self._refresh_period)

        self.show()
        self._app.exec()

    def _update_plot(self):
        cs_distance_received = False
        raw_distance_received = False
        rssi_distance_received = False
        velocity_received = False

        while not self._data_queue.empty():
            try:
                serial_data = self._data_queue.get().decode("utf-8")
                parsed_serial_data, datatype = parse_data(serial_data)
                if datatype == 'MULTI' and parsed_serial_data:
                    # Handle multiple values from single line
                    if 'distance' in parsed_serial_data and not cs_distance_received:
                        distance_m = mm_to_m(parsed_serial_data['distance'])
                        self._cs_data_buffer.append(distance_m)
                        cs_distance_received = True
                    if 'raw_distance' in parsed_serial_data and not raw_distance_received:
                        raw_distance_m = mm_to_m(parsed_serial_data['raw_distance'])
                        self._raw_distance_buffer.append(raw_distance_m)
                        raw_distance_received = True
                        # Add curve to plot on first data
                        if not self._raw_distance_ever_received:
                            self._plot.addItem(self._curve_raw_distance)
                            self._legend.addItem(self._curve_raw_distance, "RAW DISTANCE")
                            self._raw_distance_ever_received = True
                    if 'rssi_distance' in parsed_serial_data and not rssi_distance_received:
                        rssi_distance_m = mm_to_m(parsed_serial_data['rssi_distance'])
                        self._rssi_distance_buffer.append(rssi_distance_m)
                        rssi_distance_received = True
                        # Add curve to plot on first data
                        if not self._rssi_distance_ever_received:
                            self._plot.addItem(self._curve_rssi_distance)
                            self._legend.addItem(self._curve_rssi_distance, "RSSI DISTANCE")
                            self._rssi_distance_ever_received = True
                    if 'speed' in parsed_serial_data and not velocity_received:
                        self._velocity_buffer.append(parsed_serial_data['speed'])
                        velocity_received = True
                        # Add curve to plot on first data
                        if not self._velocity_ever_received:
                            self._plot_velocity.addItem(self._curve_velocity)
                            self._legend.addItem(self._curve_velocity, "VELOCITY")
                            self._velocity_widget.show()
                            self._plot.showAxis('right')
                            self._velocity_ever_received = True
            except UnicodeError:
                print("Failed at decoding serial data! Please ensure the data format complies with the format specified in the README's FAQ.")
                quit()
            except Exception as e:
                print(f"Error processing queue data: {e}")

        current_time = time.perf_counter() - self._start_time
        self._time_buffer.append(current_time)

        # Display the values, and if no data was received append the last read value to the buffers
        if not cs_distance_received:
            self._cs_data_buffer.append(self._cs_data_buffer[-1])
        if not raw_distance_received:
            self._raw_distance_buffer.append(self._raw_distance_buffer[-1])
        if not rssi_distance_received:
            self._rssi_distance_buffer.append(self._rssi_distance_buffer[-1])

        self._cs_filtered_label.setText(
            f"<div style='text-align: center;'><span style='font-size: 75px;font-weight:bold; color: #2C3E50;'>Distance</span><br><span style='font-size: {LABEL_TEXT_SIZE_PX}px;font-weight: bold; color: #2C3E50;'>{self._cs_data_buffer[-1]} m</span>"
        )

        if not velocity_received:
            self._velocity_buffer.append(self._velocity_buffer[-1])

        if self._velocity_ever_received:
            self._velocity_label.setText(
                f"<div style='text-align: center;'><span style='font-size: 75px;font-weight:bold; color: #2C3E50;'>Velocity</span><br><span style='font-size: {LABEL_TEXT_SIZE_PX}px;font-weight: bold; color: #2C3E50;'>{self._velocity_buffer[-1]} m/s</span>"
            )

        self._curve_cs_distance.setData(self._time_buffer, self._cs_data_buffer)
        self._curve_raw_distance.setData(list(self._time_buffer), list(self._raw_distance_buffer))
        self._curve_rssi_distance.setData(list(self._time_buffer), list(self._rssi_distance_buffer))

        if self._velocity_ever_received:
            self._curve_velocity.setData(list(self._time_buffer), list(self._velocity_buffer))

        # Adjust the Y-range for distance (left axis)
        if Y_LIM_M != None:
            y_lim = Y_LIM_M
        else:
            # Use all distance data for scaling
            max_values = [max(self._cs_data_buffer) if self._cs_data_buffer else 0,
                         max(self._raw_distance_buffer) if self._raw_distance_buffer else 0,
                         max(self._rssi_distance_buffer) if self._rssi_distance_buffer else 0]
            y_lim = max(max_values) if any(max_values) else 1
        self._plot.setYRange(-y_lim, y_lim)
        self._plot.setXRange(self._time_buffer[0], self._time_buffer[-1] + X_PADDING_S)

        # Adjust the Y-range for velocity (right axis)
        if self._velocity_ever_received and self._velocity_buffer:
            vel_max = max(abs(min(self._velocity_buffer)), abs(max(self._velocity_buffer)))
            vel_range = max(vel_max * 1.2, 0.1)  # Add 20% padding, minimum 0.1
            self._plot_velocity.setYRange(-vel_range, vel_range)

    def _handle_sigint(self, signum, frame):
        print("\nCtrl+C detected. Closing application...")
        self._timer.stop()
        self._app.quit()
