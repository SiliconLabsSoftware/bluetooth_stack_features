/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "app.h"
#include "sl_iostream.h"
#include "sl_iostream_handles.h"
#include "sl_cli.h"
#include "cli_functions.h"
#include "string.h"
// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;

// Current connection handle
static uint8_t connection = 0;

static uint8_t bonding = SL_BT_INVALID_BONDING_HANDLE;

static enum {
  IDLE, ADVERTISING, SCANNING, CONNECTING, CONNECTED, DISCONNECTING
} state;

static bd_addr peer_address;
static uint8_t peer_address_type;

static bool filtering = false;

#define SCAN_RESULT_SIZE 10

struct {
  bd_addr address; /**< Advertiser address */
  uint8_t address_type;
} scan_result[SCAN_RESULT_SIZE];

static int scan_result_count = 0;

#define RSSI_THRESHOLD (-85)

// Add a device to the scan result list.
// Returns -1 if already in the list or capacity exceeded. Otherwise the index
// of the added device
int add_to_list(sl_bt_evt_scanner_legacy_advertisement_report_t *p)
{
  if (scan_result_count < SCAN_RESULT_SIZE) {
    int i;
    for (i = 0; i < scan_result_count; i++) {
      if (!memcmp(&p->address, &scan_result[i].address, sizeof(bd_addr))
          && p->address_type == scan_result[i].address_type) {
        return -1;
      }
    }
    scan_result[scan_result_count].address = p->address;
    scan_result[scan_result_count].address_type = p->address_type;
    return scan_result_count++;
  } else {
    return -1;
  }
}

// Application Init.
void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////
}

// Application Process Action.
void app_process_action(void)
{
  if (app_is_process_required()) {
    /////////////////////////////////////////////////////////////////////////////
    // Put your additional application code here!                              //
    // This is will run each time app_proceed() is called.                     //
    // Do not call blocking functions from here!                               //
    /////////////////////////////////////////////////////////////////////////////
  }
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);
      sc = sl_bt_sm_configure(0x02, sl_bt_sm_io_capability_keyboarddisplay);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      peer_address = evt->data.evt_connection_opened.address;
      peer_address_type = evt->data.evt_connection_opened.address_type;
      if (peer_address_type == 4) {
        peer_address_type = 0;
      } else if (peer_address_type == 5) {
        peer_address_type = 1;
      }

      connection = evt->data.evt_connection_opened.connection;
      bonding = evt->data.evt_connection_opened.bonding;
      if (bonding != SL_BT_INVALID_BONDING_HANDLE) {
        sl_iostream_printf(sl_iostream_recommended_console_stream,
                           "Bonding id %d\r\n> ", bonding);
        sl_bt_sm_get_bonding_details(bonding, &peer_address,
                                     &peer_address_type, NULL, NULL);
      }
      state = CONNECTED;
      sl_iostream_printf(sl_iostream_recommended_console_stream,
                         "\r\nConnected\r\n> ");
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      state = IDLE;
      sl_iostream_printf(sl_iostream_recommended_console_stream,
                         "\r\nDisconnected.\r\nNot advertising.\r\n> ");
      break;

    // This event indicates a received advertisement.
    case sl_bt_evt_scanner_legacy_advertisement_report_id:
      if (evt->data.evt_scanner_legacy_advertisement_report.event_flags
          & SL_BT_SCANNER_EVENT_FLAG_CONNECTABLE) {
        int index;
        bd_addr *addr =
          &evt->data.evt_scanner_legacy_advertisement_report.address;

        if (evt->data.evt_scanner_legacy_advertisement_report.rssi
            >= RSSI_THRESHOLD
            && (index = add_to_list(
                  &evt->data.evt_scanner_legacy_advertisement_report)) != -1) {
          sl_iostream_printf(
            sl_iostream_recommended_console_stream,
            "[%d] %02x:%02x:%02x:%02x:%02x:%02x (type %d, RSSI=%d)\r\n",
            index,
            addr->addr[5],
            addr->addr[4],
            addr->addr[3],
            addr->addr[2],
            addr->addr[1],
            addr->addr[0],
            evt->data.evt_scanner_legacy_advertisement_report.address_type,
            evt->data.evt_scanner_legacy_advertisement_report.rssi);
          if (evt->data.evt_scanner_legacy_advertisement_report.address_type
              == sl_bt_gap_random_resolvable_address) {
            sc = sl_bt_sm_resolve_rpa(*addr, &peer_address,
                                      &peer_address_type, NULL);
            if (sc == SL_STATUS_OK) {
              sl_iostream_printf(
                sl_iostream_recommended_console_stream,
                "Resolved to %02x:%02x:%02x:%02x:%02x:%02x (type %d)\r\n",
                peer_address.addr[5], peer_address.addr[4],
                peer_address.addr[3], peer_address.addr[2],
                peer_address.addr[1], peer_address.addr[0],
                peer_address_type);
            }
          }
          if (index == SCAN_RESULT_SIZE - 1) {
            sc = sl_bt_scanner_stop();
            app_assert_status(sc);
            state = IDLE;
            sl_iostream_printf(sl_iostream_recommended_console_stream,
                               "Scanning stopped.\r\n");
          }
        }
      }
      break;

    case sl_bt_evt_sm_bonded_id:
      sl_iostream_printf(sl_iostream_recommended_console_stream,
                         "Bonding handle: %d, Security mode %d.\r\n",
                         evt->data.evt_sm_bonded.bonding,
                         evt->data.evt_sm_bonded.security_mode);
      bonding = evt->data.evt_sm_bonded.bonding;
      if (peer_address_type == sl_bt_gap_random_resolvable_address) {
        sl_bt_sm_get_bonding_details(bonding, &peer_address,
                                     &peer_address_type, NULL, NULL);
        sl_bt_resolving_list_add_device_by_bonding(
          bonding, sl_bt_resolving_list_privacy_mode_network);
      }
      break;
