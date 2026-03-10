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

import sys
import re
import json
import ast
from step_data.ReflectorData import ReflectorData
from step_data.InitiatorData import InitiatorData


RAS_CHAR_HANDLE = None
RANGING_DATA_UUID = "0x2c15"

# Utility extraction helper
def extract_field(pattern, line, cast=str, default=None):
    m = re.search(pattern, line)
    if not m:
        return default
    if len(m.group(1)) > 4:  # This should only be true if we read characteristic uuid
        try:
            b = ast.literal_eval(m.group(1))
            hex_value = hex_val = f"0x{int.from_bytes(b, byteorder='little'):04x}"
            return hex_val
        except Exception:
            return default
    try:
        return cast(m.group(1))
    except Exception:
        return default


def extract_bytes_literal(line, key, ras=False):
    """
    Extracts bytes literals from log lines.
    """
    # Simple HCI-style pattern:
    pattern = re.compile(r"""\b(?:data|value)=b(['"])((?:\\.|(?!\1).)*)\1""")

    m = re.search(pattern, line)
    if not m:
        return b""

    raw = m.group(2)
    # Step 1: turn Python-level escapes into actual bytes
    decoded = raw.encode("utf-8").decode("unicode_escape")
    # Step 2: convert to an actual bytes object
    final_bytes = decoded.encode("latin1")

    return final_bytes


def parse_raw_log(input_file):
    """
    Read the log line by line, and parse the CS data.
    """
    global RAS_CHAR_HANDLE

    with open(input_file, "r") as f:
        lines = f.readlines()

    procedures = []
    global_subevent_len = None
    global_subevent_interval = None

    current_proc = None
    collecting_reflector = False
    ras_intercepted = False

    for line in lines:
        # Find the RAS characteristic handle
        if "bt_evt_gatt_characteristic" in line and RAS_CHAR_HANDLE == None:
           uuid = extract_field(r"uuid=(b'.*?')", line, int)
           if uuid.lower() == RANGING_DATA_UUID:
              RAS_CHAR_HANDLE = extract_field(r"characteristic=(\d+)", line, int)

        # Global metadata from bt_evt_cs_procedure_enable_complete
        if "bt_evt_cs_procedure_enable_complete" in line:
            global_subevent_len = extract_field(r"subevent_len=(\d+)", line, int)
            global_subevent_interval = extract_field(r"subevent_interval=(\d+)", line, int)
            continue

        # Start of a new CS procedure: bt_evt_cs_result
        if "bt_evt_cs_result(" in line:
            if current_proc:
                # Check if the new procedure starts before the reflector data is fully parsed
                if current_proc["reflector_finished"] == False:
                    ras_intercepted = True

            current_proc = {
                "initiator_chunks": [],
                "reflector_chunks": [],
                "initiator_metadata": {},
                "initiator_finished": False,
                "reflector_finished": False,
            }
            procedures.append(current_proc)

            abort_reason = extract_field(r"abort_reason=(\d+)", line, int)
            freq_comp = extract_field(r"frequency_compensation=([-\d]+)", line, int)
            ref_power = extract_field(r"reference_power_level=([-\d]+)", line, int)
            num_paths = extract_field(r"num_antenna_paths=(\d+)", line, int)
            pds = extract_field(r"procedure_done_status=(\d+)", line, int)

            current_proc["initiator_metadata"] = {
                "freq_comp": freq_comp,
                "ref_power": ref_power,
                "num_paths": num_paths,
                "abort_reason": abort_reason,
            }

            chunk = extract_bytes_literal(line, "data")
            if chunk:
                current_proc["initiator_chunks"].append(chunk)

            if pds is not None and pds != 0:
                current_proc["initiator_finished"] = True

            continue

        # Continuation of initiator data
        if "bt_evt_cs_result_continue(" in line and current_proc:
            abort_reason = extract_field(r"abort_reason=(\d+)", line, int)

            if current_proc and abort_reason != 0:
                current_proc["initiator_metadata"]["abort_reason"] = abort_reason

            pds = extract_field(r"procedure_done_status=(\d+)", line, int)
            chunk = extract_bytes_literal(line, "data")
            if chunk:
                current_proc["initiator_chunks"].append(chunk)
            if pds is not None and pds != 0:
                current_proc["initiator_finished"] = True
            continue

        # Reflector data after initiator is finished
        if current_proc:
            if "bt_evt_gatt_characteristic_value" in line and RAS_CHAR_HANDLE != None:
                char = extract_field(r"characteristic=(\d+)", line, int)
                if char == RAS_CHAR_HANDLE:
                    val = extract_bytes_literal(line, "value", True)
                    if val and len(val) >= 1:
                        # Strip 1-byte segmentation header
                        if ras_intercepted == True:
                            """
                            In case the reflector data parsing was intercepted by a new procedure,
                            append to previous procedure.
                            """
                            procedures[-2]["reflector_chunks"].append(val[1:])
                        else:
                            current_proc["reflector_chunks"].append(val[1:])
                    collecting_reflector = True
            elif collecting_reflector and "RAS - real-time reception finished" in line:
                current_proc["reflector_finished"] = True
                collecting_reflector = False
                ras_intercepted = False

    return procedures, global_subevent_len, global_subevent_interval


