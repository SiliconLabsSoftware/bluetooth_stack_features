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
#include <stdio.h>
#include <string.h>

#include "app_assert.h"
#include "app.h"
#include "app_log.h"
#include "psa/crypto.h"
#include "psa/crypto_values.h"
#include "sl_bluetooth_connection_config.h"
#include "sl_bt_api.h"
#include "sl_bt_ead_core.h"
#include "sl_main_init.h"
#include "sl_sleeptimer.h"
#include "nvm3.h"
#include "nvm3_default.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
#define KEY_MATERIAL_SIZE  (SL_BT_EAD_SESSION_KEY_SIZE + SL_BT_EAD_IV_SIZE)
#define AP_SYNC_KEY_MATERIAL_ID 0x0001
// user defined NVM3 objects should be placed below 0x10000.
#define AP_SYNC_KEY_IV_NVM3_KEY 0x00000

#define PERIODIC_ADV_INTERVAL 2400
#define SUBEVENT_NUMBER 3
#define SUBEVENT_INTERVAL 255
#define RESPONSE_SLOT_DELAY 40
#define RESPONSE_SLOT_SPACING 80
#define RESPONSE_SLOT_NUMBER_PER_SUBVENT SUBEVENT_NUMBER

#define TAG_STALE_TIMEOUT_MS      30000u
#define TAG_STALE_SCAN_PERIOD_MS   5000u
#define TAG_SWEEP_SIGNAL 0x01
#define TAG_ID_UNASSIGNED 0xFF
#define MAX_TAGS SUBEVENT_NUMBER
#define BLE_EA_ADV_DATA_LEN 0xBF

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------
typedef enum tag_record_state_t {
  TAG_RECORD_FREE = 0,
  TAG_RECORD_PROVISIONING,
  TAG_RECORD_ACTIVE,
  TAG_RECORD_STALE
} tag_record_state_t;

typedef struct tag_record_t {
  tag_record_state_t state;

  uint8_t tag_id;
  uint8_t response_subevent;
  uint8_t response_slot;

  uint64_t last_seen_tick;
  bd_addr address;
  uint8_t address_type;
  uint8_t bonding;
  struct sl_bt_ead_key_material_s response_key_material;
  struct sl_bt_ead_nonce_s response_nonce;
} tag_record_t;

typedef enum gatt_sequence_state_t {
  GATT_IDLE = 0,
  GATT_DISCOVER_SERVICE,
  GATT_DISCOVER_TAG_ID,
  GATT_READ_TAG_ID,
  GATT_WRITE_TAG_ID,

  GATT_DISCOVER_AP_SYNC_KEY,
  GATT_WRITE_AP_SYNC_KEY,

  GATT_DISCOVER_TAG_RESP_KEY,
  GATT_WRITE_TAG_RESP_KEY,

  GATT_FAILED,
  GATT_DONE
} gatt_sequence_state_t;

// -----------------------------------------------------------------------------
// Static data
// -----------------------------------------------------------------------------
static sl_sleeptimer_timer_handle_t tag_sweep_timer;
static uint64_t tag_stale_timeout_ticks;

static uint8_t pawr_advertising_set_handle = 0xff;
static uint8_t advertisement_buffer[BLE_EA_ADV_DATA_LEN];
static char name[] = "Encrypted Advertiser";
static char secret_data[10];
static uint8_t secret_number = 0;

static tag_record_t tags[MAX_TAGS] = { 0 };

static const uint8_t SecureTagServiceUUID[] = {
  0x81, 0xC2, 0x00, 0x2D,
  0x31, 0xF4, 0xB0, 0xBF,
  0x2B, 0x42, 0x49, 0x68,
  0xC7, 0x25, 0x71, 0x41
};

static const uint8_t TagIDCharUUID[16] = {
  0x91, 0xD4, 0x1E, 0x54,
  0x4B, 0xE1, 0xAC, 0x9B,
  0x08, 0x46, 0xEC, 0x60,
  0x26, 0x53, 0x2A, 0xC6
};

static const uint8_t APSyncKeyMaterialUUID[16] = {
  0xCC, 0x44, 0x0B, 0x61,
  0xB3, 0x00, 0xD4, 0xB9,
  0x3B, 0x40, 0x33, 0x6E,
  0x31, 0xF0, 0x51, 0x2A
};

static const uint8_t TagResponseKeyMaterialUUID[16] = {
  0x8C, 0xED, 0xC7, 0x06,
  0x34, 0xA9, 0xDD, 0xB8,
  0x9E, 0x46, 0x57, 0xE3,
  0xCC, 0x4B, 0xAB, 0x8D
};

