/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 TinyUSB contributors
 * SPDX-License-Identifier: MIT
 */

// SET_LINE_CODING through tud_cdc_line_coding_request_cb: a coding the application refuses stalls the
// request and leaves the coding GET_LINE_CODING reports; one it accepts takes effect as before.

#include <string.h>
#include "unity.h"

#include "tusb.h"
#include "usbd.h"
TEST_SOURCE_FILE("cdc_device.c")
TEST_SOURCE_FILE("tusb_fifo.c")

#include "mock_dcd.h"

enum {
  RHPORT  = 0,
  ITF_CDC = 0,
  EPSIZE  = TUD_OPT_HIGH_SPEED ? 512 : 64,
};

static const uint8_t desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(ITF_CDC, 0, 0x81, 8, 0x02, 0x82, EPSIZE),
};

uint32_t tusb_time_millis_api(void) {
  return 0;
}

const uint8_t *tud_descriptor_device_cb(void) {
  return NULL;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)index;
  (void)langid;
  return NULL;
}

//--------------------------------------------------------------------+
// The application: refuses any rate above `fastest`, and records what took effect
//--------------------------------------------------------------------+
static uint32_t          fastest;
static uint32_t          asked;
static uint32_t          applied;
static cdc_line_coding_t applied_coding;

bool tud_cdc_line_coding_request_cb(uint8_t itf, const cdc_line_coding_t *coding) {
  TEST_ASSERT_EQUAL(ITF_CDC, itf);
  asked++;
  return coding->bit_rate <= fastest;
}

void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *coding) {
  TEST_ASSERT_EQUAL(ITF_CDC, itf);
  applied++;
  applied_coding = *coding;
}

//--------------------------------------------------------------------+
// EP0: the host's side of a control transfer
//--------------------------------------------------------------------+
static bool           ep0_pending;
static bool           ep0_stalled;
static uint8_t        ep0_addr;
static uint8_t       *ep0_buffer;
static uint16_t       ep0_len;
static const uint8_t *host_out;  // what the host sends in an OUT data stage
static uint8_t        host_in[64];

static bool stub_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes, bool is_isr,
                           int n) {
  (void)rhport;
  (void)is_isr;
  (void)n;
  if (tu_edpt_number(ep_addr) == 0) {
    ep0_pending = true;
    ep0_addr    = ep_addr;
    ep0_buffer  = buffer;
    ep0_len     = total_bytes;
  }
  return true;
}

static void stub_edpt_stall(uint8_t rhport, uint8_t ep_addr, int n) {
  (void)rhport;
  (void)n;
  if (tu_edpt_number(ep_addr) == 0) {
    ep0_stalled = true;
  }
}

// Drive a control request through its data and status stages; false if the device stalled it
static bool host_control(const tusb_control_request_t *req, const uint8_t *out) {
  host_out    = out;
  ep0_pending = false;
  ep0_stalled = false;
  dcd_event_setup_received(RHPORT, (const uint8_t *)req, false);
  tud_task();
  while (ep0_pending && !ep0_stalled) {
    ep0_pending = false;
    if (ep0_len != 0 && ep0_addr == 0x00) {
      memcpy(ep0_buffer, host_out, ep0_len);
    } else if (ep0_len != 0) {
      memcpy(host_in, ep0_buffer, ep0_len);
    }
    dcd_event_xfer_complete(RHPORT, ep0_addr, ep0_len, XFER_RESULT_SUCCESS, false);
    tud_task();
  }
  return !ep0_stalled;
}

static bool set_line_coding(uint32_t baud, uint8_t stop, uint8_t parity, uint8_t bits) {
  const uint8_t coding[7] = {U32_TO_U8S_LE(baud), stop, parity, bits};
  const tusb_control_request_t req = {.bmRequestType = 0x21,
                                      .bRequest      = CDC_REQUEST_SET_LINE_CODING,
                                      .wIndex        = ITF_CDC,
                                      .wLength       = sizeof(coding)};
  return host_control(&req, coding);
}

static uint32_t get_line_coding_rate(void) {
  const tusb_control_request_t req = {.bmRequestType = 0xA1,
                                      .bRequest      = CDC_REQUEST_GET_LINE_CODING,
                                      .wIndex        = ITF_CDC,
                                      .wLength       = 7};
  memset(host_in, 0, sizeof(host_in));
  TEST_ASSERT_TRUE(host_control(&req, NULL));
  return tu_u32(host_in[3], host_in[2], host_in[1], host_in[0]);
}

void setUp(void) {
  dcd_int_disable_Ignore();
  dcd_int_enable_Ignore();
  dcd_edpt_close_all_Ignore();
  dcd_edpt0_status_complete_Ignore();
  dcd_edpt0_setup_begin_IgnoreAndReturn(true);
  dcd_edpt_open_IgnoreAndReturn(true);
  dcd_edpt_xfer_fifo_IgnoreAndReturn(true);
  dcd_edpt_xfer_Stub(stub_edpt_xfer);
  dcd_edpt_stall_Stub(stub_edpt_stall);

  if (!tud_inited()) {
    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    dcd_init_ExpectAndReturn(RHPORT, &dev_init, true);
    tusb_init(RHPORT, &dev_init);
  }

  dcd_event_bus_reset(RHPORT, TUSB_SPEED_HIGH, false);
  tud_task();
  const tusb_control_request_t set_config = {.bmRequestType = 0x00,
                                             .bRequest      = TUSB_REQ_SET_CONFIGURATION,
                                             .wValue        = 1};
  TEST_ASSERT_TRUE(host_control(&set_config, NULL));
  TEST_ASSERT_TRUE(tud_mounted());
  fastest = 3000000;
  asked   = 0;
  applied = 0;
}

void tearDown(void) {
}

void test_accepted_coding_takes_effect(void) {
  TEST_ASSERT_TRUE(set_line_coding(921600, 2, 1, 7));
  TEST_ASSERT_EQUAL(1, asked);
  TEST_ASSERT_EQUAL(1, applied);
  TEST_ASSERT_EQUAL_UINT32(921600, applied_coding.bit_rate);
  TEST_ASSERT_EQUAL(2, applied_coding.stop_bits);
  TEST_ASSERT_EQUAL(1, applied_coding.parity);
  TEST_ASSERT_EQUAL(7, applied_coding.data_bits);
  TEST_ASSERT_EQUAL_UINT32(921600, get_line_coding_rate());
}

void test_refused_coding_stalls_and_keeps_the_last(void) {
  TEST_ASSERT_TRUE(set_line_coding(115200, 0, 0, 8));
  TEST_ASSERT_FALSE(set_line_coding(10000000, 0, 0, 8));
  TEST_ASSERT_EQUAL(2, asked);
  TEST_ASSERT_EQUAL(1, applied);
  TEST_ASSERT_EQUAL_UINT32(115200, applied_coding.bit_rate);

  cdc_line_coding_t coding;
  tud_cdc_n_get_line_coding(ITF_CDC, &coding);
  TEST_ASSERT_EQUAL_UINT32(115200, coding.bit_rate);
  TEST_ASSERT_EQUAL_UINT32(115200, get_line_coding_rate());

  // The next coding it can make still takes effect.
  TEST_ASSERT_TRUE(set_line_coding(2000000, 0, 0, 8));
  TEST_ASSERT_EQUAL(2, applied);
  TEST_ASSERT_EQUAL_UINT32(2000000, get_line_coding_rate());
}
