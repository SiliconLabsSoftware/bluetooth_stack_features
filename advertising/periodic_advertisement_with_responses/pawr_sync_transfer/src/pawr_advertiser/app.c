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
#include "sl_sleeptimer.h"

#define PAWR_INT_MIN              2400
#define PAWR_INT_MAX              2400
#define PAWR_NUM_SUBEVENTS        4
#define PAWR_SUBEVENT_INTERVAL    254
#define PAWR_RESPONSE_SLOT_DELAY  20

#define PAWR_NUM_MAX_SLOTS_PER_SUBEVENT 250
#define NUM_MAX_COUNTERS NUM_MAX_SUBEVENTS * NUM_MAX_SLOTS_PER_SUBEVENT
#define PAWR_SLOT_SPACING     3
#define LOG_BUFFER_ERRORS     0

typedef enum {
  scanning,
  opening,
  discover_services,
  discover_characteristics,
  provision,
  sync_transfer,
  running
} conn_state_t;

typedef struct {
  uint8_t conn_handle;
  uint32_t service_handle;
  uint16_t char_handle;
}conn_info_t;

static conn_state_t conn_state;
static conn_info_t  conn_info;

static const uint8_t pawr_sync_service[2] = { 0xAA, 0xAA };
static const uint8_t pawr_sync_char[2]    = { 0xBB, 0xBB };

static uint8_t slot_number = 0;
static const uint32_t pawr_flags = SL_BT_PERIODIC_ADVERTISER_INCLUDE_TX_POWER;
static uint8_t adv_handle = 0xFF;
static uint8_t subevent_data = 0;

#if LOG_BUFFER_ERRORS
static uint32_t allocation_failures = 0;
static uint32_t timestamp = 0;
#endif

static uint8_t find_service_in_advertisement(uint8_t *data, uint8_t len);
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
    case sl_bt_evt_system_boot_id:

      app_log("Boot event \r\n");
      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);
      sc = sl_bt_sm_configure(0, sl_bt_sm_io_capability_noinputnooutput);
      app_assert_status(sc);
      sc = sl_bt_sm_delete_bondings();
      app_assert_status(sc);
      sc = sl_bt_advertiser_create_set(&adv_handle);
      app_assert_status(sc);
      app_log("Starting PAwR train\r\n");
      sc = sl_bt_pawr_advertiser_start(adv_handle, PAWR_INT_MIN, PAWR_INT_MAX, pawr_flags,
                                       PAWR_NUM_SUBEVENTS, PAWR_SUBEVENT_INTERVAL, PAWR_RESPONSE_SLOT_DELAY,
                                       PAWR_SLOT_SPACING, PAWR_NUM_MAX_SLOTS_PER_SUBEVENT);
      app_assert_status(sc);

      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m, sl_bt_scanner_discover_generic);
      app_assert_status(sc);
      conn_state = scanning;
      break;

    case sl_bt_evt_scanner_legacy_advertisement_report_id:
      //Ignore scan responses when a connection is being opened
      if (conn_state == scanning) {
        if (evt->data.evt_scanner_legacy_advertisement_report.event_flags
            == (SL_BT_SCANNER_EVENT_FLAG_CONNECTABLE | SL_BT_SCANNER_EVENT_FLAG_SCANNABLE)) {
          if (find_service_in_advertisement(&(evt->data.evt_scanner_legacy_advertisement_report.data.data[0]),
                                            evt->data.evt_scanner_legacy_advertisement_report.data.len) != 0) {
            sc = sl_bt_scanner_stop();
            app_assert_status(sc);
            sc = sl_bt_connection_open(evt->data.evt_scanner_legacy_advertisement_report.address,
                                       evt->data.evt_scanner_legacy_advertisement_report.address_type,
                                       sl_bt_gap_phy_1m,
                                       &conn_info.conn_handle);
            app_assert_status(sc);
            conn_state = opening;
          }
        }
      }
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      conn_info.conn_handle = evt->data.evt_connection_opened.connection;
      sc = sl_bt_sm_increase_security(evt->data.evt_connection_opened.connection);
      app_assert_status(sc);
      break;

    case sl_bt_evt_sm_confirm_bonding_id:
      sl_bt_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 1);
      break;

    case sl_bt_evt_connection_parameters_id:
      if (evt->data.evt_connection_parameters.security_mode > 0) {
        sc = sl_bt_gatt_discover_primary_services_by_uuid(conn_info.conn_handle, sizeof(pawr_sync_service), pawr_sync_service);
        app_assert_status(sc);
        conn_state = discover_services;
      }
      break;

    case sl_bt_evt_gatt_service_id:
      if (!memcmp(evt->data.evt_gatt_service.uuid.data, pawr_sync_service, sizeof(pawr_sync_service) )) {
        conn_info.service_handle = evt->data.evt_gatt_service.service;
      }
      break;

    case sl_bt_evt_gatt_procedure_completed_id:
      if (discover_services == conn_state) {
        sc = sl_bt_gatt_discover_characteristics_by_uuid(conn_info.conn_handle, conn_info.service_handle, sizeof(pawr_sync_char), pawr_sync_char);
        app_assert_status(sc);
        conn_state = discover_characteristics;
      } else if (discover_characteristics == conn_state) {
        conn_state = provision;
        // assign a response slot number to the scanner
        sc = sl_bt_gatt_write_characteristic_value(evt->data.evt_gatt_characteristic.connection,
                                                   conn_info.char_handle, sizeof(slot_number), &slot_number);
        app_assert_status(sc);
        slot_number++;
      } else if (provision == conn_state) {
        app_log("Slot number written to the peripheral, result: %x\r\n", evt->data.evt_gatt_procedure_completed.result);
        conn_state = sync_transfer;
        // transfer the PAwR information to the scanner by using the PAST feature. Upon syncing to the train, the scanner will close the connection
        sc = sl_bt_advertiser_past_transfer(conn_info.conn_handle, 0, adv_handle);
        app_assert_status(sc);
        app_log("PAST transfer initiated \r\n");
      }
      break;

    case sl_bt_evt_sm_bonded_id:
      app_log("Bonding done: %d\r\n", evt->data.evt_sm_bonded.bonding);
      break;

    case sl_bt_evt_sm_bonding_failed_id:
      app_log("Bonding failed, reason: %x\r\n", evt->data.evt_sm_bonding_failed.reason);
      break;

    case sl_bt_evt_gatt_characteristic_id:
      if (!memcmp(evt->data.evt_gatt_characteristic.uuid.data, pawr_sync_char, sizeof(pawr_sync_char))) {
        conn_info.char_handle = evt->data.evt_gatt_characteristic.characteristic;
      }
      break;

    case sl_bt_evt_system_resource_exhausted_id:
