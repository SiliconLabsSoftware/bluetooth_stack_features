/***************************************************************************//**
 * @file
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
#include "gatt_db.h"
#include "app.h"

#include "sl_bt_api.h"
#include "app_log.h"

#define SCAN_TIMEOUT        327680 //10 sec
#define SIGNAL_SCAN_TIMEOUT 1

sl_sleeptimer_timer_handle_t sleep_timer_handle;
void sleeptimer_callback(sl_sleeptimer_timer_handle_t *handle, void *data);

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
  bd_addr address;
  uint8_t address_type;
  uint8_t system_id[8];

  uint8_t app_connection;
  bd_addr *remote_address;
  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:

      // Extract unique ID from BT Address.
      sc = sl_bt_system_get_identity_address(&address, &address_type);
      app_assert_status(sc);

      // Pad and reverse unique ID to get System ID.
      system_id[0] = address.addr[5];
      system_id[1] = address.addr[4];
      system_id[2] = address.addr[3];
      system_id[3] = 0xFF;
      system_id[4] = 0xFE;
      system_id[5] = address.addr[2];
      system_id[6] = address.addr[1];
      system_id[7] = address.addr[0];

      sc = sl_bt_gatt_server_write_attribute_value(gattdb_system_id,
                                                   0,
                                                   sizeof(system_id),
                                                   system_id);
      app_assert_status(sc);

      app_log_info("System Boot\r\n");

      sc = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_passive, 200, 200);
      app_assert_status(sc);

      // start scanning
      sl_bt_scanner_start(sl_bt_scanner_scan_phy_coded,
                          sl_bt_scanner_discover_observation);

      // Start a timer for checking scan timeout
      sc = sl_sleeptimer_start_timer(&sleep_timer_handle, SCAN_TIMEOUT, sleeptimer_callback, (void*)NULL, 0, 0);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      sl_bt_scanner_stop();
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      sl_bt_scanner_start(sl_bt_scanner_scan_phy_coded,
                          sl_bt_scanner_discover_observation);
      break;

    case sl_bt_evt_system_external_signal_id:
      if (evt->data.evt_system_external_signal.extsignals == SIGNAL_SCAN_TIMEOUT) {
        sl_bt_scanner_stop();
        app_log_info("no connectable devices in range\r\n");
      }
      break;

    case sl_bt_evt_scanner_extended_advertisement_report_id:
      // there is no filter here because the LE Coded PHY device is rare
      // add the filter if it is needed
      remote_address = &(evt->data.evt_scanner_extended_advertisement_report.address);
      app_log_info("advertisement/scan response from %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\r\n",
                   remote_address->addr[5],
                   remote_address->addr[4],
                   remote_address->addr[3],
                   remote_address->addr[2],
                   remote_address->addr[1],
                   remote_address->addr[0]);

      app_log_info("RSSI %d\r\n", evt->data.evt_scanner_extended_advertisement_report.rssi);

      app_log_info("data len: %d\r\n", evt->data.evt_scanner_extended_advertisement_report.data.len);

      app_log_info("data raw: \r\n");
      for (int i = 0; i < evt->data.evt_scanner_extended_advertisement_report.data.len; i++) {
        app_log("%c ", evt->data.evt_scanner_extended_advertisement_report.data.data[i]);
      }
      app_log("\r\n");
      app_log_info("data hex: \r\n");
      for (int i = 0; i < evt->data.evt_scanner_extended_advertisement_report.data.len; i++) {
        app_log("%x ", evt->data.evt_scanner_extended_advertisement_report.data.data[i]);
      }
      app_log("\r\n");

      //stop the tracking timer
      sl_sleeptimer_stop_timer(&sleep_timer_handle);

      sl_bt_scanner_stop();
      sc = sl_bt_connection_open(*remote_address,
                                 sl_bt_gap_public_address,
                                 sl_bt_gap_phy_coded,
                                 &app_connection);
      app_assert_status(sc);
      break;

    case sl_bt_evt_connection_phy_status_id:
      app_log_info("using PHY %d\r\n", evt->data.evt_connection_phy_status.phy);
      break;

    ///////////////////////////////////////////////////////////////////////////
    // Add additional event handlers here as your application requires!      //
    ///////////////////////////////////////////////////////////////////////////

    // -------------------------------
    // Default event handler.
    default:
      break;
  }
}

/***************************************************************************//**
 * Sleeptimer callback
 *
 * Note: This function is called from interrupt context
 *
 * @param[in] handle Handle of the sleeptimer instance
 * @param[in] data  Callback data
 ******************************************************************************/
void sleeptimer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;

  sl_bt_external_signal(SIGNAL_SCAN_TIMEOUT);
}
