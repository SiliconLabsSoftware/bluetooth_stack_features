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
#include "app_log.h"
#include <stdio.h>
#include <inttypes.h>
#include "sl_bt_ead_core.h"
#include "psa/crypto.h"
#include "psa/crypto_values.h"
#include "sl_sleeptimer.h"

#define U1250_TO_MS(x)   ((uint32_t)(x) * 1250u / 1000u)  /* 1.25 ms units -> ms */
#define U0125_TO_US(x)   ((uint32_t)(x) * 125u)           /* 0.125 ms units -> us */

#define PERIODIC_SYNC_SKIP 0
#define PERIODIC_SYNC_TIMEOUT 600

#define gattdb_tag_id                                   0x15
#define gattdb_ap_sync_key_material                     0x17
#define gattdb_tag_response_key_material                0x19

#define KEY_MATERIAL_SIZE  (SL_BT_EAD_SESSION_KEY_SIZE + SL_BT_EAD_IV_SIZE)
uint8_t tag_id = 0;

static struct sl_bt_ead_key_material_s Ap_sync_key_material = { 0 };
static struct sl_bt_ead_key_material_s tag_response_key_material = { 0 };

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
static bool advertising_active = false;

// Response data
#define BLE_EA_RSP_DATA_LEN 0xBF
#define RESPONSE_PLAINTEXT_MAX_LEN 16
uint8_t response_buffer[BLE_EA_RSP_DATA_LEN];
uint8_t secret_data_len = 0;
uint8_t secret_number = 0;

static sl_sleeptimer_timer_handle_t out_of_sync_timer;
#define TAG_OUT_OF_SYNC_PERIOD_MS 10000
#define TAG_OUT_OF_SYNC_SIGNAL 0x01
static void out_of_sync_cb(sl_sleeptimer_timer_handle_t *handle, void *data);

uint8_t subevents_subscription_list[] = { 0 };

uint16_t pawr_handle = SL_BT_INVALID_SYNC_HANDLE;
uint8_t  connection = SL_BT_INVALID_CONNECTION_HANDLE;
bd_addr ap_address = { 0 };

sl_status_t extract_and_decrypt(sl_bt_evt_pawr_sync_subevent_report_t *sync_report,
                                sl_bt_ead_key_material_p key_material,
                                sl_bt_ead_nonce_p nonce);

sl_status_t construct_response_payload(sl_bt_ead_key_material_p key_material, sl_bt_ead_nonce_p nonce, uint8_t* secret_payload, uint8_t* secret_payload_len);