static uint32_t SecureTagServiceHandle = 0;
static uint16_t TagIDCharHandle = 0;
static uint16_t APSyncKeyMaterialCharHandle = 0;
static uint16_t TagResponseKeyMaterialCharHandle = 0;

static struct sl_bt_ead_key_material_s Ap_sync_key_material = { 0 };
static psa_key_id_t ead_ap_sync_key_id = AP_SYNC_KEY_MATERIAL_ID;

#define TAG_RESP_KEY_ID_BASE 0x0002
// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
static sl_status_t find_tag(sl_bt_evt_scanner_extended_advertisement_report_t *report);
static sl_status_t construct_advertisement_payload(sl_bt_ead_key_material_p key_material,
                                                   sl_bt_ead_nonce_p nonce,
                                                   uint8_t *index);
static sl_status_t initialize_key(sl_bt_ead_key_material_p key_material);
static sl_status_t decrypt_tag_response(sl_bt_evt_pawr_advertiser_response_report_t *report,
                                        tag_record_t *tag);
static sl_status_t resolve_tag_id(uint8_t claimed_id,
                                  bd_addr address,
                                  uint8_t address_type,
                                  uint8_t *resolved_id);
static void stale_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data);
static void sweep_stale_tags(void);
static void export_ead_key_value(psa_key_id_t key_id, struct sl_bt_ead_key_material_s key_material, uint8_t key_material_value[KEY_MATERIAL_SIZE]);

