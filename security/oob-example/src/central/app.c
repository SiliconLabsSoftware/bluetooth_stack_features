/***************************************************************************/ /**
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
#include "app_log.h"

#define TIMER_TIMEOUT 3000

#define DISCONNECTED 0
#define SCANNING     1
#define FIND_SERVICE 2
#define FIND_CHAR    3
#define ENABLE_NOTIF 4
#define DATA_MODE    5

#define SIGNAL_WRITE_TIMER 1

const uint8_t Svc_uuid[] = {
  0x07,
  0xb9,
  0xf9,
  0xd7,
  0x50,
  0xa1,
  0x20,
  0x89,
  0x77,
  0x40,
  0xcb,
  0xfd,
  0x2c,
  0xc1,
  0x80,
  0x48
};
const uint8_t Ntf_uuid[] = {
  0x07,
  0xb9,
  0xf9,
  0xd7,
  0x50,
  0xa2,
  0x20,
  0x89,
  0x77,
  0x40,
  0xcb,
  0xfd,
  0x2c,
  0xc1,
  0x80,
  0x48
};
const uint8_t Wrt_uuid[] = {
  0x07,
  0xb9,
  0xf9,
  0xd7,
  0x50,
  0xa3,
  0x20,
  0x89,
  0x77,
  0x40,
  0xcb,
  0xfd,
  0x2c,
  0xc1,
  0x80,
  0x48
};

#define PRINT_GATT_INFO

static uint8_t _conn_handle = 0xFF;
static uint8_t _main_state;
static uint32_t _service_handle;
static uint16_t ntf_char_handle;
static uint16_t wrt_char_handle;
static uint8_t writeBuf[21] = { 0 };

static aes_key_128 key_random, key_confirm;

sl_sleeptimer_timer_handle_t sleep_timer_handle;
void sleeptimer_callback(sl_sleeptimer_timer_handle_t *handle, void *data);

void Reset_variables()
{
  _conn_handle = 0xFF;
  _main_state = DISCONNECTED;
  _service_handle = 0;
  ntf_char_handle = 0;
  wrt_char_handle = 0;
  memset(writeBuf, '0', 20);
}

uint8_t Process_scan_response(sl_bt_evt_scanner_legacy_advertisement_report_t *pResp)
{
  // decoding advertising packets is done here. The list of AD types can be found
  // at: https://www.bluetooth.com/specifications/assigned-numbers/Generic-Access-Profile

  uint8_t i = 0;
  uint8_t ad_match_found = 0;
  uint8_t ad_len;
  uint8_t ad_type;

  while (i < (pResp->data.len - 1)) {
    ad_len = pResp->data.data[i];
    ad_type = pResp->data.data[i + 1];

    if (ad_type == 0x06 || ad_type == 0x07) {
      // type 0x06 = More 128-bit UUIDs available
      // type 0x07 = Complete list of 128-bit UUIDs available

      // note: this check assumes that the service we are looking for is first
      // in the list. To be fixed so that there is no such limitation...
      if (memcmp(Svc_uuid, &(pResp->data.data[i + 2]), 16) == 0) {
        ad_match_found = 1;
        break;
      }
    }
    //jump to next AD record
    i = i + ad_len + 1;
  }
  return (ad_match_found);
}

static void writeToPeripheral()
{
  sl_status_t ret;

  uint8_t i = writeBuf[0];
  if (i == '9') {
    i = '0';
    memset(writeBuf, i, 20);
  } else {
    memset(writeBuf, i + 1, 20);
  }
  ret = sl_bt_gatt_write_characteristic_value(_conn_handle, wrt_char_handle, 20, writeBuf);
  app_assert_status(ret);
}
/**************************************************************************/ /**
 * Application Init.
 *****************************************************************************/
SL_WEAK void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////
}