def build_json(procedures, subevent_len, subevent_interval, output_file):
    procedure_count = 0
    out = {"cs_procedures": []}

    for proc_index, proc in enumerate(procedures):
        abort_reason = proc["initiator_metadata"].get("abort_reason", 0)
        if abort_reason != 0:
            print(f"Procedure {proc_index} aborted at initiator with reason {abort_reason}. Skipping to next procedure.")
            continue

        # Parse Initiator data
        initiator_raw = b"".join(proc["initiator_chunks"])
        num_paths = proc["initiator_metadata"].get("num_paths", 0)

        initiator_hex = " ".join(f"{b:02x}" for b in initiator_raw)
        initiator = InitiatorData(
            subevent_tstamp=subevent_len or 0,
            freq_comp=proc["initiator_metadata"].get("freq_comp", 0),
            ref_plevel=proc["initiator_metadata"].get("ref_power", 0),
            num_antenna_paths=num_paths,
            step_data_len=len(initiator_raw),
            raw_step_data=initiator_hex,
        )
        initiator.parse_step_data()

        # Parse reflector data
        ranging_body = b"".join(proc["reflector_chunks"])
        ranging_body_hex = " ".join(f"{b:02x}" for b in ranging_body)

        reflector = ReflectorData(
            subevent_tstamp=subevent_len or 0,
            freq_comp=0,
            ref_plevel=0,
            num_antenna_paths=num_paths,
            step_data_len=len(ranging_body),
            raw_step_data=ranging_body_hex,
            step_channels=initiator.step_channels
        )
        rslt = reflector.parse_step_data()

        if rslt != 0:
            print(f"Procedure {proc_index} aborted at reflector. Skipping to next procedure.")
            continue

        init_steps = initiator.parsed_step_data.get("initiator_step_data", {})
        refl_steps = reflector.parsed_step_data.get("reflector_step_data", {})

        if len(init_steps) != len(refl_steps):
            print(f"Step count mismatch in procedure {proc_index}: initiator={len(init_steps)}, reflector={len(refl_steps)}")
            continue

        out["cs_procedures"].append(
            {
                "metadata": {
                    "subevent_len": subevent_len,
                    "subevent_interval": subevent_interval,
                },
                "initiator_step_data": initiator.parsed_step_data,
                "reflector_step_data": reflector.parsed_step_data,
            }
        )

        procedure_count += 1

    with open(output_file, "w") as f:
        json.dump(out, f, indent=2)

    return procedure_count

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: step_data_parser.py <input_log.txt> <output.json>")
        sys.exit(1)

    procedures, se_len, se_int = parse_raw_log(sys.argv[1])
    procedure_count = build_json(procedures, se_len, se_int, sys.argv[2])
    print(f"Parsed {procedure_count} CS procedures into {sys.argv[2]}")
