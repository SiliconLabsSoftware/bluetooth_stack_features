# Python class for reading serial data and pushing to queue.
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


import signal
import serial
import multiprocessing as mp
from utils import *

class SerialReadProcess(mp.Process):
    def __init__(self, serial_port, baud_rate, queue, stop_event):
        super().__init__()
        self._serial_port = serial_port
        self._baud_rate = baud_rate
        self._queue = queue
        self._stop_event = stop_event
        self._ser = None

    def open_serial_connection(self):
        try:
            self._ser = serial.Serial(port=self._serial_port, baudrate=self._baud_rate, timeout=1)
        except Exception as e:
            print(e)
            self._ser = None

    def run(self):
        signal.signal(signal.SIGINT, self._handle_sigint)
        self.open_serial_connection()

        # Check if the connection was opened
        if self._ser != None:
            print(f"Serial connection opened to {self._serial_port} with baud rate {self._baud_rate}.")
        else:
            print(f"Failed to open serial connection to {self._serial_port}")
            exit(1)

        self._ser.reset_input_buffer()
        try:
            while not self._stop_event.is_set():
                data = self._ser.readline()
                if data:
                    self._queue.put(data)

        except KeyboardInterrupt:
            self._stop_event.set()
        finally:
            if self._ser is not None and self._ser.is_open:
                self._ser.close()

    def _handle_sigint(self, signum, frame):
        self._stop_event.set()
        if self._ser is not None and self._ser.is_open:
            self._ser.cancel_read()