#if LOG_BUFFER_ERRORS
      allocation_failures += evt->data.evt_system_resource_exhausted.num_buffer_allocation_failures;
      app_log("Resource exhausted event:\r\n   Buffers discarded: %d,\r\n  buffer allocation failures: %d,\r\n   heap allocation failures: %d\r\n",
              evt->data.evt_system_resource_exhausted.num_buffers_discarded,
              evt->data.evt_system_resource_exhausted.num_buffer_allocation_failures,
              evt->data.evt_system_resource_exhausted.num_heap_allocation_failures);
      timestamp = sl_sleeptimer_get_time();
      app_log("[%ld sec]: Total number of allocation failures: %ld\r\n", timestamp, allocation_failures);
#endif
      break;

    case sl_bt_evt_pawr_advertiser_subevent_data_request_id:
      uint8_t subevent = evt->data.evt_pawr_advertiser_subevent_data_request.subevent_start;
      uint8_t subevents_left = evt->data.evt_pawr_advertiser_subevent_data_request.subevent_data_count;

      while (subevents_left > 0) {
        sc = sl_bt_pawr_advertiser_set_subevent_data(adv_handle, subevent, 0, slot_number, sizeof(subevent_data), &subevent_data);
        app_assert_status(sc);

        // Check if subevent roll-over is required
        if (subevent == PAWR_NUM_SUBEVENTS - 1) {
          subevent = 0;
        } else {
          subevent++;
        }
        subevents_left--;
      }

      subevent_data++;
      break;
    case sl_bt_evt_pawr_advertiser_response_report_id:
      if (evt->data.evt_pawr_advertiser_response_report.data_status != 255) {
        app_log("Response received in slot: %d, data: %d, data status: %d\r\n",
                evt->data.evt_pawr_advertiser_response_report.response_slot,
                evt->data.evt_pawr_advertiser_response_report.data.data[0],
                evt->data.evt_pawr_advertiser_response_report.data_status);
      }
      break;
    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      app_log("Connection closed, start scanning for next\r\n");
      conn_info.char_handle     = 0;
      conn_info.service_handle  = 0;
      conn_info.conn_handle     = 0;
      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m, sl_bt_scanner_discover_generic);
      app_assert_status(sc);
      conn_state = scanning;
      break;

    default:
      //app_log("Unhandled event: %lx\r\n", SL_BT_MSG_ID(evt->header));
      break;
  }
}

// Parse advertisements looking for advertised pawr sync service
static uint8_t find_service_in_advertisement(uint8_t *data, uint8_t len)
{
  uint8_t ad_field_length;
  uint8_t ad_field_type;
  uint8_t i = 0;
  uint8_t offset = sizeof(pawr_sync_service);
  // Parse advertisement packet
  while (i < len) {
    ad_field_length = data[i];
    ad_field_type = data[i + 1];
    // Partial ($02) or complete ($03) list of 16-bit UUIDs
    if (ad_field_type == 0x02 || ad_field_type == 0x03) {
      // compare UUID to pawr sync service UUID
      if (memcmp(&data[i + offset], pawr_sync_service, offset) == 0) {
        return 1;
      }
    }
    // advance to the next AD struct
    i = i + ad_field_length + 1;
  }
  return 0;
}
