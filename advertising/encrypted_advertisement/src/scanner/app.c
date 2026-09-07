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
#include "sl_sleeptimer.h"
#include "sl_simple_button_instances.h"
#include "sl_bt_ead_core.h"
#include "psa/crypto_values.h"
#include "psa/crypto.h"

#define ADVERTISEMENT_READ_PERIOD_MS 5000
#define BTN_CONFIRM_PERIOD_MS 2000

#define CONFIRM_BTN 0
#define ONESHOT_BTN_TIMER_CALLBACK 1
#define PERIODIC_TIMER_CALLBACK 2

uint8_t btn_count = 0;

sl_sleeptimer_timer_handle_t periodic_timer_handle;
sl_sleeptimer_timer_handle_t oneshot_btn_timer_handle;

char remote_name[] = "Encrypted Advertiser";
// Gap service UUID
const uint8_t Gap_service_uuid[] = { 0x00, 0x18 };
// key material UUID
const uint8_t key_material_char_uuid[] = { 0x88, 0X2b };

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

sl_status_t find_advertiser_by_local_name(sl_bt_evt_scanner_extended_advertisement_report_t *adv_report)
{
  sl_status_t sc = SL_STATUS_FAIL;
  uint8_t ad_len;
  uint8_t ad_type;
  uint8_t i = 0;
  while (i < adv_report->data.len) {
    ad_len = adv_report->data.data[i];
    ad_type = adv_report->data.data[i + 1];
    if (ad_type == 0x09) {
      if (memcmp(remote_name, &(adv_report->data.data[i + 2]), ad_len - 1) == 0) {
        return SL_STATUS_OK;
      }
    }
    i = i + ad_len + 1;
  }
  return sc;
}

