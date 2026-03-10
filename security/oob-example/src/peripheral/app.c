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
#include "sl_iostream_init_usart_instances.h"
#include "app_log.h"
#include "string.h"
#include "stdio.h"

#define TIMER_TIMEOUT 3000
#define SIGNAL_NOTIFY_TIMER    1
#define LOCAL_ECHO
#define ASCII_CAPITAL_LETTER_MIN 65
#define ASCII_CAPITAL_LETTER_MAX 70
#define ASCII_LOWER_LETTER_MIN 97
#define ASCII_LOWER_LETTER_MAX 102
#define ASCII_NUMBER_MIN 48
#define ASCII_NUMBER_MAX 57
#define ASCII_CAPITAL_LETTER_CONVERT 55
#define ASCII_LOWER_LETTER_CONVERT 87
#define ASCII_NUMBER_CONVERT 48
#define ASCII_BACKSPACE 8

#ifndef MAX_CONNECTIONS
#define MAX_CONNECTIONS 4
#endif

#define STATE_IDLE_MODE 0
#define STATE_OOB_MODE  1

static void read_oob_data();

sl_sleeptimer_timer_handle_t sleep_timer_handle;
void sleeptimer_callback(sl_sleeptimer_timer_handle_t *handle, void *data);

uint8_t rx_oob_buf[32];
uint8_t rx_oob_buf_ascii[64];
static uint16_t notifyEnabled = 0;
static uint8_t connHandle = 0xFF;
static uint8_t notifyBuf[20] = { 0 };
// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
static uint8_t _oob_state = STATE_IDLE_MODE;
static aes_key_128 key_random, key_confirm;

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
  if (STATE_OOB_MODE == _oob_state) {
    read_oob_data();
  }
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
      memset(notifyBuf, '0', 20);
      app_log_info("stack version: %u.%u.%u\r\r\n", evt->data.evt_system_boot.major, evt->data.evt_system_boot.minor, evt->data.evt_system_boot.patch);
      app_log_info("Peripheral Boot\r\n");
      // Extract unique ID from BT Address.
      sc = sl_bt_system_get_identity_address(&address, &address_type);
      app_assert_status(sc);
      app_log_info("local BT device address: ");
      for (uint8_t i = 0; i < 5; i++) {
        app_log("%2.2x:", address.addr[5 - i]);
      }
      app_log("%2.2x\r\n", address.addr[0]);
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
      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);
      app_log_info("All bonding deleted\r\n");
      sc = sl_bt_sm_delete_bondings();
      app_assert_status(sc);

      /* bit 3 of flag is 0 to allow legacy pairing */
      sc = sl_bt_sm_configure(0x0B, sl_bt_sm_io_capability_keyboarddisplay);
      app_assert_status(sc);

      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);

      app_log_info("Use Secure Connection Pairing.\r\n");
      app_log_info("Enter 32-byte OOB data and confirm value.\r\n");
      sc = sl_bt_sm_set_oob(1, NULL, NULL);
      app_assert_status(sc);

      _oob_state = STATE_OOB_MODE;
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      app_log_info("Connected\r\n");
      connHandle = evt->data.evt_connection_opened.connection;
      // a delay could be needed before sending the security request to the phone, sending the request too soon
      // can cause the phone to drop it. 0.5s is a suggested value
      sl_sleeptimer_delay_millisecond(500);
      sc = sl_bt_sm_increase_security(connHandle);
      app_assert_status(sc);
      app_log_info("Increasing security\r\n");
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      // Restart advertising after client has disconnected.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_advertiser_connectable_scannable);
      app_assert_status(sc);
      sc = sl_sleeptimer_stop_timer(&sleep_timer_handle);
      app_assert_status(sc);
      break;
    case sl_bt_evt_gatt_server_characteristic_status_id:
      /* Char status changed */
      if ((evt->data.evt_gatt_server_characteristic_status.characteristic == gattdb_ntf_char) && (evt->data.evt_gatt_server_characteristic_status.status_flags == 0x01)) {
        notifyEnabled = evt->data.evt_gatt_server_characteristic_status.client_config_flags;
      }
      if (notifyEnabled == sl_bt_gatt_server_notification) {
        sc = sl_sleeptimer_start_periodic_timer_ms(&sleep_timer_handle,
                                                   TIMER_TIMEOUT,
                                                   sleeptimer_callback,
                                                   (void *)NULL,
                                                   0,
                                                   0);
        app_assert_status(sc);
      } else {
        sc = sl_sleeptimer_stop_timer(&sleep_timer_handle);
        app_assert_status(sc);
      }
      break;

    case sl_bt_evt_gatt_server_attribute_value_id:
      if ((evt->data.evt_gatt_server_attribute_value.att_opcode == sl_bt_gatt_write_request) && (evt->data.evt_gatt_server_attribute_value.attribute == gattdb_wrt_char)) {
        app_log_info("Gatt Write Received: ");
        for (uint8_t i = 0; i < evt->data.evt_gatt_server_attribute_value.value.len; i++) {
          app_log("%c", evt->data.evt_gatt_server_attribute_value.value.data[i]);
        }
        app_log("\r\n");
      }
      break;

    case sl_bt_evt_system_external_signal_id:
      if (evt->data.evt_system_external_signal.extsignals == SIGNAL_NOTIFY_TIMER) {
        if ((connHandle != 0xFF) && (notifyEnabled == sl_bt_gatt_server_notification)) {
          uint8_t i = notifyBuf[0];
          if (i == '9') {
            i = '0';
            memset(notifyBuf, i, 20);
          } else {
            memset(notifyBuf, i + 1, 20);
          }
          sc = sl_bt_gatt_server_send_notification(connHandle, gattdb_ntf_char, 20, notifyBuf);
          app_assert_status(sc);
        }
      }
      break;

    case sl_bt_evt_sm_confirm_bonding_id:
      sc = sl_bt_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 1);
      app_assert_status(sc);
      app_log_info("Bonding confirmed\r\n");
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
      //      app_log_info("Are you see the same passkey on the tablet: %d (y/n)?\r\n",evt->data.evt_sm_confirm_passkey.passkey);
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