/**************************************************************************/ /**
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

/**************************************************************************/ /**
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

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      app_log_info("stack version: %u.%u.%u\r\r\n", evt->data.evt_system_boot.major, evt->data.evt_system_boot.minor, evt->data.evt_system_boot.patch);
      app_log_info("Central Boot\r\n");
      // Extract unique ID from BT Address.
      sc = sl_bt_system_get_identity_address(&address, &address_type);
      app_assert_status(sc);
      app_log_info("local BT device address: ");
      for (uint8_t i = 0; i < 5; i++) {
        app_log_info("%2.2x:", address.addr[5 - i]);
      }
      app_log_info("%2.2x\r\n", address.addr[0]);
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

      /* delete all bondings to force the pairing process */
      app_log_info("All bonding deleted\r\n");
      sc = sl_bt_sm_delete_bondings();
      app_assert_status(sc);

      sc = sl_bt_sm_configure(0x0B, sl_bt_sm_io_capability_keyboarddisplay);
      app_assert_status(sc);

      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);

      sc = sl_bt_sm_set_oob(1, &key_random, &key_confirm);
      app_assert_status(sc);

      app_log_info("Enable OOB with Secure Connection Pairing - Success.\r\n");
      app_log_info("16-byte random OOB data: ");
      for (uint8_t i = 0; i < 16; i++) {
        app_log("0x%02X ", key_random.data[i]);
      }
      app_log("\r\n");
      app_log_info("16-byte confirm value: ");
      for (uint8_t i = 0; i < 16; i++) {
        app_log("0x%02X ", key_confirm.data[i]);
      }
      app_log("\r\n");

      Reset_variables();
      // start discovery
      sc = sl_bt_scanner_start(0x01, sl_bt_scanner_discover_generic);
      app_assert_status(sc);
      break;
    case sl_bt_evt_scanner_legacy_advertisement_report_id:
      // process scan responses: this function returns 1 if we found the service we are looking for
      if (Process_scan_response(&(evt->data.evt_scanner_legacy_advertisement_report)) > 0) {
        uint8_t pResp_connection;

        // match found -> stop discovery and try to connect
        sc = sl_bt_scanner_stop();
        app_assert_status(sc);

        sc = sl_bt_connection_open(evt->data.evt_scanner_legacy_advertisement_report.address, evt->data.evt_scanner_legacy_advertisement_report.address_type, 0x01, &pResp_connection);
        app_assert_status(sc);

        // make copy of connection handle for later use (for example, to cancel the connection attempt)
        _conn_handle = pResp_connection;
      }
      break;
    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      // Start service discovery (we are only interested in one UUID)
      app_log_info("Connected\r\n");
      sc = sl_bt_gatt_discover_primary_services_by_uuid(_conn_handle, 16, Svc_uuid);
      app_assert_status(sc);
      _main_state = FIND_SERVICE;
      break;

    case sl_bt_evt_connection_closed_id:
      // start discovery
      sc = sl_bt_scanner_start(0x01, sl_bt_scanner_discover_generic);
      app_assert_status(sc);
      sc = sl_sleeptimer_stop_timer(&sleep_timer_handle);
      app_assert_status(sc);
      break;

    case sl_bt_evt_gatt_service_id:
      if (evt->data.evt_gatt_service.uuid.len == 16) {
        if (memcmp(Svc_uuid, evt->data.evt_gatt_service.uuid.data, 16) == 0) {
          // Service Found.
          app_log_info("Specified Service Found.\r\n");
          _service_handle = evt->data.evt_gatt_service.service;
        }
      }
      break;

    case sl_bt_evt_gatt_characteristic_id:
      if (evt->data.evt_gatt_characteristic.uuid.len == 16) {
        if (memcmp(Ntf_uuid, evt->data.evt_gatt_characteristic.uuid.data, 16) == 0) {
          ntf_char_handle = evt->data.evt_gatt_characteristic.characteristic;
        } else if (memcmp(Wrt_uuid, evt->data.evt_gatt_characteristic.uuid.data, 16) == 0) {
          wrt_char_handle = evt->data.evt_gatt_characteristic.characteristic;
        }
      }
      break;

    case sl_bt_evt_gatt_procedure_completed_id:
      switch (_main_state) {
        case FIND_SERVICE:

          if (_service_handle != 0) {
            // Service found, next search for characteristics
            sc = sl_bt_gatt_discover_characteristics(_conn_handle, _service_handle);
            app_assert_status(sc);
            _main_state = FIND_CHAR;
          } else {
            // no service found -> disconnect
            sc = sl_bt_connection_close(_conn_handle);
            app_assert_status(sc);
          }
          break;

        case FIND_CHAR:
          if (ntf_char_handle && wrt_char_handle) {
            // Char found, turn on indications
            sc = sl_bt_gatt_set_characteristic_notification(_conn_handle, ntf_char_handle, sl_bt_gatt_notification);
            app_assert_status(sc);
            _main_state = ENABLE_NOTIF;
          } else {
            // no characteristic found? -> disconnect
            sc = sl_bt_connection_close(_conn_handle);
            app_assert_status(sc);
          }
          break;

        case ENABLE_NOTIF:
          _main_state = DATA_MODE;
          sc = sl_sleeptimer_start_periodic_timer_ms(&sleep_timer_handle,
                                                     TIMER_TIMEOUT,
                                                     sleeptimer_callback,
                                                     (void *)NULL,
                                                     0,
                                                     0);
          app_assert_status(sc);
          break;

        default:
          break;
      }
      break;

    case sl_bt_evt_gatt_characteristic_value_id:
      if ((evt->data.evt_gatt_characteristic_value.att_opcode == sl_bt_gatt_handle_value_notification) && (evt->data.evt_gatt_characteristic_value.characteristic == ntf_char_handle)) {
        // TODO: data notification is received here
#ifdef PRINT_GATT_INFO
        app_log_info("Gatt Notification Received: ");
        for (uint8_t i = 0; i < evt->data.evt_gatt_characteristic_value.value.len; i++) {
          app_log("%c", evt->data.evt_gatt_characteristic_value.value.data[i]);
        }
        app_log("\r\n");
#endif
      }
      break;

    case sl_bt_evt_system_external_signal_id:
      if (SIGNAL_WRITE_TIMER == evt->data.evt_system_external_signal.extsignals) {
        if ((_conn_handle != 0xFF) && (wrt_char_handle != 0)) {
          writeToPeripheral();
        }
      }
      break;

    case sl_bt_evt_sm_confirm_bonding_id:
      app_log_info("Bonding confirmed\r\n");
      sc = sl_bt_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 1);
      app_assert_status(sc);
      break;

    case sl_bt_evt_sm_passkey_display_id:
      app_log_info("Passkey display\r\n");
      //      app_log_info("Enter this passkey on the tablet:\r\n%d\r\n",
      //          evt->data.evt_sm_passkey_display.passkey);
      break;

    case sl_bt_evt_sm_passkey_request_id:
      app_log_info("Passkey request\r\n");
      //      app_log_info("Enter the passkey you see on the tablet\r\n");
      break;

    case sl_bt_evt_sm_confirm_passkey_id:
      app_log_info("Confirm passkey\r\n");
      //                  app_log_info("Are you see the same passkey on the tablet: %d (y/n)?\r\n",evt->data.evt_sm_confirm_passkey.passkey);
      break;

    case sl_bt_evt_sm_bonded_id:
      /* The bonding/pairing was successful so set the flag to allow indications to proceed */
      app_log_info("Bonding completed\r\n");
      break;

    case sl_bt_evt_sm_bonding_failed_id:
      /* If the attempt at bonding/pairing failed, clear the bonded flag and display the reason */
      app_log_info("Bonding failed: ");
      switch (evt->data.evt_sm_bonding_failed.reason) {
        case SL_STATUS_BT_SMP_PASSKEY_ENTRY_FAILED:
          app_log_info("The user input of passkey failed\r\n");
          break;

        case SL_STATUS_BT_SMP_OOB_NOT_AVAILABLE:
          app_log_info("Out of Band data is not available for authentication\r\n");
          break;

        case SL_STATUS_BT_SMP_AUTHENTICATION_REQUIREMENTS:
          app_log_info("The pairing procedure cannot be performed as authentication requirements cannot be met due to IO capabilities of one or both devices\r\n");
          break;

        case SL_STATUS_BT_SMP_CONFIRM_VALUE_FAILED:
          app_log_info("The confirm value does not match the calculated compare value\r\n");
          break;

        case SL_STATUS_BT_SMP_PAIRING_NOT_SUPPORTED:
          app_log_info("Pairing is not supported by the device\r\n");
          break;

        case SL_STATUS_BT_SMP_ENCRYPTION_KEY_SIZE:
          app_log_info("The resultant encryption key size is insufficient for the security requirements of this device\r\n");
          break;

        case SL_STATUS_BT_SMP_COMMAND_NOT_SUPPORTED:
          app_log_info("The SMP command received is not supported on this device\r\n");
          break;

        case SL_STATUS_BT_SMP_UNSPECIFIED_REASON:
          app_log_info("Pairing failed due to an unspecified reason\r\n");
          break;

        case SL_STATUS_BT_SMP_REPEATED_ATTEMPTS:
          app_log_info("Pairing or authentication procedure is disallowed because too little time has elapsed since last pairing request or security request\r\n");
          break;

        case SL_STATUS_BT_SMP_INVALID_PARAMETERS:
          app_log_info("Invalid Parameters\r\n");
          break;

        case SL_STATUS_BT_SMP_DHKEY_CHECK_FAILED:
          app_log_info("The bonding does not exist\r\n");
          break;

        case SL_STATUS_BT_CTRL_PIN_OR_KEY_MISSING:
          app_log_info("Pairing failed because of missing PIN, or authentication failed because of missing Key\r\n");
          break;

        case SL_STATUS_TIMEOUT:
          app_log_info("Operation timed out\r\n");
          break;

        default:
          app_log_info("Unknown error: 0x%X\r\n", evt->data.evt_sm_bonding_failed.reason);
          break;
      }

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

  sl_bt_external_signal(SIGNAL_WRITE_TIMER);
}
