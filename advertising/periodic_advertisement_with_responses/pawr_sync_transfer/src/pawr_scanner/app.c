/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
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
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "app.h"
#include "gatt_db.h"
#include "app_log.h"

#define PAWR_MAX_SKIP   0
#define PAWR_TIMEOUT    1000

static uint8_t advertising_set_handle = 0xff;
static uint8_t pawr_slot_number = 0;
static uint8_t conn_handle;
static uint8_t resp_data = 0;

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

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:

      sc = sl_bt_past_receiver_set_default_sync_receive_parameters(sl_bt_past_receiver_mode_synchronize,
                                                                   PAWR_MAX_SKIP, PAWR_TIMEOUT, sl_bt_sync_report_all);
      app_assert_status(sc);

      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);
      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);
      sc = sl_bt_sm_configure(0, sl_bt_sm_io_capability_noinputnooutput);
      app_assert_status(sc);
      sc = sl_bt_sm_delete_bondings();
      app_assert_status(sc);

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);
      // Start advertising and enable connections.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_advertiser_connectable_scannable);
      app_assert_status(sc);

      app_log("Started advertising\r\n");
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      app_log("Connection opened\r\n");
      conn_handle = evt->data.evt_connection_opened.connection;
      break;

    case sl_bt_evt_sm_confirm_bonding_id:
      sl_bt_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 1);
      break;

    case sl_bt_evt_connection_parameters_id:
      app_log("Security level: %d\r\n", evt->data.evt_connection_parameters.security_mode);
      break;

    case sl_bt_evt_sm_bonded_id:
      app_log("Bonding done, ID: %d\r\n", evt->data.evt_sm_bonded.bonding);
      break;

    case sl_bt_evt_sm_bonding_failed_id:
      app_log("Bonding failed, reason: %x\r\n", evt->data.evt_sm_bonding_failed.reason);
      break;

    case sl_bt_evt_gatt_server_attribute_value_id:
      if (gattdb_pawr_sync_char == evt->data.evt_gatt_server_attribute_value.attribute) {
        pawr_slot_number = evt->data.evt_gatt_server_attribute_value.value.data[0];
        app_log("Response slot number received: %d\r\n", pawr_slot_number);
      }
      break;

    case sl_bt_evt_pawr_sync_transfer_received_id:
      app_log("PAwR sync transfer received, closing connection \r\n");
      sc = sl_bt_connection_close(conn_handle);
      app_assert_status(sc);
      break;

    case sl_bt_evt_pawr_sync_subevent_report_id:
      app_log("PAwR subevent report. Data: %d\r\n", evt->data.evt_pawr_sync_subevent_report.data.data[0]);
      sc = sl_bt_pawr_sync_set_response_data(evt->data.evt_pawr_sync_subevent_report.sync,
                                             evt->data.evt_pawr_sync_subevent_report.event_counter,
                                             evt->data.evt_pawr_sync_subevent_report.subevent,
                                             evt->data.evt_pawr_sync_subevent_report.subevent,
                                             pawr_slot_number, sizeof(resp_data), &resp_data);
      app_assert_status(sc);
      resp_data++;
      break;

    case sl_bt_evt_sync_closed_id:
      app_log("Sync lost, reason: %x\r\n", evt->data.evt_sync_closed.reason);
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);
      // Restart advertising after sync is lost
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_advertiser_connectable_scannable);
      app_assert_status(sc);
      break;

    case sl_bt_evt_connection_closed_id:
      app_log("Connection closed, reason: %d \r\n", evt->data.evt_connection_closed.reason);
      break;

    default:
      //app_log("Unhandled event: %x\r\n", SL_BT_MSG_ID(evt->header));
      break;
  }
}
