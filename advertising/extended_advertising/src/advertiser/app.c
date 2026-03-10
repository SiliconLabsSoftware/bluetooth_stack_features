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

void demo_setup_start_ext_adv(uint8_t handle);

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;

const uint8_t dummy_service[16] = { 0x81, 0xc2, 0x00, 0x2d, 0x31, 0xf4, 0xb0, 0xbf, 0x2b, 0x42, 0x49, 0x68, 0xc7, 0x25, 0x71, 0x41 };

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

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      // Print boot message.
      app_log("Bluetooth stack booted: v%d.%d.%d-b%d\n",
              evt->data.evt_system_boot.major,
              evt->data.evt_system_boot.minor,
              evt->data.evt_system_boot.patch,
              evt->data.evt_system_boot.build);
      // Extract unique ID from BT Address.
      sc = sl_bt_system_get_identity_address(&address, &address_type);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to get Bluetooth address\n",
                 (int)sc);
      app_log("Bluetooth %s address: %02X:%02X:%02X:%02X:%02X:%02X\n",
              address_type ? "static random" : "public device",
              address.addr[5],
              address.addr[4],
              address.addr[3],
              address.addr[2],
              address.addr[1],
              address.addr[0]);
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
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to write attribute\n",
                 (int)sc);

      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to create advertising set\n",
                 (int)sc);
      demo_setup_start_ext_adv(advertising_set_handle);
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      // Restart advertising after client has disconnected.
      sc = sl_bt_extended_advertiser_generate_data(advertising_set_handle,
                                                   sl_bt_advertiser_general_discoverable);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to generate data\n",
                 (int)sc);
      sc = sl_bt_extended_advertiser_start(
        advertising_set_handle,
        sl_bt_extended_advertiser_scannable,
        0);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to start advertising\n",
                 (int)sc);
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

#define TEST_EXT_ELE_LENGTH 200
void demo_setup_ext_adv(uint8_t handle)
{
  sl_status_t sc;
  uint8_t amout_bytes = 0;
  uint8_t extended_buf[TEST_EXT_ELE_LENGTH];
  /* https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers - To get your company ID*/
  uint16_t company_id = 0x02FF; // 0x02FF - Silicon Labs' company ID

  // Initialize advertising data with Flag and Local name
  uint8_t adv_data[MAX_EXTENDED_ADV_LENGTH] = {
    0x02,   // Length of flag
    0x01,   // Type flag
    0x06,   // Flag data
    0x05,   // Length of Local name
    0x09,   // Type local name
    'A', 'd', 'v', 'C',   // Local name
  };
  // Byte amount in advertising data buffer now increased by 9
  amout_bytes += 9;
  adv_data[amout_bytes++] = 0x11;// length 17 bytes
  adv_data[amout_bytes++] = 0x06;//more_128_uuids

  // copy the 128bits UUID of the service into the advertising data
  memcpy(adv_data + amout_bytes, dummy_service, 16);
  amout_bytes += 16;

  // Prepare manufacturer_specific_data
  memcpy(extended_buf, (uint8_t *)&company_id, 2);
  for (uint8_t i = 2; i < TEST_EXT_ELE_LENGTH; i++) {
    extended_buf[i] = i;
  }

  // Adding manufacturer_specific_data
  adv_data[amout_bytes++] = 0xC9;//length TEST_EXT_ELE_LENGTH + 1
  adv_data[amout_bytes++] = 0xFF;//ad type: manufacturer_specific_data
  memcpy(adv_data + amout_bytes, (uint8_t *)&extended_buf, TEST_EXT_ELE_LENGTH);
  amout_bytes += TEST_EXT_ELE_LENGTH;

  // Set advertising data
  sc = sl_bt_extended_advertiser_set_data(handle, amout_bytes, adv_data);
  app_assert(sc == SL_STATUS_OK,
             "[E: 0x%04x] Failed to set advertising data\n",
             (int)sc);
}

void demo_setup_start_ext_adv(uint8_t handle)
{
  sl_status_t sc;
  // Set advertising interval to 100ms.
  sc = sl_bt_advertiser_set_timing(
    handle,
    160, // min. adv. interval (milliseconds * 1.6)
    160, // max. adv. interval (milliseconds * 1.6)
    0,   // adv. duration
    0);  // max. num. adv. events
  app_assert(sc == SL_STATUS_OK,
             "[E: 0x%04x] Failed to set advertising timing\n",
             (int)sc);

  demo_setup_ext_adv(handle);

  sc = sl_bt_extended_advertiser_start(
    handle,
    sl_bt_extended_advertiser_non_connectable,
    0);
  app_assert(sc == SL_STATUS_OK,
             "[E: 0x%04x] Failed to start advertising\n",
             (int)sc);
  /* Start general advertising and enable connections. */
  app_log("Start advertising.\r\n");
}