#if 0
    case sl_bt_evt_gatt_server_characteristic_status_id:
      sl_iostream_printf(sl_iostream_recommended_console_stream,
                         "characteristic[%d]\r\n",
                         evt->data.evt_gatt_server_characteristic_status.characteristic);
      break;
#endif
    // -------------------------------
    // Default event handler. The message ID is printed
    default:
      sl_iostream_printf(sl_iostream_recommended_console_stream, "[%08lx]\r\n",
                         SL_BT_MSG_ID(evt->header));
      break;
  }
}

// Print the error message of invalid state with the current state.
static void invalid_state(void)
{
  sl_iostream_printf(sl_iostream_recommended_console_stream,
                     "Invalid state: %d\r\n", state);
}

// Start/stop scanning
void scan(sl_cli_command_arg_t *arguments) /* 1=start, 0=stop and show result */
{
  sl_status_t sc;
  bool enable = sl_cli_get_argument_uint8(arguments, 0);

  if (state == IDLE && enable) {
    scan_result_count = 0;
    sc = sl_bt_scanner_set_parameters_and_filter(
      sl_bt_scanner_scan_mode_passive,
      160,
      160,
      0,
      filtering
      ? sl_bt_scanner_filter_policy_basic_filtered
      : sl_bt_scanner_filter_policy_basic_unfiltered);
    app_assert_status(sc);

    sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m,
                             sl_bt_scanner_discover_generic);
    app_assert_status(sc);

    state = SCANNING;
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Scanning started.\r\n");
  } else if (state == SCANNING && !enable) {
    sc = sl_bt_scanner_stop();
    app_assert_status(sc);
    state = IDLE;
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Scanning stopped.\r\n");
  } else {
    invalid_state();
  }
}

// Connect one of the devices in the scan result.
void connect(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc = SL_STATUS_OK;
  (void) arguments;
  if (state == IDLE) {
    int index = sl_cli_get_argument_uint8(arguments, 0);
    if (index < scan_result_count) {
      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);

      sc = sl_bt_connection_open(scan_result[index].address,
                                 scan_result[index].address_type,
                                 sl_bt_gap_phy_1m, &connection);
      if (sc == SL_STATUS_OK) {
        sl_iostream_printf(sl_iostream_recommended_console_stream,
                           "Connecting device [%d].\r\n", index);
        state = CONNECTING;
      } else {
        app_assert_status(sc);
      }
    }
  } else {
    invalid_state();
    return;
  }
}

// Pair with the connected device
void pair(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc = SL_STATUS_OK;
  (void) arguments;

  if (state == CONNECTED) {
    sc = sl_bt_sm_increase_security(connection);
    app_assert_status(sc);
  } else {
    invalid_state();
    return;
  }
}

// Start/stop advertising
void advertise(sl_cli_command_arg_t *arguments) /* 1=start, 0=stop */
{
  sl_status_t sc;
  bool enable = sl_cli_get_argument_uint8(arguments, 0);
  if (state == IDLE && enable) {
    // Generate data for advertising
    sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                               sl_bt_advertiser_general_discoverable);
    app_assert_status(sc);

    // Set advertising interval to 100ms.
    sc = sl_bt_advertiser_set_timing(
      advertising_set_handle,
      160,   // min. adv. interval (milliseconds * 1.6)
      160,   // max. adv. interval (milliseconds * 1.6)
      0,     // adv. duration
      0);    // max. num. adv. events
    app_assert_status(sc);

    sl_bt_advertiser_configure(
      advertising_set_handle,
      filtering ? SL_BT_ADVERTISER_USE_FILTER_FOR_SCAN_REQUESTS
      | SL_BT_ADVERTISER_USE_FILTER_FOR_CONNECTION_REQUESTS
      : 0);

    // Start advertising and enable connections.
    sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                       sl_bt_legacy_advertiser_connectable);
    app_assert_status(sc);

    state = ADVERTISING;
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Advertising started.\r\n");
  } else if (state == ADVERTISING && !enable) {
    sl_bt_advertiser_stop(advertising_set_handle);
    state = IDLE;
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Advertising stopped.\r\n");
  } else {
    invalid_state();
  }
}

