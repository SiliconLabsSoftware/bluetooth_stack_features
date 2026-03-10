/***************************************************************************//**
 * @file rsp_queue.c
 * @brief
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

#include "sl_bluetooth.h"
#include "rsp_queue.h"
/* Maximum number of advertisements or scan responses stored in queue */
#define MAX_RSP_NUM                         (8)

static uint32_t counter = 0;

int __match(
  const sl_bt_evt_scanner_extended_advertisement_report_t *rsp,
  rsp_t *r)
{
  return ((rsp->address_type == r->data.address_type)
          && (!memcmp(rsp->address.addr, r->data.address.addr, 6))
          && (rsp->data.len == r->data.data.len)
          && (!memcmp(rsp->data.data, r->data.data.data, rsp->data.len)));
}

void __copy(const sl_bt_evt_scanner_extended_advertisement_report_t *rsp,
            rsp_t *r)
{
  memcpy(&r->data, rsp,
         sizeof(sl_bt_evt_scanner_extended_advertisement_report_t) + rsp->data.len);
}

void update_counter(rsp_t *r)
{
  r->counter = counter;
}

rsp_t *find_rsp(rsp_queue_t *rsp_queue, const sl_bt_evt_scanner_extended_advertisement_report_t *rsp)
{
  rsp_t *r;

  if (!rsp_queue->head || !rsp) {
    return NULL;
  }

  r = rsp_queue->head;
  do {
    if (__match(rsp, r)) {
      return r;
    }
    r = r->next;
  } while (r && r != rsp_queue->head);
  return NULL;
}

void head_item(rsp_queue_t *rsp_queue, rsp_t *r)
{
  rsp_t *tmp;
  if (!r || r == rsp_queue->head) {
    return;
  }
  if (!rsp_queue->head) {
    r->next = r;
    r->prev = r;
    rsp_queue->head = r;
    return;
  }
  if (r->prev) {
    r->prev->next = r->next;
  }
  if (r->next) {
    r->next->prev = r->prev;
  }

  tmp = rsp_queue->head->prev;
  r->next = rsp_queue->head;
  tmp->next = r;
  rsp_queue->head->prev = r;
  r->prev = tmp;
  rsp_queue->head = r;
}

void remove_item(rsp_queue_t *rsp_queue, rsp_t *r)
{
  if (!r || !rsp_queue->head) {
    return;
  }

  if (rsp_queue->num != 1) {
    if (r == rsp_queue->head) {
      rsp_queue->head = rsp_queue->head->next;
    }
    r->prev->next = r->next;
    r->next->prev = r->prev;
    free(r);
    rsp_queue->num--;
  } else {
    free(rsp_queue->head);
    rsp_queue->head = NULL;
    rsp_queue->num = 0;
  }
}

int insert_rsp(rsp_queue_t *rsp_queue, const sl_bt_evt_scanner_extended_advertisement_report_t *rsp)
{
  rsp_t *r;
  if (rsp_queue->num == MAX_RSP_NUM) {
    r = rsp_queue->head->prev; /* the last one */
    if (r->data.data.len < rsp->data.len) {
      r->prev->next = r->next;
      r->next->prev = r->prev;
      free(r);
      r = calloc(sizeof(rsp_t) + rsp->data.len, 1);
    }
  } else {
    r = calloc(sizeof(rsp_t) + rsp->data.len, 1);
  }
  if (!r) {
    return -1;
  }
  head_item(rsp_queue, r);
  __copy(rsp, r);
  update_counter(r);
  if (rsp_queue->num != MAX_RSP_NUM) {
    rsp_queue->num++;
  }
  return 0;
}
