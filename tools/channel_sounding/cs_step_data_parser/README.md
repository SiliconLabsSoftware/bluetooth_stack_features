# Logging and parsing raw BLE Channel Sounding step data

This document describes the steps required in order to log and parse raw Channel Sounding (CS) step data, using Silicon Labs CS sample applications. For more information on our CS solution,
please refer to our [official CS documentation](https://docs.silabs.com/rtl-lib/latest/rtl-lib-channel-sounding-getting-started/).

> [!NOTE]  
> This method has been developed and tested using SiSDK-2025.6.2 and Python 3.10.

## Collecting the logs

The step data is collected from the CS initiator using BGAPI-traces and the [BGAPI Trace Tool](https://github.com/SiliconLabs/bluetooth_stack_features/tree/master/tools/bgapi_trace). The BGAPI-trace includes all the step data both from the initiator and the reflector.

### Connecting JLinkRTTLogger

Assuming a Silicon Labs CS devkit, such as BRD2606A is used:

1. **Create a project based on the *bt_cs_soc_initiator* sample application, and flash it to the device.**

2. **While pressing BTN0, reset the device. On BRD2606A, an orange LED should light up. This indicates the application
     is waiting for the user to collect the BGAPI-traces.**

3. **Record and convert the BGAPI-traces as described [here](https://github.com/SiliconLabs/bluetooth_stack_features/tree/master/tools/bgapi_trace#recording-bgapi-traces-to-a-file-and-converting-the-log-to-text-format).**

After following these steps, the user should have a decoded BGAPI-trace. The BGAPI-trace contains stack events and data that is irrelevant for CS, so the file has to be parsed. Examples of decoded BGAPI-traces can be found in the [example_logs](./example_logs/) folder.

## Parsing the decoded BGAPI-trace

The BGAPI-trace contains all the BLE-stack events from the initiator. In the file, the relevant events for CS are:

1. `bt_evt_cs_result` – Contains CS step data from the initiator.
2. `bt_evt_cs_result_continue` – Contains additional CS step data, if the step data did not fit in one event.
3. `bt_evt_gatt_characteristic_value` – When using the CS SoC sample applications, characteristic 29 contains the reflector CS step data, read using the ranging service (RAS).

> [!NOTE]  
> Reflector (RAS) step data may interleave with CS initiator events in the log.
> The parser determines the end of reflector data exclusively from
> "RAS - real-time reception finished" log messages.

The parser parses the log line-for-line, extracting the CS step data into chunks. Each chunk is then further parsed as follows:

Let's say the following chunk of CS step data was collected:
```text
000b0500d901ea0200460500d802e80200210500d501e802022d151394b1ec03ebdf39036126d5033cc2180300000013
```

If we start from the first byte, we can see that:

```text
Step_Mode = 0x00
Step_Channel = 0x0b
Step_Data_Length = 0x05
```

If we move 5 bytes forward in the array, we get to the next header and use that to parse the next step. When doing this for the whole chunk, we get the following structure for the step data:

```text
00 0b 05 00 d9 01 ea 02
00 46 05 00 d8 02 e8 02
00 21 05 00 d5 01 e8 02
02 2d 15 13 94 b1 ec 03 eb df 39 03 61 26 d5 03 3c c2 18 03 00 00 00 13
```

As can be seen, the chunk has 3 mode-0 steps with data length 5, and 1 mode-2 steps with data length 0x15 = 21.

### Parser script

The [parser script](./step_data_parser.py) is intended to work as an example on how to parse the raw step data into JSON-format. The script parses the step data based on *BLE Core Specification Version 6.0 | Vol 4, Part E Section 7.7.65.44* and the *BLE Ranging Service V1.0 section 3.2.1*.

To use the script, start by installing the required Python packages from *requirements.txt*:

```bash
pip install -r requirements.txt
```

Once the installation is finished, the script can be started:

```bash
python step_data_parser.py <input.txt> <output.json>
```

> [!TIP]  
> To test the script, use the included example logs as inputs (for example [example_log_rtt.txt](./example_logs/example_pbr_input.txt)). The output should look like the included example outputs (for example [example_output_rtt.txt](./example_logs/example_pbr_output.json)).

## Repository structure

```text
.
├── step_data_parser.py      # Reference parser script
├── README.md                # Usage instructions and background
├── requirements.txt         # Python dependencies
├── example_logs/            # Example decoded logs and parsed JSON files
│   ├── example_pbr_input.txt
│   ├── example_pbr_output.json
│   └── ...
└── step_data/               # Role-specific classes used by the parser
    ├── InitiatorData.py
    └── ...
```
