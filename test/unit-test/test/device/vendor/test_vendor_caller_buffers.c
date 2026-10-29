/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 CordlessCoder
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#include "unity.h"

// Files to test
#include "osal/osal.h"
#include "tusb_fifo.h"
#include "tusb.h"
#include "usbd.h"
TEST_SOURCE_FILE("usbd_control.c")
TEST_SOURCE_FILE("vendor_device.c")

// Mock File
#include "mock_dcd.h"

// CFG_TUD_VENDOR_CALLER_BUFFERS, set for this test in project.yml: every bulk transfer goes to or
// from memory the application passed, and the calls that would need a class buffer refuse.

uint32_t tusb_time_millis_api(void) {
  return 0;
}

enum {
  EDPT_VENDOR_OUT = 0x01,
  EDPT_VENDOR_IN  = 0x81,
};

static uint8_t const rhport = 0;

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

static uint8_t const desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100),
  TUD_VENDOR_DESCRIPTOR(0, 0, EDPT_VENDOR_OUT, EDPT_VENDOR_IN, 512),
};

static tusb_control_request_t const request_set_configuration = {
  .bmRequestType = 0x00,
  .bRequest      = TUSB_REQ_SET_CONFIGURATION,
  .wValue        = 1,
  .wIndex        = 0,
  .wLength       = 0
};

uint8_t const *tud_descriptor_device_cb(void) {
  return NULL;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) index;
  (void) langid;
  return NULL;
}

typedef struct {
  uint8_t ep_addr;
  uint8_t *buffer;
  uint16_t total_bytes;
} xfer_call_t;

static xfer_call_t xfers[8];
static uint32_t xfer_count;

static bool record_xfer(uint8_t rh, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes, bool is_isr, int num_calls) {
  (void) rh;
  (void) is_isr;
  (void) num_calls;
  TEST_ASSERT_LESS_THAN(TU_ARRAY_SIZE(xfers), xfer_count);
  xfers[xfer_count++] = (xfer_call_t) {ep_addr, buffer, total_bytes};
  return true;
}

static uint32_t data_xfers(void) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < xfer_count; i++) {
    if (tu_edpt_number(xfers[i].ep_addr) != 0) {
      count++;
    }
  }
  return count;
}

static uint8_t const *rx_buffer;
static uint32_t rx_length;
static uint32_t rx_calls;

void tud_vendor_rx_cb(uint8_t idx, uint8_t const *buffer, uint32_t bufsize) {
  TEST_ASSERT_EQUAL(0, idx);
  rx_buffer = buffer;
  rx_length = bufsize;
  rx_calls++;
}

void setUp(void) {
  dcd_int_disable_Ignore();
  dcd_int_enable_Ignore();
  dcd_edpt0_setup_begin_IgnoreAndReturn(true);
  dcd_edpt_open_IgnoreAndReturn(true);
  dcd_edpt_clear_stall_Ignore();
  dcd_edpt_xfer_Stub(record_xfer);

  if (!tud_inited()) {
    tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO
    };
    dcd_init_ExpectAndReturn(0, &dev_init, true);
    tusb_init(0, &dev_init);
  }

  dcd_event_bus_reset(rhport, TUSB_SPEED_HIGH, false);
  tud_task();

  dcd_event_setup_received(rhport, (uint8_t const *) &request_set_configuration, false);
  tud_task();
  TEST_ASSERT_TRUE(tud_vendor_n_mounted(0));

  xfer_count = 0;
  rx_buffer = NULL;
  rx_length = 0;
  rx_calls = 0;
}

void tearDown(void) {
}

// Manual RX: nothing is armed until the application passes a buffer.
void test_open_arms_nothing(void) {
  TEST_ASSERT_EQUAL(0, data_xfers());
}

void test_read_lands_in_caller_buffer(void) {
  static uint8_t buffer[512];
  TEST_ASSERT_TRUE(tud_vendor_n_read_xfer_buf(0, buffer));
  TEST_ASSERT_EQUAL(1, xfer_count);
  TEST_ASSERT_EQUAL_HEX8(EDPT_VENDOR_OUT, xfers[0].ep_addr);
  TEST_ASSERT_EQUAL_PTR(buffer, xfers[0].buffer);
  TEST_ASSERT_EQUAL(512, xfers[0].total_bytes);

  dcd_event_xfer_complete(rhport, EDPT_VENDOR_OUT, 7, XFER_RESULT_SUCCESS, false);
  tud_task();
  TEST_ASSERT_EQUAL(1, rx_calls);
  TEST_ASSERT_EQUAL_PTR(buffer, rx_buffer);
  TEST_ASSERT_EQUAL(7, rx_length);
  // Manual RX: the class does not re-arm on its own.
  TEST_ASSERT_EQUAL(1, xfer_count);
}

void test_write_sends_caller_buffer(void) {
  static uint8_t buffer[512];
  TEST_ASSERT_EQUAL(9, tud_vendor_n_write_xfer(0, buffer, 9));
  TEST_ASSERT_EQUAL(1, xfer_count);
  TEST_ASSERT_EQUAL_HEX8(EDPT_VENDOR_IN, xfers[0].ep_addr);
  TEST_ASSERT_EQUAL_PTR(buffer, xfers[0].buffer);
  TEST_ASSERT_EQUAL(9, xfers[0].total_bytes);
}

// With no class buffer to copy into or receive into, these two refuse without arming anything.
void test_class_buffer_calls_refuse(void) {
  uint8_t const data[4] = {1, 2, 3, 4};
  TEST_ASSERT_FALSE(tud_vendor_n_read_xfer(0));
  TEST_ASSERT_EQUAL(0, tud_vendor_n_write(0, data, sizeof(data)));
  TEST_ASSERT_EQUAL(0, xfer_count);
}
