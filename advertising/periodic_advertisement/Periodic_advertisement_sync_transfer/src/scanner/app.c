/***************************************************************************//**
 * @file scanner
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "app.h"
#include "sl_bt_api.h"
#include "app_log.h"

#define SYNC_TIMEOUT 1000 // effective value SYNC_TIMEOUT*10 ms

// UUID of periodic synchronous service used to identify the advertisement of the target device
const uint8_t periodicSyncService[16] = { 0x81, 0xc2, 0x00, 0x2d, 0x31, 0xf4, 0xb0, 0xbf, 0x2b, 0x42, 0x49, 0x68, 0xc7, 0x25, 0x71, 0x41 };

// Parse advertisements looking for advertised periodicSync Service.
static uint8_t findServiceInAdvertisement(uint8_t *data, uint8_t len)
{
  uint8_t adFieldLength;
  uint8_t adFieldType;
  uint8_t i = 0;
  // Parse advertisement packet
  while (i < len) {
    adFieldLength = data[i];
    adFieldType = data[i + 1];
    // found incomplete/complete list of 128-bit Service UUIDs as defined in the assgigned numbers SIG document.
    if (adFieldType == 0x06 || adFieldType == 0x07) {
      // compare  UUID to periodic synchronous service UUID
      if (memcmp(&data[i + 2], periodicSyncService, 16) == 0) {
        return 1;
      }
    }
    // advance to the next AD struct
    i = i + adFieldLength + 1;
  }
  return 0;
}

/**************************************************************************//**
 * Application Init.
 *****************************************************************************/
SL_WEAK void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////
}

/**************************************************************************//**
 * Application Process Action.
 *****************************************************************************/
SL_WEAK void app_process_action(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application code here!                              //
  // This is called infinitely.                                              //
  // Do not call blocking functions from here!                               //
  /////////////////////////////////////////////////////////////////////////////
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the dummy weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;
  static uint8_t connection_handle;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id: {
      bd_addr address;
      uint8_t address_type;
      // Extract unique ID from BT Address.
      sc = sl_bt_system_get_identity_address(&address, &address_type);
      app_assert_status(sc);

      app_log_info("Bluetooth %s address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   address_type ? "static random" : "public device",
                   address.addr[5],
                   address.addr[4],
                   address.addr[3],
                   address.addr[2],
                   address.addr[1],
                   address.addr[0]);

      sc = sl_bt_past_receiver_set_default_sync_receive_parameters(sl_bt_past_receiver_mode_synchronize, 0, SYNC_TIMEOUT, sl_bt_sync_report_all);
      app_assert_status(sc);

      // Scan in intervals of 125ms for 125ms time window
      sc = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_passive, 200, 200);
      app_assert_status(sc);

      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m,
                               sl_bt_scanner_discover_observation);
      app_assert_status(sc);
      break;
    }

    // -------------------------------
    // This event indicates the device has received extended advertisements PDU
    case sl_bt_evt_scanner_extended_advertisement_report_id:
      if (findServiceInAdvertisement(&(evt->data.evt_scanner_extended_advertisement_report.data.data[0]), evt->data.evt_scanner_extended_advertisement_report.data.len) != 0) {
        app_log_info("found periodic sync service, attempting to open connection\r\n");
        sc = sl_bt_connection_open(evt->data.evt_scanner_extended_advertisement_report.address, evt->data.evt_scanner_extended_advertisement_report.address_type, sl_bt_gap_phy_1m, &connection_handle);
        app_assert_status(sc);
        /* now that connection is open, we can stop scanning*/
        sc = sl_bt_scanner_stop();
        app_assert_status(sc);
      }
      break;

    // -------------------------------
    // This event indicates the a connection was opened
    case sl_bt_evt_connection_opened_id: {
      bd_addr peer_add = evt->data.evt_connection_opened.address;
      app_log_info("connection open with peer device %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   peer_add.addr[5], peer_add.addr[4], peer_add.addr[3],
                   peer_add.addr[2], peer_add.addr[1], peer_add.addr[0]);
      break;
    }

    // -------------------------------
    // This event indicates that a new connection was closed.
    case sl_bt_evt_connection_closed_id:
      app_log_info("connection with handle %02X closed\r\n", connection_handle);
      break;

    // -------------------------------
    // This event indicates the device has synchronized successfully to the perodic advertisement after receiving PAST
    case sl_bt_evt_periodic_sync_transfer_received_id:
      app_log_info("syncing to periodic advertisement result: %02X\r\n", evt->data.evt_periodic_sync_transfer_received.status);
      sc = sl_bt_connection_close(evt->data.evt_periodic_sync_transfer_received.connection);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates the synchornizatiion has been terminated
    case sl_bt_evt_sync_closed_id:
      app_log_info("periodic sync closed. reason 0x%04X, sync handle 0x%04X\r\n",
                   evt->data.evt_sync_closed.reason,
                   evt->data.evt_sync_closed.sync);
      /* restart discovery */
      sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m,
                          sl_bt_scanner_discover_observation);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates the device has received a periodic advertisements PDU
    case sl_bt_evt_periodic_sync_report_id:
      app_log_info("Periodic advertisemenet payload:  ");
      for (int i = 0; i < evt->data.evt_periodic_sync_report.data.len; i++) {
        app_log("%02X,", evt->data.evt_periodic_sync_report.data.data[i]);
      }
      app_log("\r\n");
      break;

      ///////////////////////////////////////////////////////////////////////////
      // Add additional event handlers here as your application requires!      //
      ///////////////////////////////////////////////////////////////////////////
  }
}
