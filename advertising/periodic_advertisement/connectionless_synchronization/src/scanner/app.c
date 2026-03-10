/***************************************************************************//**
 * @file scanner
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
#include "app.h"
#include "sl_bt_api.h"
#include "app_log.h"

// UUID of periodic synchronous service used to identify the advertisement of the target device
const uint8_t periodicSyncService[16] = { 0x81, 0xc2, 0x00, 0x2d, 0x31, 0xf4, 0xb0, 0xbf, 0x2b, 0x42, 0x49, 0x68, 0xc7, 0x25, 0x71, 0x41 };

// Parse advertisements looking for advertised periodicSync Service.
static uint8_t findServiceInAdvertisement(uint8_t *data, uint8_t len)
{
  uint8_t adFieldLength;
  uint8_t adFieldType;
  uint8_t i = 0;
  // Parse advertisement packet
  while (i < len) {
    adFieldLength = data[i];
    adFieldType = data[i + 1];
    // found incomplete/complete list of 128-bit Service UUIDs as defined in the assgigned numbers SIG document.
    if (adFieldType == 0x06 || adFieldType == 0x07) {
      // compare  UUID to periodic synchronous service UUID
      if (memcmp(&data[i + 2], periodicSyncService, 16) == 0) {
        return 1;
      }
    }
    // advance to the next AD struct
    i = i + adFieldLength + 1;
  }
  return 0;
}

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
  static uint16_t sync;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id: {
      bd_addr address;
      uint8_t address_type;
      // Extract unique ID from BT Address.
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

      // Scan in intervals of 125ms for 125ms time window
      sc = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_passive, 200, 200);
      app_assert_status(sc);

      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m,
                               sl_bt_scanner_discover_observation);
      app_assert_status(sc);
      break;
    }

    // -------------------------------
    // This event indicates the device has received extended advertisements PDU
    case sl_bt_evt_scanner_extended_advertisement_report_id:
      app_log_info("got extended advertisement indication with tx_power = %d\r\n",
                   evt->data.evt_scanner_extended_advertisement_report.tx_power);
      if (findServiceInAdvertisement(&(evt->data.evt_scanner_extended_advertisement_report.data.data[0]), evt->data.evt_scanner_extended_advertisement_report.data.len) != 0) {
        app_log_info("found periodic sync service, attempting to open sync\r\n");
        sc = sl_bt_sync_scanner_open(evt->data.evt_scanner_extended_advertisement_report.address,
                                     evt->data.evt_scanner_extended_advertisement_report.address_type,
                                     evt->data.evt_scanner_extended_advertisement_report.adv_sid,
                                     &sync);
        app_assert_status(sc);
        /* now that sync is open, we can stop scanning*/
        sl_bt_scanner_stop();
      }
      break;

    // -------------------------------
    // This event indicates the device has synchronized successfully to the perodic advertisement
    case sl_bt_evt_periodic_sync_opened_id:
    {
      bd_addr advA = evt->data.evt_periodic_sync_opened.address;
      app_log_info("Successfully synchronized to bluetooth address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   advA.addr[5],
                   advA.addr[4],
                   advA.addr[3],
                   advA.addr[2],
                   advA.addr[1],
                   advA.addr[0]);

      break;
    }

    // -------------------------------
    // This event indicates the synchornizatiion has been terminated
    case sl_bt_evt_sync_closed_id:
      app_log_info("periodic sync closed. reason 0x%04X, sync handle 0x%04X\r\n",
                   evt->data.evt_sync_closed.reason,
                   evt->data.evt_sync_closed.sync);
      /* restart discovery */
      sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m,
                          sl_bt_scanner_discover_observation);
      break;

    // -------------------------------
    // This event indicates the device has received a periodic advertisements PDU
    case sl_bt_evt_periodic_sync_report_id:
      app_log_info("Periodic advertisemenet payload:  ");
      for (int i = 0; i < evt->data.evt_periodic_sync_report.data.len; i++) {
        app_log("%02X,", evt->data.evt_periodic_sync_report.data.data[i]);
      }
      app_log("\r\n");
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