// Add the currently connected device to the Filter Accept List.
void add_peer(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc = SL_STATUS_OK;
  (void) arguments;
  if (state == CONNECTED) {
#if 0
    if (bonding != SL_BT_INVALID_BONDING_HANDLE) {
      sc = sl_bt_accept_list_add_device_by_bonding(bonding);
    } else
#endif
    sc = sl_bt_accept_list_add_device_by_address(peer_address,
                                                 peer_address_type);

    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "%02x:%02x:%02x:%02x:%02x:%02x (type %d)\r\n",
                       peer_address.addr[5], peer_address.addr[4],
                       peer_address.addr[3], peer_address.addr[2],
                       peer_address.addr[1], peer_address.addr[0],
                       peer_address_type);

    if (sc == SL_STATUS_OK) {
      sl_iostream_printf(
        sl_iostream_recommended_console_stream,
        "Connected device added to the Filter Accept List.\r\n");
    } else if (sc == SL_STATUS_BT_CTRL_MEMORY_CAPACITY_EXCEEDED) {
      sl_iostream_printf(sl_iostream_recommended_console_stream,
                         "Memory capacity exceeded.\r\n");
    } else {
      sl_iostream_printf(sl_iostream_recommended_console_stream,
                         "Operation failed with reason 0x%lx.\r\n", sc);
    }
  } else {
    invalid_state();
  }
}

// Add one the devices in the scan result to the Filter Accept List.
void add_discovered(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc = SL_STATUS_OK;
  (void) arguments;
  if (state == IDLE) { // add the specified index to filter list
    int index = sl_cli_get_argument_uint8(arguments, 0);
    if (index < scan_result_count) {
      sc = sl_bt_accept_list_add_device_by_address(
        scan_result[index].address, scan_result[index].address_type);
      if (sc == SL_STATUS_OK) {
        sl_iostream_printf(
          sl_iostream_recommended_console_stream,
          "Discovered device [%d] added to the Filter Accept List.\r\n",
          index);
      } else if (sc == SL_STATUS_BT_CTRL_MEMORY_CAPACITY_EXCEEDED) {
        sl_iostream_printf(sl_iostream_recommended_console_stream,
                           "Memory capacity exceeded.\r\n");
      } else {
        sl_iostream_printf(sl_iostream_recommended_console_stream,
                           "Operation failed with reason 0x%lx.\r\n", sc);
      }
    }
  } else {
    invalid_state();
    return;
  }
}

// Empty the Filter Accept List.
void clear(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc;
  (void) arguments;
  if (state == IDLE || state == CONNECTED) {
    sc = sl_bt_accept_list_remove_all_devices();
    app_assert_status(sc);
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Filter Accept List empty.\r\n");
  } else {
    invalid_state();
  }
}

// Enable/disable filtering
void filter(sl_cli_command_arg_t *arguments) /* 1=start, 0=stop */
{
  if (state == IDLE || state == CONNECTED) {
    filtering = sl_cli_get_argument_uint8(arguments, 0);
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Filtering set to: %d\r\n", filtering);
  } else {
    invalid_state();
  }
}

// Close the current connection.
void disconnect(sl_cli_command_arg_t *arguments)
{
  (void) arguments;
  if (state == CONNECTED) {
    sl_bt_connection_close(connection);
    sl_iostream_printf(sl_iostream_recommended_console_stream,
                       "Disconnecting...\r\n");
  } else {
    invalid_state();
  }
}

// List the current bondings
void bondings(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc = SL_STATUS_OK;
  (void) arguments;
  if (state == IDLE) {
    uint32_t i;
    for (i = 0; i < 32; i++) {
      sc = sl_bt_sm_get_bonding_details(i, &peer_address,
                                        &peer_address_type, NULL, NULL);
      if (sc == SL_STATUS_OK) {
        sl_iostream_printf(
          sl_iostream_recommended_console_stream,
          "[%d] %02x:%02x:%02x:%02x:%02x:%02x (type %d)\r\n", (int) i,
          peer_address.addr[5], peer_address.addr[4],
          peer_address.addr[3], peer_address.addr[2],
          peer_address.addr[1], peer_address.addr[0], peer_address_type);
      }
    }
  } else {
    invalid_state();
  }
}

// Unpair one the devices in the bonding database.
void unpair(sl_cli_command_arg_t *arguments)
{
  sl_status_t sc = SL_STATUS_OK;
  (void) arguments;
  if (state == IDLE) {
    int index = sl_cli_get_argument_uint8(arguments, 0);
    sl_bt_resolving_list_remove_device_by_bonding(index);
    sc = sl_bt_sm_delete_bonding(index);
    app_assert_status(sc);
  } else {
    invalid_state();
    return;
  }
}