// Application Init.
void app_init(void)
{
  sl_status_t sc;
  uint32_t out_of_sync_ticks;

  sc = sl_sleeptimer_init();
  app_assert_status(sc);

  sc = sl_sleeptimer_ms32_to_tick(TAG_OUT_OF_SYNC_PERIOD_MS,
                                  &out_of_sync_ticks);
  app_assert_status(sc);
  sc = sl_sleeptimer_start_periodic_timer(&out_of_sync_timer,
                                          out_of_sync_ticks,
                                          out_of_sync_cb,
                                          NULL,
                                          0,
                                          0);
  app_assert_status(sc);
}

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
  static struct sl_bt_ead_nonce_s ap_sync_nonce, tag_resp_nonce;
  static psa_key_id_t ap_sync_key_id = PSA_KEY_ID_NULL;
  static psa_key_id_t tag_resp_key_id = PSA_KEY_ID_NULL;

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
    {
      bd_addr address;
      uint8_t address_type;
      app_log_info("++++++++++++++++++++++++++++++++++++ Tag boot ++++++++++++++++++++++++++++++++++++\r\n");
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

      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);
      sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_BONDING_REQUIRED
                              | SL_BT_SM_CONFIGURATION_SC_ONLY
                              | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
                              sl_bt_sm_io_capability_noinputnooutput);
      app_assert_status(sc);

      sc =  sl_bt_past_receiver_set_default_sync_receive_parameters(
        sl_bt_past_receiver_mode_synchronize,
        PERIODIC_SYNC_SKIP,
        PERIODIC_SYNC_TIMEOUT,
        sl_bt_sync_report_all
        );

      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      sc = sl_bt_extended_advertiser_generate_data(advertising_set_handle,
                                                   sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);
      // Start advertising and enable connections.
      sc = sl_bt_extended_advertiser_start(advertising_set_handle,
                                           sl_bt_extended_advertiser_connectable, 0x00);
      app_assert_status(sc);
      advertising_active = true;
      app_log_info("starting provisioning advertisement\r\n");
      break;
    }

    case sl_bt_evt_connection_opened_id:
    {
      app_log_info("connection opened\r\n");
      connection = evt->data.evt_connection_opened.connection;
      ap_address = evt->data.evt_connection_opened.address;
      advertising_active = false;
      break;
    }

    case sl_bt_evt_connection_closed_id:
    {
      app_log_info("connection closed, reason 0x%2X\r\n", evt->data.evt_connection_closed.reason);
      connection = SL_BT_INVALID_CONNECTION_HANDLE;
      break;
    }

    case sl_bt_evt_connection_parameters_id: {
      switch (evt->data.evt_connection_parameters.security_mode) {
        case sl_bt_connection_mode1_level1:
          app_log_info("No Security\r\n");
          break;
        case sl_bt_connection_mode1_level2:
          app_log_info("Unauthenticated pairing with encryption\r\n");
          break;
        case sl_bt_connection_mode1_level3:
          app_log_info("Authenticated pairing with encryption\r\n");
          break;
        case sl_bt_connection_mode1_level4:
          app_log_info("Authenticated Secure Connections pairing with encryption\r\n");
          break;
      }
      break;
    }

    case sl_bt_evt_gatt_server_attribute_value_id:
    {
      sl_bt_evt_gatt_server_attribute_value_t *att_value = &evt->data.evt_gatt_server_attribute_value;
      switch (att_value->attribute) {
        case gattdb_tag_id:
          app_log_info("assigned tag id %d\r\n", att_value->value.data[0]);
          tag_id =  att_value->value.data[0];
          subevents_subscription_list[0]  = tag_id;
          break;
        case gattdb_ap_sync_key_material:

          memcpy(Ap_sync_key_material.key,
                 att_value->value.data,
                 SL_BT_EAD_SESSION_KEY_SIZE);

          memcpy(Ap_sync_key_material.iv,
                 att_value->value.data + SL_BT_EAD_SESSION_KEY_SIZE,
                 SL_BT_EAD_IV_SIZE);

          memcpy(ap_sync_nonce.iv,
                 Ap_sync_key_material.iv,
                 SL_BT_EAD_IV_SIZE);
          sc = sl_bt_ead_store_key(PSA_KEY_USAGE_DECRYPT, PSA_KEY_LIFETIME_VOLATILE, &Ap_sync_key_material, &ap_sync_key_id);
          app_assert_status(sc);
          break;
        case gattdb_tag_response_key_material:
          memcpy(tag_response_key_material.key,
                 att_value->value.data,
                 SL_BT_EAD_SESSION_KEY_SIZE);

          memcpy(tag_response_key_material.iv,
                 att_value->value.data + SL_BT_EAD_SESSION_KEY_SIZE,
                 SL_BT_EAD_IV_SIZE);

          memcpy(tag_resp_nonce.iv,
                 tag_response_key_material.iv,
                 SL_BT_EAD_IV_SIZE);
          sc = sl_bt_ead_store_key(PSA_KEY_USAGE_ENCRYPT, PSA_KEY_LIFETIME_VOLATILE, &tag_response_key_material, &tag_resp_key_id);
          app_assert_status(sc);
          break;
      }
      break;
    }

    case sl_bt_evt_pawr_sync_transfer_received_id:
    {
      sl_bt_evt_pawr_sync_transfer_received_t sync_report = evt->data.evt_pawr_sync_transfer_received;
      app_log(" sync status %02X\r\n", sync_report.status);
      app_log(
        "PAwR sync opened:\r\n"
        "adv_interval= %" PRIu32 "ms\r\n"
                                 "num_subevents= %d\r\n"
                                 "subevent_interval= %" PRIu32 "ms\r\n"
                                                               "rsp_slot_delay= %" PRIu32 "ms\r\n"
                                                                                          "rsp_slot_spacing= %" PRIu32 "us\r\n",
        U1250_TO_MS(sync_report.adv_interval),
        sync_report.num_subevents,
        U1250_TO_MS(sync_report.subevent_interval),
        U1250_TO_MS(sync_report.response_slot_delay),
        U0125_TO_US(sync_report.response_slot_spacing)
        );
      pawr_handle = sync_report.sync;
      sc = sl_bt_pawr_sync_set_sync_subevents(pawr_handle, sizeof(subevents_subscription_list), subevents_subscription_list);
      app_assert_status(sc);
      sc = sl_bt_connection_close(sync_report.connection);
      app_assert_status(sc);
      break;
    }

    case sl_bt_evt_sync_closed_id:
    {
      app_log_info("Sync closed: handle=0x%04X reason=0x%04X\r\n",
                   evt->data.evt_sync_closed.sync,
                   evt->data.evt_sync_closed.reason);
      pawr_handle = SL_BT_INVALID_SYNC_HANDLE;
      break;
    }

    case sl_bt_evt_pawr_sync_opened_id:
    {
      app_log_info("sync opened\r\n");
      break;
    }

    case sl_bt_evt_pawr_sync_subevent_report_id:
    {
      sl_bt_evt_pawr_sync_subevent_report_t *sync_report = &evt->data.evt_pawr_sync_subevent_report;
      for (uint8_t index = 0; index < sizeof(subevents_subscription_list) / sizeof(uint8_t); index++) {
        if (subevents_subscription_list[index] == sync_report->subevent) {
          app_log("--------------------------------------------------------------------------\n\r");
          app_log_info(
            "PAwR report: sync=0x%04X subevent=%u status=%u len=%u counter=%u\r\n",
            sync_report->sync,
            sync_report->subevent,
            sync_report->data_status,
            sync_report->data.len,
            sync_report->counter);
          if (sync_report->data_status == 255) {
            app_log_info("Failed to receive subevent data in this subevent\r\n");
            break;
          }
          sc = extract_and_decrypt(sync_report, &Ap_sync_key_material, &ap_sync_nonce);
          if (sc != 0) {
            app_log("failed to decrypt the message\r\n");
            sc = sl_bt_sync_close(sync_report->sync);
            app_assert_status(sc);
            pawr_handle = SL_BT_INVALID_SYNC_HANDLE;
            break;
          }
          sc = sl_bt_ead_randomizer_update(&tag_resp_nonce);
          app_assert_status(sc);

          secret_number++;

          secret_data_len = BLE_EA_RSP_DATA_LEN;

          sc = construct_response_payload(&tag_response_key_material,
                                          &tag_resp_nonce,
                                          response_buffer,
                                          &secret_data_len);
          app_assert_status(sc);

          sc = sl_bt_pawr_sync_set_response_data(pawr_handle,
                                                 sync_report->event_counter,
                                                 sync_report->subevent,
                                                 sync_report->subevent,
                                                 tag_id,
                                                 secret_data_len,
                                                 response_buffer);
          if (sc == SL_STATUS_BT_CTRL_TOO_LATE) {
            app_log_warning("TAG PAWR response was too late, skipping\r\n");
          } else {
            app_assert_status(sc);
          }
        }
      }
      break;
    }

    case sl_bt_evt_sm_confirm_bonding_id: {
      app_log_info("new bonding request\r\n");
      sl_bt_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 0x01);
      break;
    }

    case sl_bt_evt_sm_bonded_id:
    {
      app_log_info("device bonded successfully\r\n");
      break;
    }

    case sl_bt_evt_sm_bonding_failed_id:
    {
      app_log_info("bonding failed, peer address: %2X:%2X:%2X:%2X:%2X:%2X reason 0x%2X, closing connection\r\n",
                   ap_address.addr[5],
                   ap_address.addr[4],
                   ap_address.addr[3],
                   ap_address.addr[2],
                   ap_address.addr[1],
                   ap_address.addr[0],
                   evt->data.evt_sm_bonding_failed.reason);
      break;
    }

    case sl_bt_evt_system_external_signal_id:
    {
      if (evt->data.evt_system_external_signal.extsignals & TAG_OUT_OF_SYNC_SIGNAL) {
        if (pawr_handle == SL_BT_INVALID_SYNC_HANDLE && connection == SL_BT_INVALID_CONNECTION_HANDLE && !advertising_active) {
          sc = sl_bt_extended_advertiser_generate_data(advertising_set_handle,
                                                       sl_bt_advertiser_general_discoverable);
          app_assert_status(sc);
          sc = sl_bt_extended_advertiser_start(advertising_set_handle,
                                               sl_bt_extended_advertiser_connectable, 0x00);
          app_assert_status(sc);
          advertising_active = true;
          app_log_info("sync lost, restarting provisioning advertisement\r\n");
        }
      }
    }
    break;

    default:
    {
      // app_log_info("untracked  event id %08lX\r\n",(unsigned long)SL_BT_MSG_ID(evt->header));
      break;
    }
  }
}

