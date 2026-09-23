# Bluetooth - SoC Filter Accept List #

## Overview ##

The Filter Accept List is a feature that filters out devices not in the accept list during scanning or connection establishment. This example demonstrates the use of Filter Accept List in various scenarios.

Peripherals used: EUSART/vcom.

## SDK version ##

- SiSDK v2026.6.0

## Hardware Required ##

Any Bluetooth-capable EFR32 series 2 device.

## Connections Required ##

Connect the board to your PC via a micro-USB cable to flash the example and use the virtual serial port for the CLI.

## Setup ##
To test this application, you can either create a project based on this example or start with a Bluetooth - SoC Empty example project.

### Create a project based on the example in Simplicity Studio ###

1. Make sure that this repository is cloned locally and added to [SDK extensions](https://docs.silabs.com/ssv6ug/latest/ssv6-install-sdk-extensions/).

2. From **Devices**, select the connected device, and click on the **EXAMPLE PROJECTS & DEMOS** tab. You can find this example project by setting filtering keyword **'filter'**.

3. Click on the **Create** button on the **Bluetooth - SoC Filter Accept List** example. Example project creation dialog pops up -> click **Finish** and the project should be generated.

4. Open the project in the IDE, build and flash this example to your device.

### Start with a Bluetooth - SoC Empty application example project ###

1. Create a new "Bluetooth - SoC Empty" project
2. In Simplicity Studio, open the .slcp file in the project folder, and add these software components:
                a. Services -> Command Line Interface -> CLI Instance. Change EOL to \r\n in CLI Global Configuration (optional).
                b. Bluetooth -> Bluetooth LE Controller (Link Layer) -> Features with Commands and Events -> Device filtering with Bluetooth controller's Filter Accept List.
                c. Bluetooth -> Bluetooth LE Controller (Link Layer) -> Additional Features -> Address resolving with Bluetooth Controller's Resolving List.
                d. Bluetooth -> Bluetooth Host (Stack) -> Additional Features -> Select API semantics that use accurate Bluetooth address types
				e. In Platform -> Board -> Board Control, enable **Enable Virtual COM UART**.
3. Copy the "template_contribution" section of the included soc_filter_accept_list.slcp file and paste it into the generated .slcp file in your project folder.
4. Copy the included cli_functions.h file to the project folder.
5. Copy the included app.c file to the project folder.
6. In Simplicity Studio, open the .slcp file in the project folder, navigate to the Overview panel, click the three dots in Project Details, and click "Force Generation".
7. Open the project in the IDE, build and flash this example to your device.

## How It Works ##

See the API documentation for details.

## Testing ##

### Test environment requirement ###

A pair of BLE-capable EFR32 devices.

### Commands available in the CLI ###

scan {1|0}

Start scanning (1) or stop scanning (0). A list of up to 10 devices will be displayed, with indices 0-9 in brackets. Scanning is stopped when the number of listed devices reaches 10, or if the 'scan 0' command is issued. Each time a 'scan 1' command is issued, the list will be cleared internally first. When scanning is stopped, the device is in the idle state.

connect {n}

Connect to the device at the specified index in the scan result list. This command can be issued only in the idle state.

disconnect

Close the current connection. This command can be issued only in the connected state. After disconnection, the device is in the idle state.

advertise {1|0}

Start (1) or stop (0) advertising. If advertising is started, connection can be established from a scanner. The device is in the idle state if advertising is stopped.

add_discovered {n}

Add the device with index n in the scan result to the filter accept list. This can be done only when in the idle state.

clear

Clear the filter accept list.

add_peer

Add the current connected device to the filter accept list.

pair

Pair with the currently connected device.

bondings

List the currently paired devices. This command can only be issued in the idle state. An index is associated with each device and it can be used in the 'unpair' command.

unpair {n}

Unpair the device with index n in the bonding database. The device will also be removed from the resolving list. The filter entry for this device, if it uses an RPA, should not be deemed valid after this.

filter {1|0}

Enable (1) or disable (0) the Filter Accept List. This can be done only in the idle state.

### Test procedures ###

I. Scanning filter

1. 'scan 1'.
2. 'scan 0' if not automatically stopped.
3. 'add_discovered {n}'
4. Repeat the above until all devices are added.
5. 'filter 1'
6. 'scan 1'
7. Only the added devices will be visible.
8. 'filter 0'
9. Other devices should be visible after this.
10. 'clear' to empty the filter accept list.

II. Central connection filter

1. 'scan 1'.
2. 'scan 0' if not automatically stopped.
3. 'add_discovered {n}'
4. Repeat the above until all devices are added.
5. 'filter 1'
6. 'connect {n}'
7. The above command should work with devices added to the filter accept list.
8. 'disconnect' from either side.
9. 'filter 0'
10. Verify if other devices can be connected now.
11. 'disconnect' if connected.
12. 'clear' to empty the filter accept list.

III. Peripheral connection filter with scanned device

1. Add a device from a scan result to the filter accept list (I. 1~5)
2. 'advertise 1'
3. Wait for the central device to connect.
4. Only the devices in the filter accept list can connect to this device.
5. 'disconnect' if connected.
6. 'filter 0'
7. 'advertise 1'
8. Verify that other devices can also connect to this device.
9. 'disconnect' if connected.
10. 'clear' to empty the filter accept list.

IV. Peripheral connection filter with formerly connected device

1. 'advertise 1'
2. Wait for the remote central device to connect.
3. Once connected, 'add_peer' to add the connected device to the filter accept list.
4. 'disconnect' and repeat the above steps to add more devices.
5. 'filter 1'
6. 'advertise 1'
7. Wait for the remote central device to connect: Only devices in the filter accept list can establish the connection.
8. 'disconnect' if connected
9. 'filter 0'
10. Verify that other devices can also connect to this device.
11. 'disconnect' if connected.
12. 'clear' to empty the filter accept list.

V. Testing RPA (Requiring a mobile phone with RPA, like an iPhone)

1. 'advertise 1'
2. Scan the device from the mobile phone.
3. Establish a connection from the mobile phone.
4. Make sure that the peer device is using RPA (type = 4), and run 'pair'.
5. 'add_peer' to add the identity address of the connected device to the filter accept list
6. 'disconnect' and add more devices if needed.
7. 'filter 1'
8. 'advertise 1'
9. Wait for the remote central device to connect: Only devices in the filter accept list can establish the connection.
10. 'disconnect' if connected.
11. (Optional) wait until the device using an RPA changes its RPA, then go to step 8.
12. 'filter 0'
13. Verify that other devices can also connect to this device.
14. 'disconnect' if connected.
15. 'clear' to empty the filter accept list.

## Limitations ##

1. Pairing is done in the minimalist way, which does not perform authentication.
2. If a connection is initiated towards an iPhone, it will be disconnected in a few seconds.
3. Only one device should be connected at any time for simplicity. This is not a hard limit of the stack and can be easily extended.
4. Unpairing a device does not automatically remove it from the filter accept list.
