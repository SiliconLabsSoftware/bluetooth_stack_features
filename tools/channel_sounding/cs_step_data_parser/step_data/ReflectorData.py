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

class ReflectorData(StepData):
    def __init__(self, subevent_tstamp, freq_comp, ref_plevel, num_antenna_paths, step_data_len, raw_step_data, step_channels):
        super().__init__("reflector", subevent_tstamp, freq_comp, ref_plevel, num_antenna_paths, step_data_len, raw_step_data)
        self._step_channels = step_channels
        self._ras_status_byte_index = 8
        self._ras_num_of_steps_index = 11

    def parse_step_data(self):
        """
        Parse RAS reflector data.
        For more details on the RAS data structure, refer to the BLE Ranging Service v1.0
        specification section 3.2.1.
        """
        # Extract subevent header fields
        status_byte, byte_index = self._get_bytes(self._ras_status_byte_index, 1)
        ranging_done_status = (int(status_byte) >> 4) & 0x0F
        subevent_done_status = int(status_byte) & 0x0F

        num_steps, byte_index = self._get_bytes(self._ras_num_of_steps_index, 1)

        if ranging_done_status != 0 or subevent_done_status != 0:
            print(f"RAS Data status incomplete. Ranging={ranging_done_status}, Subevent={subevent_done_status}")

        byte_index = 12  # After headers removed
        for step_idx in range(1, num_steps+1):
            step_mode, byte_index = self._get_bytes(byte_index, 1)

            # Error check: bit 7 should not be set
            if int(step_mode) & 0x80:
                print(f"Reflector step {step_idx} aborted (bit 7 set).")
                return -1

            mode_type = int(step_mode) & 0x03

            if mode_type == 0:
                single_step_data_len = 3
            elif mode_type == 1:
                single_step_data_len = 6
            elif mode_type == 2:
                single_step_data_len = 4 * (self.num_antenna_paths + 1) + 1

            if byte_index + single_step_data_len > len(self.raw_step_data_array):
                break

            # Insert channel based on channel reported by initiator
            step_channel = self._step_channels[step_idx - 1]

            step_data = {
                "Step_Mode": int(step_mode),
                "Step_Channel": int(step_channel),
                "Step_Data_Length": int(single_step_data_len)
            }

            mode_specific_data, byte_index = self._get_mode_specific_data(mode_type, byte_index)
            # Add the parsed data to the dict
            step_data.update(mode_specific_data)
            self.parsed_step_data[f"{self.role}_step_data"][f"step_{step_idx}"] = step_data

        return 0