sl_status_t extract_and_decrypt(sl_bt_evt_pawr_sync_subevent_report_t *sync_report,
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

  app_log("Decrypting\r\n");

  advertisement_info.length = sizeof(encrypted_data_buffer);
  advertisement_info.randomizer = &randomizer;
  advertisement_info.ad_data = encrypted_data_buffer;
  advertisement_info.mic = &mic;

  while (i < sync_report->data.len) {
    ad_len = sync_report->data.data[i];
    ad_type = sync_report->data.data[i + 1];
    if (ad_type == SL_BT_ENCRYPTED_DATA_AD_TYPE) {
      app_log("Subevent information encrypted:\r\n");
      for (uint8_t j = 0; j < ad_len + 1; j++) {
        app_log("%02X", sync_report->data.data[i + j]);
      }
      sc = sl_bt_ead_unpack_ad_data(&sync_report->data.data[i], &advertisement_info);
      if (sc != 0) {
        app_log("unpacking unsuccessful with rc %08lX\r\n", sc);
      }
      memcpy(nonce->randomizer, advertisement_info.randomizer, SL_BT_EAD_RANDOMIZER_SIZE);
      sc = sl_bt_ead_decrypt(key_material, nonce, (uint8_t *)advertisement_info.mic, advertisement_info.length, advertisement_info.ad_data);
      if (sc != 0) {
        app_log("decrypting unsuccessful with rc %08lX\r\n", sc);
      } else {
        app_log("\r\nSubevent information decrypted:");
        for (uint8_t i = 2; i < advertisement_info.length; i++) {
          app_log("%c", advertisement_info.ad_data[i]);
        }
        app_log("\r\n");
      }
    }
    i = i + ad_len + 1;
  }
  return sc;
}

