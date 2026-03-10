# Copyright 2025 Silicon Laboratories Inc. www.silabs.com
#
# SPDX-License-Identifier: Zlib
#
# The licensor of this software is Silicon Laboratories Inc.
#
# This software is provided 'as-is', without any express or implied
# warranty. In no event will the authors be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.

from step_data.StepData import StepData

class InitiatorData(StepData):
    def __init__(self, subevent_tstamp, freq_comp, ref_plevel, num_antenna_paths, step_data_len, raw_step_data):
        super().__init__("initiator", subevent_tstamp, freq_comp, ref_plevel, num_antenna_paths, step_data_len, raw_step_data)
        self._ras_status_byte_index = 8
        self._ras_num_of_steps_index = 11
        self.step_channels = []

    def parse_step_data(self):
        """ Iterate over the step data, parse it, and save it in a dict. """
        byte_index = 0
        step_index = 1

        # Step through the raw procedure data
        # step_data_len = self.step_data_len
        while byte_index < self.step_data_len:
            # Start by parsing the mode-agnostic data. This should be the first 3 bytes of every step
            step_mode, byte_index = self._get_bytes(byte_index, 1)
            step_channel, byte_index = self._get_bytes(byte_index, 1)
            self.step_channels.append(step_channel)
            single_step_data_len, byte_index = self._get_bytes(byte_index, 1)

            step_data = {
                "Step_Mode": int(step_mode),
                "Step_Channel": int(step_channel),
                "Step_Data_Length": int(single_step_data_len)
            }

            # Parse the role and mode specific data
            mode_specific_data, byte_index = self._get_mode_specific_data(step_mode, byte_index)

            # Add the parsed data to the dict
            step_data.update(mode_specific_data)
            self.parsed_step_data[f"{self.role}_step_data"][f"step_{step_index}"] = step_data
            step_index += 1