sl_status_t extract_and_deycrypt(sl_bt_evt_scanner_extended_advertisement_report_t *adv_report,
                                 sl_bt_ead_key_material_p key_material,
                                 sl_bt_ead_nonce_p nonce)
{
  sl_status_t sc = SL_STATUS_FAIL;
  uint8_t ad_len;
  uint8_t ad_type;
  uint8_t i = 0;
  struct sl_bt_ead_ad_structure_s advertisement_info;
  sl_bt_ead_randomizer_t randomizer;
  uint8_t encrypted_data_buffer[30];
  sl_bt_ead_mic_t mic;

  app_log("--------------------------------------------------------------------------\n\r");
  app_log("Decrypting\r\n");

  advertisement_info.length = sizeof(encrypted_data_buffer);
  advertisement_info.randomizer = &randomizer;
  advertisement_info.ad_data = encrypted_data_buffer;
  advertisement_info.mic = &mic;

  while (i < adv_report->data.len) {
    ad_len = adv_report->data.data[i];
    ad_type = adv_report->data.data[i + 1];
    if (ad_type == SL_BT_ENCRYPTED_DATA_AD_TYPE) {
      app_log("secret information encrypted:\r\n");
      for (uint8_t j = 0; j < ad_len + 1; j++) {
        app_log("%02X", adv_report->data.data[i + j]);
      }
      sc = sl_bt_ead_unpack_ad_data(&adv_report->data.data[i], &advertisement_info);
      if (sc != 0) {
        app_log("unpacking unsuccessful with rc %08lX\r\n", sc);
      }
      memcpy(nonce->randomizer, advertisement_info.randomizer, SL_BT_EAD_RANDOMIZER_SIZE);
      sc = sl_bt_ead_decrypt(key_material, nonce, (uint8_t *)advertisement_info.mic, advertisement_info.length, advertisement_info.ad_data);
      if (sc != 0) {
        app_log("decrypting unsuccessful with rc %08lX\r\n", sc);
      } else {
        app_log("\r\nDecrypted information:\r\n");
        for (int i = 0; i < advertisement_info.length; i++) {
          app_log("%c", advertisement_info.ad_data[i]);
        }
        app_log("\r\n");
      }
    }
    i = i + ad_len + 1;
  }
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
  sc = sl_sleeptimer_start_periodic_timer_ms(&periodic_timer_handle, ADVERTISEMENT_READ_PERIOD_MS, periodic_sleeptimer_callback, (void *)NULL, 0, 0);
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
  static uint8_t connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;
  static uint8_t key_need_update;
  static uint8_t pairing_state;
  static uint32_t gap_service_handle;
  static uint32_t key_material_char_handle;
  static uint8_t Gatt_procedure;
  static struct sl_bt_ead_key_material_s key_material;
  static struct sl_bt_ead_nonce_s nonce;
  static psa_key_id_t key_id = PSA_KEY_ID_NULL;
  static uint8_t decrypt_adv;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      pairing_state = 0;
      key_need_update = 1;
      decrypt_adv = 1;
      app_log("boot\r\n");
      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);
      sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_MITM_REQUIRED | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED | SL_BT_SM_CONFIGURATION_SC_ONLY | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED, sl_bt_sm_io_capability_displayyesno);
      app_assert_status(sc);
      sc = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_passive, 200, 200);
      app_assert_status(sc);
      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m, sl_bt_scanner_discover_observation);
      app_assert_status(sc);
      break;

    case sl_bt_evt_scanner_extended_advertisement_report_id:
      sl_bt_evt_scanner_extended_advertisement_report_t *adv_report = &evt->data.evt_scanner_extended_advertisement_report;
      if (find_advertiser_by_local_name(adv_report) == 0) {
        if (key_need_update == 1) {
          sc = sl_bt_scanner_stop();
          app_assert_status(sc);
          sc = sl_bt_connection_open(adv_report->address,
                                     adv_report->address_type,
                                     adv_report->primary_phy,
                                     &connection_handle);
          app_assert_status(sc);
        } else if (decrypt_adv) { // decrypt advertisemnt
          decrypt_adv = 0;
          sc = extract_and_deycrypt(adv_report, &key_material, &nonce);
          if (sc != 0) {
            app_log("failed to decrypt the message, fetching new key\r\n");
            key_need_update = 1;
          }
        }
      }
      break;

    case sl_bt_evt_scanner_legacy_advertisement_report_id:
      break;

    case sl_bt_evt_connection_opened_id:
      sl_bt_evt_connection_opened_t connection_data = evt->data.evt_connection_opened;
      app_log("connection opened\r\n");
      connection_handle = connection_data.connection;
      if (connection_data.bonding == SL_BT_INVALID_BONDING_HANDLE) {
        sl_bt_sm_increase_security(connection_handle);
      } else {
        app_log("discovering services\r\n");
        Gatt_procedure = SERVICE_DISCOVERY;
        sc = sl_bt_gatt_discover_primary_services_by_uuid(connection_handle, sizeof(Gap_service_uuid), Gap_service_uuid);
        app_assert_status(sc);
      }
      break;

    case sl_bt_evt_connection_closed_id:
      app_log("closed connection reason: 0x%4X\r\n", evt->data.evt_connection_closed.reason);
      connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;
      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m, sl_bt_scanner_discover_observation);
      app_assert_status(sc);
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

    case sl_bt_evt_gatt_service_id:
      app_log("Service discovery using UUID: ");
      gap_service_handle = evt->data.evt_gatt_service.service;
      for (int i = 0; i < evt->data.evt_gatt_service.uuid.len; i++) {
        app_log("%02X", evt->data.evt_gatt_service.uuid.data[i]);
      }

      app_log("\r\nresulted in the handle: %08lX\r\n", gap_service_handle);
      break;

    case sl_bt_evt_gatt_characteristic_id:
      app_log("Characteristic discovery using UUID: ");
      key_material_char_handle = evt->data.evt_gatt_characteristic.characteristic;
      for (int i = 0; i < evt->data.evt_gatt_characteristic.uuid.len; i++) {
        app_log("%02X", evt->data.evt_gatt_characteristic.uuid.data[i]);
      }
      app_log("\r\nresulted in the handle: %04X \r\n", evt->data.evt_gatt_characteristic.characteristic);
      break;

    case sl_bt_evt_gatt_characteristic_value_id:
      // copy received Gatt value to the key material
      memcpy(key_material.key, evt->data.evt_gatt_characteristic_value.value.data, SL_BT_EAD_SESSION_KEY_SIZE);
      memcpy(key_material.iv, evt->data.evt_gatt_characteristic_value.value.data + SL_BT_EAD_SESSION_KEY_SIZE, SL_BT_EAD_IV_SIZE);
      // copy iv to the nonce struct
      memcpy(nonce.iv, key_material.iv, SL_BT_EAD_IV_SIZE);
      app_log("key material: ");
      for (uint8_t i = 0; i < SL_BT_EAD_SESSION_KEY_SIZE; i++) {
        app_log("%02X:", key_material.key[i]);
      }
      app_log("\r\ninitialization vector: ");
      for (uint8_t i = 0; i < SL_BT_EAD_IV_SIZE; i++) {
        app_log("%02X:", key_material.iv[i]);
      }
      app_log("\r\n");
      sc = sl_bt_ead_store_key(PSA_KEY_USAGE_DECRYPT, PSA_KEY_LIFETIME_VOLATILE, &key_material, &key_id);
      app_assert_status(sc);
      key_need_update = 0;
      sl_bt_connection_close(connection_handle);
      break;

    case sl_bt_evt_gatt_procedure_completed_id:
      app_log("Gatt procedure result:  0x%04X \r\n", evt->data.evt_gatt_procedure_completed.result);
      if (Gatt_procedure == SERVICE_DISCOVERY) {
        Gatt_procedure = CHARACHTERISTIC_DISCOVERY;
        sc = sl_bt_gatt_discover_characteristics_by_uuid(connection_handle, gap_service_handle, sizeof(key_material_char_uuid), key_material_char_uuid);
      } else if (Gatt_procedure == CHARACHTERISTIC_DISCOVERY) {
        Gatt_procedure = CHARACHTERISTIC_READ;
        sl_bt_sm_increase_security(connection_handle);
        sl_bt_gatt_read_characteristic_value(connection_handle, key_material_char_handle);
      }
      break;

    case sl_bt_evt_sm_confirm_passkey_id:
      pairing_state = 1;
      app_log("The passkey is: %06li\r\n", evt->data.evt_sm_confirm_passkey.passkey);
      app_log("Please press btn0 once to refuse bonding or twice to accept bonding\n\r");
      break;

    case sl_bt_evt_sm_bonded_id:
      app_log("device bonded successfully\r\n");
      pairing_state = 0;
      sc = sl_bt_connection_close(connection_handle);
      app_assert_status(sc);
      break;

    case sl_bt_evt_sm_bonding_failed_id:
      app_log("bonding failed, reason 0x%2X\r\n",
              evt->data.evt_sm_bonding_failed.reason);
      sc = sl_bt_connection_close(connection_handle);
      app_assert_status(sc);
      break;

    case sl_bt_evt_system_external_signal_id:
      if (evt->data.evt_system_external_signal.extsignals == PERIODIC_TIMER_CALLBACK) {
        decrypt_adv = 1;
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