sl_status_t construct_response_payload(sl_bt_ead_key_material_p key_material,
                                       sl_bt_ead_nonce_p nonce,
                                       uint8_t *response_payload,
                                       uint8_t *response_payload_len)
{
  sl_status_t sc;
  uint8_t plaintext[RESPONSE_PLAINTEXT_MAX_LEN];
  uint8_t packed_len = BLE_EA_RSP_DATA_LEN;
  size_t plaintext_len;
  sl_bt_ead_mic_t mic;
  struct sl_bt_ead_ad_structure_s encrypted_ad_structure;

  snprintf((char *)plaintext,
           sizeof(plaintext),
           "secret%02d",
           secret_number);

  plaintext_len = strlen((char *)plaintext);

  app_log("Response Information before encryption: %s\r\n",
          plaintext);

  sc = sl_bt_ead_encrypt(key_material,
                         nonce,
                         plaintext_len,
                         plaintext,
                         mic);
  app_assert_status(sc);

  encrypted_ad_structure.length = plaintext_len;
  encrypted_ad_structure.ad_type = SL_BT_ENCRYPTED_DATA_AD_TYPE;
  encrypted_ad_structure.ad_data = plaintext;
  encrypted_ad_structure.randomizer = &(nonce->randomizer);
  encrypted_ad_structure.mic = &mic;

  sc = sl_bt_ead_pack_ad_data(&encrypted_ad_structure,
                              &packed_len,
                              response_payload);
  app_assert_status(sc);

  *response_payload_len = packed_len;

  app_log("Response Information after encryption:\r\n");
  for (uint8_t i = 0; i < packed_len; i++) {
    app_log("%02X", response_payload[i]);
  }
  app_log("\r\n");

  return SL_STATUS_OK;
}

static void out_of_sync_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  sl_bt_external_signal(TAG_OUT_OF_SYNC_SIGNAL);
}
