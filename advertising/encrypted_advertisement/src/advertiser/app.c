/***************************************************************************/ /**
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
#include "sl_common.h"
#include "sl_bt_api.h"
#include "app_assert.h"
#include "app.h"
#include "sl_bt_ead_core.h"
#include <stdio.h>
#include "psa/crypto_values.h"
#include "psa/crypto.h"
#include "sl_memory_manager.h"
#include "gatt_db.h"
#include "sl_sleeptimer.h"
#include "sl_simple_button_instances.h"

#define BLE_EA_ADV_DATA_LEN 0xBF
#define ADDRESS_CHANGE_PERIOD_MS 20000
#define BTN_CONFIRM_PERIOD_MS 2000

#define CONFIRM_BTN 0
#define ONESHOT_BTN_TIMER_CALLBACK 1
#define PERIODIC_TIMER_CALLBACK 2

uint8_t btn_count = 0;

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
uint8_t advertisement_buffer[BLE_EA_ADV_DATA_LEN];

sl_sleeptimer_timer_handle_t periodic_timer_handle;
sl_sleeptimer_timer_handle_t oneshot_btn_timer_handle;

char name[] = "Encrypted Advertiser";
char secret_data[10];
uint8_t secret_number = 0;

void oneshot_sleeptimer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  sl_bt_external_signal(ONESHOT_BTN_TIMER_CALLBACK);
}

void periodic_sleeptimer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  sl_bt_external_signal(PERIODIC_TIMER_CALLBACK);
}

void sl_button_on_change(const sl_button_t *handle)
{
  if (handle == &sl_button_btn0 && sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
    sl_bt_external_signal(CONFIRM_BTN);
  }
}

sl_status_t initialize_and_store_key_material(sl_bt_ead_key_material_p key_material, psa_key_id_t key_id)
{
  sl_status_t sc = SL_STATUS_FAIL;
  // generate the session key and the IV needed to create the key material
  sl_bt_ead_session_key_t session_key;
  sl_bt_ead_iv_t initialization_vector;
  if (psa_generate_random(session_key, SL_BT_EAD_SESSION_KEY_SIZE) != PSA_SUCCESS || psa_generate_random(initialization_vector, SL_BT_EAD_IV_SIZE) != PSA_SUCCESS) {
    return sc;
  }
  memcpy(key_material->key, session_key, SL_BT_EAD_SESSION_KEY_SIZE);
  memcpy(key_material->iv, initialization_vector, SL_BT_EAD_IV_SIZE);
  app_log("session key: ");
  for (uint8_t i = 0; i < 16; i++) {
    app_log("%02X:", key_material->key[i]);
  }
  app_log("\r\ninitiazation vector: ");
  for (uint8_t i = 0; i < 8; i++) {
    app_log("%02X:", key_material->iv[i]);
  }
  app_log("\r\n");
  // set the key material Gatt characteristic to the key material value
  sc = sl_bt_gatt_server_write_attribute_value(gattdb_Encrypted_Data_Key_Material, 0, SL_BT_EAD_KEY_MATERIAL_SIZE, (uint8_t *)key_material);
  app_assert_status(sc);
  // store the key material and flush the input buffer
  sc = sl_bt_ead_store_key(PSA_KEY_USAGE_ENCRYPT, PSA_KEY_LIFETIME_VOLATILE, key_material, &key_id);
  app_assert_status(sc);
  return sc;
}

sl_status_t construct_advertisement_payload(sl_bt_ead_key_material_p key_material, sl_bt_ead_nonce_p nonce, uint8_t *index)
{
  sl_status_t sc = SL_STATUS_FAIL;
  *index = 0;
  // refresh the secret part of the advertisement
  secret_number++;
  sprintf(secret_data, "secret%02d", secret_number);
  // add an optional unencrypted part e.g flags and complete local name
  // add flags
  advertisement_buffer[(*index)++] = 0x02; // Ad structure len
  advertisement_buffer[(*index)++] = 0x01; // Ad structure type
  advertisement_buffer[(*index)++] = 0x06; // Ad structure data
  // add complete local name
  advertisement_buffer[(*index)++] = strlen(name) + 1;       // Ad structure len
  advertisement_buffer[(*index)++] = 0x09;                   // Ad structure type
  memcpy(advertisement_buffer + *index, name, strlen(name)); // Ad structure data
  *index += strlen(name);
  // Note: any part of the advertisement or all can be encrypted including the above
  // add an encrypted part e.g manufacturer specific data
  uint8_t manufactuer_data_buf[BLE_EA_ADV_DATA_LEN];
  size_t manufactuer_data_len = (size_t)(2 + strlen(secret_data)); // len+type+data
  sl_bt_ead_mic_t message_integraty_check;
  manufactuer_data_buf[0] = strlen(secret_data) + 1;                  // Ad structure len
  manufactuer_data_buf[1] = 0xFF;                                     // Ad structure type
  memcpy(manufactuer_data_buf + 2, secret_data, strlen(secret_data)); // Ad structure data
  sc = sl_bt_ead_encrypt(key_material, nonce, manufactuer_data_len, manufactuer_data_buf, message_integraty_check);
  app_assert_status(sc);
  sl_bt_ead_ad_structure_p encrypted_ad_structure = (sl_bt_ead_ad_structure_p)sl_malloc(sizeof(struct sl_bt_ead_ad_structure_s));
  uint8_t *encrypted_data_length = &(uint8_t){BLE_EA_ADV_DATA_LEN };
  encrypted_ad_structure->length = manufactuer_data_len;
  encrypted_ad_structure->ad_type = SL_BT_ENCRYPTED_DATA_AD_TYPE;
  encrypted_ad_structure->ad_data = manufactuer_data_buf;
  encrypted_ad_structure->randomizer = &(nonce->randomizer);
  encrypted_ad_structure->mic = &message_integraty_check;
  sc = sl_bt_ead_pack_ad_data(encrypted_ad_structure, encrypted_data_length, advertisement_buffer + *index);
  app_log("--------------------------------------------------------------------------\n\r");
  app_log("Information before encryption: %s \n\r", secret_data);
  app_log("Information after encryption:\n\r");
  for (uint8_t i = *index; i < *index + *encrypted_data_length; i++) {
    app_log("%02X", advertisement_buffer[i]);
  }
  printf("\n\r");
  app_assert_status(sc);
  free(encrypted_ad_structure);
  (*index) += *encrypted_data_length;

  return sc;
}

// Application Init.
SL_WEAK void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////
  sl_status_t sc;
  sc = sl_sleeptimer_start_periodic_timer_ms(&periodic_timer_handle, ADDRESS_CHANGE_PERIOD_MS, periodic_sleeptimer_callback, (void *)NULL, 0, 0);
  app_assert_status(sc);
}

// Application Process Action.
SL_WEAK void app_process_action(void)
{
  if (app_is_process_required()) {
    /////////////////////////////////////////////////////////////////////////////
    // Put your additional application code here!                              //
    // This is will run each time app_proceed() is called.                     //
    // Do not call blocking functions from here!                               //
    /////////////////////////////////////////////////////////////////////////////
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
  static struct sl_bt_ead_key_material_s key_material;
  static struct sl_bt_ead_nonce_s nonce;
  static psa_key_id_t key_id = PSA_KEY_ID_NULL;
  static uint8_t index;
  static uint8_t connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;
  static uint8_t pairing_state;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      pairing_state = 0;
      app_log("boot\r\n");
      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);
      sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_MITM_REQUIRED | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED | SL_BT_SM_CONFIGURATION_SC_ONLY | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED, sl_bt_sm_io_capability_displayyesno);
      app_assert_status(sc);
      // initializes the key material
      initialize_and_store_key_material(&key_material, key_id);
      // initializes the nonce
      sl_bt_ead_session_init(&key_material, NULL, &nonce);
      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);
      bd_addr random_address;
      sc = sl_bt_advertiser_set_random_address(advertising_set_handle, sl_bt_gap_random_resolvable_address, (bd_addr){0 }, &random_address);
      app_assert_status(sc);
      // fill the advertisement with encrypted and unencrypted data
      sc = construct_advertisement_payload(&key_material, &nonce, &index);
      app_assert_status(sc);
      sc = sl_bt_extended_advertiser_set_data(advertising_set_handle, index, advertisement_buffer);
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
      sc = sl_bt_extended_advertiser_start(advertising_set_handle,
                                           sl_bt_extended_advertiser_connectable, 0);
      app_assert_status(sc);
      app_log("started advertisement\r\n");
      break;

    case sl_bt_evt_connection_opened_id:
      app_log("connection opened\r\n");
      sc = sl_sleeptimer_stop_timer(&periodic_timer_handle);
      app_assert_status(sc);
      connection_handle = evt->data.evt_connection_opened.connection;
      break;

    case sl_bt_evt_connection_parameters_id:
      // This switch case indicates security mode changes
      switch (evt->data.evt_connection_parameters.security_mode) {
        case sl_bt_connection_mode1_level1:
          app_log("No Security\r\n");
          break;
        case sl_bt_connection_mode1_level2:
          app_log("Unauthenticated pairing with encryption\r\n");
          break;
        case sl_bt_connection_mode1_level3:
          app_log("Authenticated pairing with encryption\r\n");
          break;
        case sl_bt_connection_mode1_level4:
          app_log("Authenticated Secure Connections pairing with encryption\r\n");
          break;
      }
      break;

    case sl_bt_evt_connection_closed_id:
      app_log("closed connection reason: 0x%4X\r\n", evt->data.evt_connection_closed.reason);
      connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;
      sc = sl_sleeptimer_start_periodic_timer_ms(&periodic_timer_handle, ADDRESS_CHANGE_PERIOD_MS, periodic_sleeptimer_callback, (void *)NULL, 0, 0);
      app_assert_status(sc);
      sc = sl_bt_extended_advertiser_start(advertising_set_handle,
                                           sl_bt_extended_advertiser_connectable, 0);
      app_assert_status(sc);
      break;

    case sl_bt_evt_sm_confirm_passkey_id:
      pairing_state = 1;
      app_log("The passkey is: %06li\r\n", evt->data.evt_sm_confirm_passkey.passkey);
      app_log("Please press btn0 once to refuse bonding or twice to accept bonding\n\r");
      break;

    case sl_bt_evt_sm_confirm_bonding_id:
      app_log("New bonding request\r\n");
      sc = sl_bt_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 1);
      app_assert_status(sc);
      break;

    case sl_bt_evt_sm_bonded_id:
      app_log("device bonded successfully\r\n");
      pairing_state = 0;
      break;

    case sl_bt_evt_sm_bonding_failed_id:
      app_log("bonding failed, reason 0x%2X\r\n",
              evt->data.evt_sm_bonding_failed.reason);
      app_assert_status(sc);
      break;

    case sl_bt_evt_system_external_signal_id:
      if (evt->data.evt_system_external_signal.extsignals == PERIODIC_TIMER_CALLBACK) {
        // stop advertising, change resolvable private address and update the randomizer according to
        //  the Supplement to the Bluetooth Core Specification v11 Part A, Section 1.23.4
        sc = sl_bt_advertiser_stop(advertising_set_handle);
        app_assert_status(sc);
        sc = sl_bt_advertiser_set_random_address(advertising_set_handle, sl_bt_gap_random_resolvable_address, (bd_addr){0 }, &random_address);
        app_assert_status(sc);
        sc = sl_bt_ead_randomizer_update(&nonce);
        app_assert_status(sc);
        sc = construct_advertisement_payload(&key_material, &nonce, &index);
        app_assert_status(sc);
        sc = sl_bt_extended_advertiser_set_data(advertising_set_handle, index, advertisement_buffer);
        app_assert_status(sc);
        sc = sl_bt_extended_advertiser_start(advertising_set_handle,
                                             sl_bt_extended_advertiser_connectable, 0);
        app_assert_status(sc);
        break;
      }
      if (pairing_state == 0) {
        break;
      }

      if (evt->data.evt_system_external_signal.extsignals == ONESHOT_BTN_TIMER_CALLBACK) {
        btn_count = 0;
        sc = sl_bt_sm_passkey_confirm(connection_handle, 0);
        app_assert_status(sc);
        break;
      }

      if (evt->data.evt_system_external_signal.extsignals == CONFIRM_BTN) {
        btn_count++;
        if (btn_count == 2) {
          app_log("connection handle %d\r\n", connection_handle);
          sc = sl_bt_sm_passkey_confirm(connection_handle, 1);
          app_assert_status(sc);
          btn_count = 0;
        } else if (btn_count == 1) {
          sc = sl_sleeptimer_start_timer_ms(&oneshot_btn_timer_handle, BTN_CONFIRM_PERIOD_MS, oneshot_sleeptimer_callback, (void *)NULL, 0, 0);
          app_assert_status(sc);
        }
        break;
      }
  }
}