static void read_oob_data()
{
  sl_status_t sc;
  uint8_t input;
  sc = sl_iostream_getchar(sl_iostream_vcom_handle,
                           (char *)&input);
  static uint8_t num = 0;
  if (sc == SL_STATUS_OK) {
#ifdef LOCAL_ECHO
    if (input != '\n') {
      app_log("%c", input);
    }
#endif
    if (((input >= ASCII_LOWER_LETTER_MIN) && (input <= ASCII_LOWER_LETTER_MAX))
        || ((input >= ASCII_NUMBER_MIN) && (input <= ASCII_NUMBER_MAX))
        || ((input >= ASCII_CAPITAL_LETTER_MIN) && (input <= ASCII_CAPITAL_LETTER_MAX))) {
      app_log("   Position in the array: %d\r\n", num);
      rx_oob_buf_ascii[num++] = input;
    } else if (input == ASCII_BACKSPACE) {
      if (num != 0) {
        num = num - 1;
      }
      app_log("Deleted character: %c\r\n", rx_oob_buf_ascii[num]);
      if (num == 0) {
        app_log("The whole 32-byte OOB data got deleted.\r\n");
      }
      app_log("Position of the next character in the array after delete: %d\r\n", num);
    } else {
      app_log("\r\n");
      app_log("Invalid character deleted: %c, keep typing the 32-byte OOB data.\r\n", input);
    }
  }
  if (num == 64) {
    for (uint8_t x = 0; x < 64; x += 2) {
      uint8_t val = rx_oob_buf_ascii[x + 1];
      if ((rx_oob_buf_ascii[x] >= ASCII_LOWER_LETTER_MIN) && (rx_oob_buf_ascii[x] <= ASCII_LOWER_LETTER_MAX)) {
        rx_oob_buf_ascii[x] = rx_oob_buf_ascii[x] - ASCII_LOWER_LETTER_CONVERT;
      } else if ((rx_oob_buf_ascii[x] >= ASCII_NUMBER_MIN) && (rx_oob_buf_ascii[x] <= ASCII_NUMBER_MAX)) {
        rx_oob_buf_ascii[x] = rx_oob_buf_ascii[x] - ASCII_NUMBER_CONVERT;
      } else if ((rx_oob_buf_ascii[x] >= ASCII_CAPITAL_LETTER_MIN) && (rx_oob_buf_ascii[x] <= ASCII_CAPITAL_LETTER_MAX)) {
        rx_oob_buf_ascii[x] = rx_oob_buf_ascii[x] - ASCII_CAPITAL_LETTER_CONVERT;
      }
      if ((val >= ASCII_LOWER_LETTER_MIN) && (val <= ASCII_LOWER_LETTER_MAX)) {
        val = val - ASCII_LOWER_LETTER_CONVERT;
      } else if ((val >= ASCII_NUMBER_MIN) && (val <= ASCII_NUMBER_MAX)) {
        val = val - ASCII_NUMBER_CONVERT;
      } else if ((val >= ASCII_CAPITAL_LETTER_MIN) && (val <= ASCII_CAPITAL_LETTER_MAX)) {
        val = val - ASCII_CAPITAL_LETTER_CONVERT;
      }
      rx_oob_buf[x / 2] = (rx_oob_buf_ascii[x] << 4) + val;
    }
    app_log_info("16-byte OOB data: ");
    for (uint8_t x = 0; x < 32; x++) {
      if (x == 16) {
        app_log_info("\r\n");
        app_log_info("16-byte confirm value: ");
      }
      app_log("0x%02X ", rx_oob_buf[x]);
    }
    app_log_info("\r\n");
    memcpy(key_random.data, &rx_oob_buf[0], sizeof(key_random));
    memcpy(key_confirm.data, &rx_oob_buf[16], sizeof(key_confirm));
    sc = sl_bt_sm_set_remote_oob(1, key_random, key_confirm);
    app_assert_status(sc);
    /* Set advertising parameters. 100ms advertisement interval. All channels used.
     * The first two parameters are minimum and maximum advertising interval, both in
     * units of (milliseconds * 1.6). The third parameter '7' sets advertising on all channels. */
    sc = sl_bt_advertiser_set_timing(advertising_set_handle, 160, 160, 0, 0);
    app_assert_status(sc);

    sc = sl_bt_advertiser_set_channel_map(advertising_set_handle, 7);
    app_assert_status(sc);

    sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                               sl_bt_advertiser_general_discoverable);
    app_assert_status(sc);
    /* Start general advertising and enable connections. */
    sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                       sl_bt_advertiser_connectable_scannable);
    app_assert_status(sc);
    num = 0;
    _oob_state = STATE_IDLE_MODE;
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

  sl_bt_external_signal(SIGNAL_NOTIFY_TIMER);
}
