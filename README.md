<table border="0">
  <tr>
    <td align="left" valign="middle">
    <h1>EFR32 Bluetooth Stack Feature Examples</h1>
  </td>
  <td align="left" valign="middle">
    <a href="https://www.silabs.com/wireless/bluetooth">
      <img src="http://pages.silabs.com/rs/634-SLU-379/images/WGX-transparent.png"  title="Silicon Labs Gecko and Wireless Gecko MCUs" alt="EFM32 32-bit Microcontrollers" width="250"/>
    </a>
  </td>
  </tr>
</table>

# Silicon Labs Bluetooth Stack Feature Examples #

This repo contains example projects which demonstrate the features of the Silicon Labs Bluetooth stack. The examples are categorized by the features that they demonstrate. These features are advertising, connections, GATT protocol, security, persistent storage, firmware upgrade, NCP, and system and performance.

## Examples ##

- Advertising
- Channel Sounding
- Connections
- GATT Protocol
- Security
- Filter Accept List
- Persistent Storage
- Firmware Upgrade
  - OTA for NCP Hosts
  - OTA from Windows
- NCP
- System and Performance
- Tools

## Libraries/Extensions ##

- Connection Manager


## Setup

### Using the repository as an external Repo
To generate GitHub example projects with Simplicity Studio (only applicable to repositories with slcp files):

1. Open Simplicity Studio and navigate to Window > Preferences > Simplicity Studio > External Repos.
2. Click Add.
3. In the URL field enter the repository's HTTPS clone address, for example https://github.com/SiliconLabs/bluetooth_stack_features.git
   Alternatively, clone the repository onto your hard drive, and provide the path to the .git folder in your local repository, for example
   C:\MyRepositories\bluetooth_stack_features\.git.
4. Enter an arbitrary name (such as 'Bluetooth Stack Features') and description for the repository, which will be displayed later.
5. Click Next. If you have entered the repository's clone address, Simplicity Studio will clone the repository for you.
6. Click Finish, then click Apply and Close.
7. If you are not on the Launcher perspective, open it from the Perspectives toolbar in the upper-right corner.
8. Select your board in the Debug Adapters view or in the My Products view.
9. On the General card, verify the Gecko SDK version and change if necessary.
   Note: Bluetooth stack feature examples are currently compatible with Gecko SDK v4.1 only!
10. Go to the Example Projects & Demos tab.
11. Now you should see your repository listed under the Provider filtering class. Select this filter.
    Note: The repository only shows up if it contains at least one example that is compatible with your device.
12. All the examples contained in the repository that are compatible with your device are displayed. Click Create on any of them to create a new example project. The example project installs all the software components necessary to demonstrate the given feature, and all the needed code is automatically copied into your project. Additional  configuration might be needed, so read the readme file of the example carefully.

![](doc\image\Studio.png)

### Using the repo as an SDK extension

The repo can be added to the Studio as an SDK Extension. If you use it this way, the sample applications will only be available when the corresponding SDK is selected. This will allow you to use see any provided components in the "Software Components" view.

1. Open Simplicity Studio and navigate to Window > Preferences > Simplicity Studio > SDK.
2. Select "Add Extension"

![](doc\image\SDK_Extension.png)

3. Select the repo location

![](doc\image\Add_Extension.png)

4. When using the Launcher, select the SDK with the extension:
![](doc\image\SDK_Selection.png)

You will be able to select the sample applications under "Example Projects & Demos" as usual, and now also the components will appear:
![](doc\image\Component.png)



## Documentation ##

Official documentation can be found at our [Developer Documentation](https://docs.silabs.com/bluetooth/latest/) page.

## Reporting Bugs/Issues and Posting Questions and Comments ##

To report bugs in the Application Examples projects, please create a new "Issue" in the "Issues" section of this repo. Please reference the board, project, and source files associated with the bug, and reference line numbers. If you are proposing a fix, also include information on the proposed fix. Since these examples are provided as-is, there is no guarantee that these examples will be updated to fix these issues.

Questions and comments related to these examples should be made by creating a new "Issue" in the "Issues" section of this repo.

## Disclaimer ##

The Gecko SDK suite supports development with Silicon Labs IoT SoC and module devices. Unless otherwise specified in the specific directory, all examples are considered to be EXPERIMENTAL QUALITY which implies that the code provided in the repos has not been formally tested and is provided as-is.  It is not suitable for production environments.  In addition, this code will not be maintained and there may be no bug maintenance planned for these resources. Silicon Labs may update projects from time to time.