// -----------------------------------------------------------------------------
// Application entry points
// -----------------------------------------------------------------------------
void app_init(void)
{
  sl_status_t sc;
  psa_status_t psa_sc;
  Ecode_t nvm3_sc;
  uint32_t stale_scan_period_ticks;
  uint32_t timeout_ticks;
  psa_key_handle_t ead_ap_sync_key_handle;

  app_log_info(" ++++++++++++++++++++++++++++++++++++ Ap boot ++++++++++++++++++++++++++++++++++++\r\n");

  sc = sl_sleeptimer_init();
  app_assert_status(sc);

  sc = sl_sleeptimer_ms32_to_tick(TAG_STALE_SCAN_PERIOD_MS,
                                  &stale_scan_period_ticks);
  app_assert_status(sc);

  sc = sl_sleeptimer_ms32_to_tick(TAG_STALE_TIMEOUT_MS, &timeout_ticks);
  app_assert_status(sc);
  tag_stale_timeout_ticks = timeout_ticks;

  sc = sl_sleeptimer_start_periodic_timer(&tag_sweep_timer,
                                          stale_scan_period_ticks,
                                          stale_timer_cb,
                                          NULL,
                                          0,
                                          0);
  app_assert_status(sc);

  psa_sc = psa_crypto_init();
  app_assert_status(psa_sc);
  psa_sc = psa_open_key(ead_ap_sync_key_id,
                        &ead_ap_sync_key_handle);
  // check if AP sync key exists otherwise create key;
  if (psa_sc == PSA_SUCCESS) {
    app_log_info("Existing AP sync PSA key found\r\n");
    psa_sc = psa_close_key(ead_ap_sync_key_handle);
    app_assert_status(psa_sc);
    Ap_sync_key_material.key_id = ead_ap_sync_key_id;
    nvm3_sc = nvm3_readData(nvm3_defaultHandle,
                            AP_SYNC_KEY_IV_NVM3_KEY,
                            Ap_sync_key_material.iv,
                            sizeof(Ap_sync_key_material.iv));
    app_assert_status(nvm3_sc);
  } else if (psa_sc == PSA_ERROR_DOES_NOT_EXIST) {
    app_log_info("AP sync key missing; creating it\r\n");
    sc = initialize_key(&Ap_sync_key_material);
    app_assert_status(sc);
    sc = sl_bt_ead_store_key(PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_EXPORT,
                             PSA_KEY_LIFETIME_PERSISTENT,
                             &Ap_sync_key_material,
                             &ead_ap_sync_key_id);
    app_assert_status(sc);
    nvm3_sc = nvm3_writeData(nvm3_defaultHandle,
                             AP_SYNC_KEY_IV_NVM3_KEY,
                             Ap_sync_key_material.iv,
                             sizeof(Ap_sync_key_material.iv));
    app_assert_status(nvm3_sc);
  } else {
    app_log_error("Failed to open EAD key ID=0x%08lx, PSA status=0x%08lx\r\n",
                  (unsigned long)ead_ap_sync_key_id,
                  (unsigned long)psa_sc);
  }
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
  static uint8_t connection_handle = 0xFF;
  static gatt_sequence_state_t gatt_state = GATT_IDLE;
  static tag_record_t connected_tag = { 0 };
  static uint8_t provisioning_tag_id_value = 0;
  static struct sl_bt_ead_nonce_s nonce;
  static bool connection_in_progress = false;

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id: {
      bd_addr address;
      uint8_t address_type;
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

      sc = sl_bt_ead_session_init(&Ap_sync_key_material, NULL, &nonce);
      app_assert_status(sc);

      sc = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_passive,
                                        200,
                                        200);
      app_assert_status(sc);

      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m,
                               sl_bt_scanner_discover_observation);
      app_assert_status(sc);
      app_log_info("Scanning for Tags\r\n");

      sc = sl_bt_advertiser_create_set(&pawr_advertising_set_handle);
      app_assert_status(sc);

      sc = sl_bt_pawr_advertiser_start(pawr_advertising_set_handle,
                                       PERIODIC_ADV_INTERVAL,
                                       PERIODIC_ADV_INTERVAL,
                                       0,
                                       SUBEVENT_NUMBER,
                                       SUBEVENT_INTERVAL,
                                       RESPONSE_SLOT_DELAY,
                                       RESPONSE_SLOT_SPACING,
                                       RESPONSE_SLOT_NUMBER_PER_SUBVENT);
      app_assert_status(sc);

      break;
    }

    case sl_bt_evt_scanner_extended_advertisement_report_id: {
      sl_bt_evt_scanner_extended_advertisement_report_t *report =
        &evt->data.evt_scanner_extended_advertisement_report;

      if (connection_in_progress) {
        break;
      }

      sc = find_tag(report);
      if (sc == SL_STATUS_OK) {
        connection_in_progress = true;
        sc = sl_bt_connection_open(report->address,
                                   report->address_type,
                                   sl_bt_gap_phy_1m,
                                   &connection_handle);
        app_assert_status(sc);
      }
      break;
    }

    case sl_bt_evt_connection_opened_id: {
      sl_bt_evt_connection_opened_t *connection = &evt->data.evt_connection_opened;
      app_log_info(
        "opened: connection=%u bonding=%u "
        "peer=%02X:%02X:%02X:%02X:%02X:%02X type=%u\r\n",
        connection->connection,
        connection->bonding,
        connection->address.addr[5],
        connection->address.addr[4],
        connection->address.addr[3],
        connection->address.addr[2],
        connection->address.addr[1],
        connection->address.addr[0],
        connection->address_type);
      mbedtls_platform_zeroize(&connected_tag, sizeof(connected_tag));
      connected_tag.tag_id = TAG_ID_UNASSIGNED;
      connected_tag.state = TAG_RECORD_PROVISIONING;
      connected_tag.address = connection->address;
      connected_tag.address_type = connection->address_type;
      connected_tag.bonding = connection->bonding;
      gatt_state = GATT_IDLE;
      break;
    }

    case sl_bt_evt_system_error_id: {
      const sl_bt_evt_system_error_t *error =
        &evt->data.evt_system_error;

      app_log_error("Bluetooth system error: reason=0x%04X data_len=%u data=",
                    error->reason,
                    error->data.len);

      for (uint8_t i = 0; i < error->data.len; i++) {
        app_log("%02X", error->data.data[i]);
      }

      app_log("\r\n");
      break;
    }

    case sl_bt_evt_connection_parameters_id: {
      sl_bt_evt_connection_parameters_t *connection = &evt->data.evt_connection_parameters;

      switch (connection->security_mode) {
        case sl_bt_connection_mode1_level1:
          app_log_info("No Security\r\n");
          sc = sl_bt_gatt_discover_primary_services_by_uuid(connection->connection,
                                                            sizeof(SecureTagServiceUUID),
                                                            SecureTagServiceUUID);
          app_assert_status(sc);
          gatt_state = GATT_DISCOVER_SERVICE;
          break;
        case sl_bt_connection_mode1_level2:
          app_log_info("Unauthenticated pairing with encryption\r\n");
          sc = sl_bt_gatt_write_characteristic_value(connection->connection,
                                                     TagIDCharHandle,
                                                     sizeof(provisioning_tag_id_value),
                                                     &provisioning_tag_id_value);
          app_assert_status(sc);
          gatt_state = GATT_WRITE_TAG_ID;

          break;
        case sl_bt_connection_mode1_level3:
          app_log_info("Authenticated pairing with encryption\r\n");
          break;
        case sl_bt_connection_mode1_level4:
          app_log_info("Authenticated Secure connections pairing with encryption\r\n");
          break;
        default:
          break;
      }
      break;
    }

    case sl_bt_evt_gatt_service_id: {
      sl_bt_evt_gatt_service_t *service = &evt->data.evt_gatt_service;
      if (memcmp(service->uuid.data,
                 SecureTagServiceUUID,
                 sizeof(SecureTagServiceUUID)) == 0) {
        SecureTagServiceHandle = service->service;
      }
      break;
    }

    case sl_bt_evt_gatt_characteristic_id: {
      const sl_bt_evt_gatt_characteristic_t *char_evt =
        &evt->data.evt_gatt_characteristic;

      switch (gatt_state) {
        case GATT_DISCOVER_TAG_ID:
          TagIDCharHandle = char_evt->characteristic;
          break;
        case GATT_DISCOVER_AP_SYNC_KEY:
          APSyncKeyMaterialCharHandle = char_evt->characteristic;
          break;
        case GATT_DISCOVER_TAG_RESP_KEY:
          TagResponseKeyMaterialCharHandle = char_evt->characteristic;
          break;
        default:
          break;
      }
      break;
    }

    case sl_bt_evt_gatt_characteristic_value_id: {
      const sl_bt_evt_gatt_characteristic_value_t *char_evt =
        &evt->data.evt_gatt_characteristic_value;
      if (char_evt->characteristic == TagIDCharHandle) {
        connected_tag.tag_id = char_evt->value.data[0];
      }

      break;
    }

    case sl_bt_evt_gatt_procedure_completed_id: {
      const sl_bt_evt_gatt_procedure_completed_t *gatt_evt =
        &evt->data.evt_gatt_procedure_completed;

      if (gatt_evt->result != SL_STATUS_OK) {
        gatt_state = GATT_FAILED;
        app_log_info("Gatt procedure failed rc %04X\r\n", gatt_evt->result);
        sc = sl_bt_connection_close(gatt_evt->connection);
        app_assert_status(sc);
        break;
      }

      switch (gatt_state) {
        case GATT_DISCOVER_SERVICE:
          sc = sl_bt_gatt_discover_characteristics_by_uuid(gatt_evt->connection,
                                                           SecureTagServiceHandle,
                                                           sizeof(TagIDCharUUID),
                                                           TagIDCharUUID);
          app_assert_status(sc);
          gatt_state = GATT_DISCOVER_TAG_ID;
          break;

        case GATT_DISCOVER_TAG_ID:
          sc = sl_bt_gatt_discover_characteristics_by_uuid(gatt_evt->connection,
                                                           SecureTagServiceHandle,
                                                           sizeof(APSyncKeyMaterialUUID),
                                                           APSyncKeyMaterialUUID);
          app_assert_status(sc);
          gatt_state = GATT_DISCOVER_AP_SYNC_KEY;
          break;

        case GATT_DISCOVER_AP_SYNC_KEY:
          sc = sl_bt_gatt_discover_characteristics_by_uuid(gatt_evt->connection,
                                                           SecureTagServiceHandle,
                                                           sizeof(TagResponseKeyMaterialUUID),
                                                           TagResponseKeyMaterialUUID);
          app_assert_status(sc);
          gatt_state = GATT_DISCOVER_TAG_RESP_KEY;
          break;

        case GATT_DISCOVER_TAG_RESP_KEY:
          sc = sl_bt_gatt_read_characteristic_value(gatt_evt->connection,
                                                    TagIDCharHandle);
          app_assert_status(sc);
          gatt_state = GATT_READ_TAG_ID;
          break;

        case GATT_READ_TAG_ID:
        {
          sc = resolve_tag_id(connected_tag.tag_id,
                              connected_tag.address,
                              connected_tag.address_type,
                              &provisioning_tag_id_value);
          if (sc != SL_STATUS_OK) {
            app_log_error("No free/stale tag record available\r\n");
            sc = sl_bt_connection_close(gatt_evt->connection);
            app_assert_status(sc);
            gatt_state = GATT_FAILED;
            break;
          }
          connected_tag.tag_id = provisioning_tag_id_value;
          sc = sl_bt_sm_increase_security(gatt_evt->connection);
          app_assert_status(sc);
          app_log_info("requested to increase security\r\n");
          break;
        }

        case GATT_WRITE_TAG_ID:
        {
          connected_tag.response_subevent = provisioning_tag_id_value;
          connected_tag.response_slot = provisioning_tag_id_value;
          app_log_info("Tag ID successfully written, writing AP sync key material\r\n");

          uint8_t Ap_sync_key_material_value[KEY_MATERIAL_SIZE] = { 0 };
          export_ead_key_value(ead_ap_sync_key_id, Ap_sync_key_material, Ap_sync_key_material_value);
          sc = sl_bt_gatt_write_characteristic_value(gatt_evt->connection,
                                                     APSyncKeyMaterialCharHandle,
                                                     sizeof(Ap_sync_key_material_value),
                                                     Ap_sync_key_material_value);
          app_assert_status(sc);
          mbedtls_platform_zeroize(Ap_sync_key_material_value,
                                   sizeof(Ap_sync_key_material_value));
          gatt_state = GATT_WRITE_AP_SYNC_KEY;
          break;
        }

        case GATT_WRITE_AP_SYNC_KEY:
        {
          app_log_info("AP sync key successfully written, writing Tag resp key material\r\n");
          struct sl_bt_ead_key_material_s tag_resp_key_material = { 0 };
          uint8_t tag_resp_key_material_value[KEY_MATERIAL_SIZE] = { 0 };
          sc = initialize_key(&tag_resp_key_material);
          app_assert_status(sc);
          psa_key_id_t key_id = TAG_RESP_KEY_ID_BASE + connected_tag.tag_id;
          sc = sl_bt_ead_store_key(PSA_KEY_USAGE_DECRYPT | PSA_KEY_USAGE_EXPORT,
                                   PSA_KEY_LIFETIME_PERSISTENT,
                                   &tag_resp_key_material,
                                   &key_id);
          app_assert_status(sc);
          connected_tag.response_key_material = tag_resp_key_material;
          memcpy(connected_tag.response_nonce.iv,
                 connected_tag.response_key_material.iv,
                 SL_BT_EAD_IV_SIZE);
          app_log_info("Stored response decrypt key for tag_id=%d slot=%d\r\n",
                       provisioning_tag_id_value,
                       connected_tag.response_slot);
          export_ead_key_value(key_id, tag_resp_key_material, tag_resp_key_material_value);
          sc = sl_bt_gatt_write_characteristic_value(gatt_evt->connection,
                                                     TagResponseKeyMaterialCharHandle,
                                                     sizeof(tag_resp_key_material_value),
                                                     tag_resp_key_material_value);
          app_assert_status(sc);
          mbedtls_platform_zeroize(tag_resp_key_material_value,
                                   sizeof(tag_resp_key_material_value));
          gatt_state = GATT_WRITE_TAG_RESP_KEY;
          break;
        }

        case GATT_WRITE_TAG_RESP_KEY:
        {
          sc = sl_bt_advertiser_past_transfer(connection_handle,
                                              0,
                                              pawr_advertising_set_handle);
          app_assert_status(sc);
          connected_tag.state = TAG_RECORD_ACTIVE;
          connected_tag.last_seen_tick = sl_sleeptimer_get_tick_count64();
          tags[connected_tag.tag_id] = connected_tag;
          gatt_state = GATT_DONE;
          break;
        }

        default:
          break;
      }
      break;
    }

    case sl_bt_evt_sm_confirm_bonding_id: {
      app_log_info("confirm bonding\r\n");
      break;
    }

    case sl_bt_evt_sm_bonded_id: {
      app_log_info("device bonded successfully\r\n");
      break;
    }

    case sl_bt_evt_sm_bonding_failed_id: {
      app_log_info("bonding failed, peer address: %2X:%2X:%2X:%2X:%2X:%2X reason 0x%2X, closing connection\r\n",
                   connected_tag.address.addr[5],
                   connected_tag.address.addr[4],
                   connected_tag.address.addr[3],
                   connected_tag.address.addr[2],
                   connected_tag.address.addr[1],
                   connected_tag.address.addr[0],
                   evt->data.evt_sm_bonding_failed.reason);
      sl_bt_connection_close(evt->data.evt_sm_bonding_failed.connection);

      break;
    }

    case sl_bt_evt_connection_closed_id: {
      app_log_info("connection closed, reason 0x%2X\r\n", evt->data.evt_connection_closed.reason);
      connection_in_progress = false;
      break;
    }

    case sl_bt_evt_pawr_advertiser_subevent_data_request_id: {
      sl_bt_evt_pawr_advertiser_subevent_data_request_t req =
        evt->data.evt_pawr_advertiser_subevent_data_request;

      for (uint8_t i = 0; i < req.subevent_data_count; i++) {
        uint8_t subevent_index = (req.subevent_start + i) % SUBEVENT_NUMBER;
        uint8_t advertisement_len;

        app_log_info("Requesting data  for subevent %02X\r\n", subevent_index);

        sc = sl_bt_ead_randomizer_update(&nonce);
        app_assert_status(sc);

        sc = construct_advertisement_payload(&Ap_sync_key_material,
                                             &nonce,
                                             &advertisement_len);
        app_assert_status(sc);

        sc = sl_bt_pawr_advertiser_set_subevent_data(req.advertising_set,
                                                     subevent_index,
                                                     0,
                                                     RESPONSE_SLOT_NUMBER_PER_SUBVENT,
                                                     advertisement_len,
                                                     advertisement_buffer);
      }
      break;
    }

    case sl_bt_evt_pawr_advertiser_response_report_id: {
      sl_bt_evt_pawr_advertiser_response_report_t *report =
        &evt->data.evt_pawr_advertiser_response_report;

      if (report->data_status != 0) {
        break;
      }

      app_log_info("response for subevent %02X, response slot %02X\r\n",
                   report->subevent,
                   report->response_slot);

      if (report->response_slot >= MAX_TAGS) {
        break;
      }

      tag_record_t *tag = &tags[report->response_slot];

      if (tag->state != TAG_RECORD_ACTIVE
          || tag->response_subevent != report->subevent
          || tag->response_slot != report->response_slot) {
        break;
      }

      sc = decrypt_tag_response(report, tag);
      if (sc == SL_STATUS_OK) {
        tag->last_seen_tick = sl_sleeptimer_get_tick_count64();
      } else {
        app_log_error("Failed to decrypt response from tag_id=%d slot=%d rc=0x%04lX\r\n",
                      tag->tag_id,
                      report->response_slot,
                      sc);
      }
      app_log("\n\r**************************************************************************\n\r");
      break;
    }

    case sl_bt_evt_system_external_signal_id: {
      if (evt->data.evt_system_external_signal.extsignals & TAG_SWEEP_SIGNAL) {
        sweep_stale_tags();
      }
      break;
    }

    case sl_bt_evt_scanner_legacy_advertisement_report_id: {
      break;
    }

    default: {
      // app_log_info("untracked  event id %08lX\r\n",(unsigned long)SL_BT_MSG_ID(evt->header));
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static sl_status_t construct_advertisement_payload(sl_bt_ead_key_material_p key_material,
                                                   sl_bt_ead_nonce_p nonce,
                                                   uint8_t *index)
{
  sl_status_t sc = SL_STATUS_FAIL;
  uint8_t manufacturer_data_buf[BLE_EA_ADV_DATA_LEN];
  size_t manufacturer_data_len; // len + type + data
  sl_bt_ead_mic_t message_integraty_check;
  struct sl_bt_ead_ad_structure_s encrypted_ad_structure;
  uint8_t encrypted_data_length = BLE_EA_ADV_DATA_LEN;

  *index = 0;

  // Refresh the secret part of the advertisement.
  secret_number++;
  snprintf(secret_data, sizeof(secret_data), "secret%02d", secret_number);
  manufacturer_data_len = (size_t)(2 + strlen(secret_data));

  // Add flags.
  advertisement_buffer[(*index)++] = 0x02; // Ad structure len
  advertisement_buffer[(*index)++] = 0x01; // Ad structure type
  advertisement_buffer[(*index)++] = 0x06; // Ad structure data

  // Add complete local name.
  advertisement_buffer[(*index)++] = strlen(name) + 1;       // Ad structure len
  advertisement_buffer[(*index)++] = 0x09;                   // Ad structure type
  memcpy(advertisement_buffer + *index, name, strlen(name)); // Ad structure data
  *index += strlen(name);

  // Add encrypted manufacturer specific data.
  manufacturer_data_buf[0] = strlen(secret_data) + 1;                  // Ad structure len
  manufacturer_data_buf[1] = 0xFF;                                     // Ad structure type
  memcpy(manufacturer_data_buf + 2, secret_data, strlen(secret_data)); // Ad structure data

  sc = sl_bt_ead_encrypt(key_material,
                         nonce,
                         manufacturer_data_len,
                         manufacturer_data_buf,
                         message_integraty_check);
  app_assert_status(sc);

  encrypted_ad_structure.length = manufacturer_data_len;
  encrypted_ad_structure.ad_type = SL_BT_ENCRYPTED_DATA_AD_TYPE;
  encrypted_ad_structure.ad_data = manufacturer_data_buf;
  encrypted_ad_structure.randomizer = &(nonce->randomizer);
  encrypted_ad_structure.mic = &message_integraty_check;

  sc = sl_bt_ead_pack_ad_data(&encrypted_ad_structure,
                              &encrypted_data_length,
                              advertisement_buffer + *index);
  app_assert_status(sc);
  app_log("Information before encryption:%s\n\r", secret_data);
  app_log("Information after encryption:\n\r");

  for (uint8_t i = *index; i < *index + encrypted_data_length; i++) {
    app_log("%02X", advertisement_buffer[i]);
  }

  app_log("\n\r");
  app_log("--------------------------------------------------------------------------\n\r");
  (*index) += encrypted_data_length;

  return sc;
}

static sl_status_t initialize_key(sl_bt_ead_key_material_p key_material)
{
  psa_status_t psa_sc = PSA_SUCCESS;
  sl_bt_ead_session_key_t session_key = { 0 };
  sl_bt_ead_iv_t initialization_vector = { 0 };

  psa_sc = psa_generate_random(session_key, SL_BT_EAD_SESSION_KEY_SIZE);
  if (psa_sc != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }

  app_log("\r\n");

  psa_sc = psa_generate_random(initialization_vector, SL_BT_EAD_IV_SIZE);
  if (psa_sc != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }

  app_log("\r\n");
  memcpy(key_material->key, session_key, SL_BT_EAD_SESSION_KEY_SIZE);
  memcpy(key_material->iv, initialization_vector, SL_BT_EAD_IV_SIZE);
  mbedtls_platform_zeroize(session_key, sizeof(session_key));
  mbedtls_platform_zeroize(initialization_vector,
                           sizeof(initialization_vector));
  return SL_STATUS_OK;
}

static sl_status_t decrypt_tag_response(sl_bt_evt_pawr_advertiser_response_report_t *report,
                                        tag_record_t *tag)
{
  sl_status_t sc;
  uint8_t i = 0;
  struct sl_bt_ead_ad_structure_s response_info;
  sl_bt_ead_randomizer_t randomizer;
  sl_bt_ead_mic_t mic;
  uint8_t decrypted_data_buffer[30];

  if (report == NULL || tag == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  response_info.length = sizeof(decrypted_data_buffer);
  response_info.randomizer = &randomizer;
  response_info.ad_data = decrypted_data_buffer;
  response_info.mic = &mic;

  while (i + 1 < report->data.len) {
    uint8_t ad_len = report->data.data[i];

    if (ad_len == 0) {
      break;
    }

    if ((uint16_t)i + ad_len >= report->data.len) {
      app_log_error("Invalid response AD length: i=%d ad_len=%d report_len=%d\r\n",
                    i,
                    ad_len,
                    report->data.len);
      return SL_STATUS_INVALID_PARAMETER;
    }

    uint8_t ad_type = report->data.data[i + 1];

    if (ad_type == SL_BT_ENCRYPTED_DATA_AD_TYPE) {
      app_log_info("Encrypted response from tag_id=%d slot=%d, response data:\r\n",
                   tag->tag_id,
                   report->response_slot);

      for (uint8_t j = 0; j < ad_len + 1; j++) {
        app_log("%02X", report->data.data[i + j]);
      }
      app_log("\r\n");

      response_info.length = sizeof(decrypted_data_buffer);

      sc = sl_bt_ead_unpack_ad_data(&report->data.data[i],
                                    &response_info);
      if (sc != SL_STATUS_OK) {
        app_log_error("response unpack failed rc=0x%04lX\r\n", sc);
        return sc;
      }

      memcpy(tag->response_nonce.randomizer,
             response_info.randomizer,
             SL_BT_EAD_RANDOMIZER_SIZE);

      sc = sl_bt_ead_decrypt(&tag->response_key_material,
                             &tag->response_nonce,
                             (uint8_t *)response_info.mic,
                             response_info.length,
                             response_info.ad_data);

      if (sc != SL_STATUS_OK) {
        app_log_error("response decrypt failed rc=0x%04lX\r\n", sc);
        return sc;
      }

      app_log_info("Decrypted response from tag_id=%d: ", tag->tag_id);

      for (uint8_t k = 0; k < response_info.length; k++) {
        app_log("%c", response_info.ad_data[k]);
      }
      app_log("\r\n");

      return SL_STATUS_OK;
    }

    i = i + ad_len + 1;
  }

  return SL_STATUS_NOT_FOUND;
}

static sl_status_t find_tag(sl_bt_evt_scanner_extended_advertisement_report_t *report)
{
  uint8_t index = 0;

  while (index < report->data.len) {
    uint8_t ADdatalen = report->data.data[index];
    uint8_t ADdatatype = report->data.data[index + 1];

    if (ADdatatype == 0x06 || ADdatatype == 0x07) {
      if (memcmp(&report->data.data[index + 2],
                 SecureTagServiceUUID,
                 sizeof(SecureTagServiceUUID)) == 0) {
        return SL_STATUS_OK;
      }
    }

    index = index + ADdatalen + 1;
  }

  return SL_STATUS_FAIL;
}

static bool address_compare(const tag_record_t *tag,
                            bd_addr address,
                            uint8_t address_type)
{
  return tag->state != TAG_RECORD_FREE
         && tag->address_type == address_type
         && memcmp(tag->address.addr, address.addr, sizeof(address.addr)) == 0;
}

static sl_status_t find_record_by_address(bd_addr address,
                                          uint8_t address_type,
                                          uint8_t *tag_id)
{
  for (uint8_t i = 0; i < MAX_TAGS; i++) {
    if (address_compare(&tags[i], address, address_type)) {
      *tag_id = i;
      return SL_STATUS_OK;
    }
  }

  return SL_STATUS_NOT_FOUND;
}

static sl_status_t allocate_record(uint8_t *tag_id)
{
  for (uint8_t i = 0; i < MAX_TAGS; i++) {
    if (tags[i].state == TAG_RECORD_FREE || tags[i].state == TAG_RECORD_STALE) {
      *tag_id = i;
      return SL_STATUS_OK;
    }
  }

  return SL_STATUS_FULL;
}

static sl_status_t resolve_tag_id(uint8_t claimed_id,
                                  bd_addr address,
                                  uint8_t address_type,
                                  uint8_t *resolved_id)
{
  if (claimed_id < MAX_TAGS && address_compare(&tags[claimed_id], address, address_type)) {
    *resolved_id = claimed_id;
    return SL_STATUS_OK;
  }

  if (find_record_by_address(address, address_type, resolved_id) == SL_STATUS_OK) {
    return SL_STATUS_OK;
  }

  return allocate_record(resolved_id);
}

static void sweep_stale_tags(void)
{
  uint64_t now = sl_sleeptimer_get_tick_count64();

  for (uint8_t i = 0; i < MAX_TAGS; i++) {
    tag_record_t *tag = &tags[i];

    if (tag->state == TAG_RECORD_ACTIVE
        && (now - tag->last_seen_tick) > tag_stale_timeout_ticks) {
      if (tag->response_key_material.key_id != PSA_KEY_ID_NULL) {
        sl_bt_ead_delete_key(&tag->response_key_material);
      }
      mbedtls_platform_zeroize(&tag->response_nonce,
                               sizeof(tag->response_nonce));
      tag->state = TAG_RECORD_STALE;
      tag->bonding = SL_BT_INVALID_BONDING_HANDLE;
      app_log_info("tag_id=%d marked stale\r\n", tag->tag_id);
    }
  }
}

static void stale_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  sl_bt_external_signal(TAG_SWEEP_SIGNAL);
}

static void export_ead_key_value(psa_key_id_t key_id, struct sl_bt_ead_key_material_s key_material, uint8_t key_material_value[KEY_MATERIAL_SIZE])
{
  uint8_t session_key_value[SL_BT_EAD_SESSION_KEY_SIZE] = { 0 };
  size_t exported_length = 0;

  psa_status_t psa_sc = psa_export_key(
    key_id,
    session_key_value,
    sizeof(session_key_value),
    &exported_length);
  app_assert_status(psa_sc);

  for (size_t i = 0; i < exported_length / 2; i++) {
    uint8_t tmp = session_key_value[i];
    session_key_value[i] =
      session_key_value[exported_length - 1 - i];
    session_key_value[exported_length - 1 - i] = tmp;
  }

  memcpy(key_material_value, session_key_value, SL_BT_EAD_SESSION_KEY_SIZE);
  memcpy(key_material_value + SL_BT_EAD_SESSION_KEY_SIZE, key_material.iv, SL_BT_EAD_IV_SIZE);
  mbedtls_platform_zeroize(session_key_value,
                           sizeof(session_key_value));
}
