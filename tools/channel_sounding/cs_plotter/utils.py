# Utilities used by the CS Plotter project.
#
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

from pyqtgraph.Qt import QtWidgets, QtCore
import re

Y_LIM_M = None # If None, the scale will be dynamic
X_SCALE_S = 30
X_PADDING_S = 1 # Seconds of padding added to the x-axis
PLOT_REFRESH_PERIOD_MS = 30
BUFFER_SIZE = int(X_SCALE_S / (PLOT_REFRESH_PERIOD_MS / 1000))  # Number of samples displayed
LABEL_TEXT_SIZE_PX = 150
WINDOW_SIZE = (1440, 1080)

def parse_data(data):
    # Example with raw distance: '[APP] [1] 04:87:27:E7:02:C0 |   347 mm |     356mm | 0.87  |       23mm | -0.00 m/s'
    # Example without raw distance: '[APP] [1] 04:87:27:E7:02:C0 |   623 mm | 0.82  |      217mm | -0.00 m/s'
    # Return dictionary with distance, raw distance, RSSI distance, and speed if found
    # Skip header lines
    if 'BT Address' in data or 'Dist.' in data:
        return None, None
    # Split by | to get individual fields
    parts = data.split('|')
    result = {}
    # Parse distance in mm: first distance field ("347 mm" or "623 mm")
    if len(parts) > 1:
        distance_match = re.search(r'(\d+)\s*mm', parts[1])
        if distance_match:
            result['distance'] = int(distance_match.group(1))
    # Determine format by checking if parts[2] contains 'mm' (raw distance) or is a decimal (likelihood)
    # If parts[2] has mm, format is: | Dist. | RAW Dist. | Like. | RSSI Dist. | Speed
    # If parts[2] has decimal, format is: | Dist. | Like. | RSSI Dist. | Speed
    has_raw_distance = False
    if len(parts) > 2:
        # Check if parts[2] contains a distance value (mm)
        if 'mm' in parts[2]:
            has_raw_distance = True
            raw_distance_match = re.search(r'(\d+)\s*mm', parts[2])
            if raw_distance_match:
                result['raw_distance'] = int(raw_distance_match.group(1))
    # Parse RSSI distance based on format
    if has_raw_distance:
        # Format: | Dist. | RAW Dist. | Like. | RSSI Dist. | Speed
        # RSSI is in parts[4]
        if len(parts) > 4:
            rssi_distance_match = re.search(r'(\d+)\s*mm', parts[4])
            if rssi_distance_match:
                result['rssi_distance'] = int(rssi_distance_match.group(1))
    else:
        # Format: | Dist. | Like. | RSSI Dist. | Speed
        # RSSI is in parts[3]
        if len(parts) > 3:
            rssi_distance_match = re.search(r'(\d+)\s*mm', parts[3])
            if rssi_distance_match:
                result['rssi_distance'] = int(rssi_distance_match.group(1))

    # Parse speed in m/s: look for pattern like "| +0.85 m/s" or "| -0.00 m/s"
    speed_match = re.search(r'([+-]?\d*\.?\d+)\s*m/s', data)
    if speed_match:
        result['speed'] = float(speed_match.group(1))
    # Return the result dictionary and a special type to indicate multiple values
    if result:
        return result, 'MULTI'
    else:
        return None, None

def create_label_widget():
    frame  = QtWidgets.QFrame()
    layout = QtWidgets.QVBoxLayout()
    label = QtWidgets.QLabel()
    layout.addWidget(label)
    layout.setAlignment(label, QtCore.Qt.AlignCenter)
    frame.setLayout(layout)

    return frame, label

def mm_to_m(distance_mm):
    return round(distance_mm / 1000.0, 1)
