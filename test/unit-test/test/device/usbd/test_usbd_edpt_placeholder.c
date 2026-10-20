/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 TinyUSB contributors
 * SPDX-License-Identifier: MIT
 */

// Placeholder endpoints (CFG_TUD_EDPT_PLACEHOLDER), built with CFG_TUD_ENDPPOINT_MAX 6 so that EP6 and EP7 are
// numbers usbd has no state for. The DCD is mocked strictly: any call to it a test does not expect fails the test.

#include "unity.h"

#include "tusb.h"
#include "usbd.h"
#include "device/usbd_pvt.h"
TEST_SOURCE_FILE("cdc_device.c")
TEST_SOURCE_FILE("tusb_fifo.c")

#include "mock_dcd.h"

enum {
  EDPT_CTRL_OUT = 0x00,
  EDPT_CTRL_IN  = 0x80,
  EDPT_DATA_OUT = 0x02,
  EDPT_DATA_IN  = 0x82,
  EDPT_VENDOR   = 0x87,
  EPSIZE = TUD_OPT_HIGH_SPEED ? 512 : 64,
};

static const uint8_t rhport = 0;

// clang-format off
// CDC with its notification at EP6 IN, a number past CFG_TUD_ENDPPOINT_MAX
static const uint8_t cfg_cdc_notif_86[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(0, 0, 0x86, 8, EDPT_DATA_OUT, EDPT_DATA_IN, EPSIZE)
};

// The same with its notification at EP1 IN, a number the controller has
static const uint8_t cfg_cdc_notif_81[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(0, 0, 0x81, 8, EDPT_DATA_OUT, EDPT_DATA_IN, EPSIZE)
};

// One vendor interface whose endpoint at EP7 IN is bound but never opened by its driver, so only the binding
// sees it
static const uint8_t cfg_vendor_87[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + 9 + 7, 0x00, 100),
  9, TUSB_DESC_INTERFACE, 0, 0, 1, TUSB_CLASS_VENDOR_SPECIFIC, 0, 0, 0,
  7, TUSB_DESC_ENDPOINT, EDPT_VENDOR, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(8), 1
};
// clang-format on

static const uint8_t *desc_configuration;

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

static uint16_t vendor_open(uint8_t rhport_, const tusb_desc_interface_t *itf, uint16_t max_len) {
  (void)rhport_;
  TU_VERIFY(itf->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC, 0);
  const uint16_t len = 9 + 7;
  TU_VERIFY(max_len >= len, 0);
  return len;
}

static void vendor_init(void) {
}

static void vendor_reset(uint8_t rhport_) {
  (void)rhport_;
}

static bool vendor_control_xfer_cb(uint8_t rhport_, uint8_t stage, const tusb_control_request_t *request) {
  (void)rhport_;
  (void)stage;
  (void)request;
  return false;
}

static const usbd_class_driver_t vendor_driver = {
  .name            = "VENDOR_TEST",
  .init            = vendor_init,
  .deinit          = NULL,
  .reset           = vendor_reset,
  .open            = vendor_open,
  .control_xfer_cb = vendor_control_xfer_cb,
  .xfer_cb         = NULL,
  .xfer_isr        = NULL,
  .sof             = NULL,
};

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &vendor_driver;
}

static const tusb_desc_endpoint_t *edpt_desc(uint8_t ep_addr) {
  const uint8_t *p_desc   = desc_configuration;
  const uint8_t *desc_end = p_desc + tu_le16toh(((const tusb_desc_configuration_t *)p_desc)->wTotalLength);
  while (p_desc < desc_end) {
    if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT &&
        ((const tusb_desc_endpoint_t *)p_desc)->bEndpointAddress == ep_addr) {
      return (const tusb_desc_endpoint_t *)p_desc;
    }
    p_desc = tu_desc_next(p_desc);
  }
  TEST_FAIL_MESSAGE("endpoint not in the test configuration");
  return NULL;
}

static bool register_placeholders(uint16_t bm_in, uint16_t bm_out) {
  const tud_configure_edpt_placeholder_t cfg = {.bm_in = bm_in, .bm_out = bm_out};
  return tud_configure(rhport, TUD_CFGID_EDPT_PLACEHOLDER, &cfg);
}

static void setup_request(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
  const tusb_control_request_t req = {.bmRequestType = type,
                                      .bRequest      = request,
                                      .wValue        = value,
                                      .wIndex        = index,
                                      .wLength       = length};
  dcd_event_setup_received(rhport, (const uint8_t *)&req, false);
}

static void expect_stall_ep0(void) {
  dcd_edpt_stall_Expect(rhport, EDPT_CTRL_OUT);
  dcd_edpt_stall_Expect(rhport, EDPT_CTRL_IN);
}

// The CDC data endpoints open, OUT is armed, and SET_CONFIGURATION's status stage is sent
static void expect_cdc_data_open(void) {
  dcd_edpt_open_ExpectAndReturn(rhport, edpt_desc(EDPT_DATA_OUT), true);
  dcd_edpt_xfer_fifo_ExpectAndReturn(rhport, EDPT_DATA_OUT, NULL, EPSIZE, false, true);
  dcd_edpt_xfer_fifo_IgnoreArg_ff();
  dcd_edpt_open_ExpectAndReturn(rhport, edpt_desc(EDPT_DATA_IN), true);
  dcd_edpt_xfer_ExpectAndReturn(rhport, EDPT_CTRL_IN, NULL, 0, false, true);
}

void setUp(void) {
  dcd_int_disable_Ignore();
  dcd_int_enable_Ignore();
  dcd_edpt0_setup_begin_IgnoreAndReturn(true);

  if (!tud_inited()) {
    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    dcd_init_ExpectAndReturn(0, &dev_init, true);
    tusb_init(0, &dev_init);
  }

  // unconfigured, so the placeholder set may change
  dcd_event_bus_reset(rhport, TUSB_SPEED_HIGH, false);
  tud_task();
  TEST_ASSERT_TRUE(register_placeholders(0, 0));
}

void tearDown(void) {
}

//--------------------------------------------------------------------+
// Enumeration
//--------------------------------------------------------------------+

void test_cdc_notif_placeholder_past_max_enumerates(void) {
  TEST_ASSERT_TRUE(register_placeholders(TU_BIT(6), 0));
  desc_configuration = cfg_cdc_notif_86;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  expect_cdc_data_open();
  tud_task();

  TEST_ASSERT_TRUE(tud_mounted());
  TEST_ASSERT_TRUE(tud_cdc_n_ready(0));
}

void test_cdc_notif_past_max_unregistered_fails(void) {
  desc_configuration = cfg_cdc_notif_86;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  expect_stall_ep0();
  tud_task();

  TEST_ASSERT_FALSE(tud_mounted());
}

void test_cdc_notif_in_range_unregistered_is_opened(void) {
  desc_configuration = cfg_cdc_notif_81;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  dcd_edpt_open_ExpectAndReturn(rhport, edpt_desc(0x81), true);
  expect_cdc_data_open();
  tud_task();

  TEST_ASSERT_TRUE(tud_mounted());
}

void test_cdc_notif_in_range_placeholder_is_not_opened(void) {
  TEST_ASSERT_TRUE(register_placeholders(TU_BIT(1), 0));
  desc_configuration = cfg_cdc_notif_81;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  expect_cdc_data_open();
  tud_task();

  TEST_ASSERT_TRUE(tud_mounted());
}

// #3828's check in the binding: an endpoint past CFG_TUD_ENDPPOINT_MAX that no driver opens
void test_bind_past_max_unregistered_fails(void) {
  desc_configuration = cfg_vendor_87;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  expect_stall_ep0();
  tud_task();

  TEST_ASSERT_FALSE(tud_mounted());
}

void test_bind_past_max_placeholder_configures(void) {
  TEST_ASSERT_TRUE(register_placeholders(TU_BIT(7), 0));
  desc_configuration = cfg_vendor_87;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  dcd_edpt_xfer_ExpectAndReturn(rhport, EDPT_CTRL_IN, NULL, 0, false, true);
  tud_task();

  TEST_ASSERT_TRUE(tud_mounted());
}

// A placeholder's direction counts: OUT EP6 registered does not cover IN EP6
void test_placeholder_direction_is_separate(void) {
  TEST_ASSERT_TRUE(register_placeholders(0, TU_BIT(6)));
  desc_configuration = cfg_cdc_notif_86;

  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  expect_stall_ep0();
  tud_task();

  TEST_ASSERT_FALSE(tud_mounted());
}

//--------------------------------------------------------------------+
// Host requests addressed to an endpoint
//--------------------------------------------------------------------+

static void configure_cdc(uint16_t placeholder_in, const uint8_t *cfg, bool open_notif) {
  TEST_ASSERT_TRUE(register_placeholders(placeholder_in, 0));
  desc_configuration = cfg;
  setup_request(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
  if (open_notif) {
    dcd_edpt_open_ExpectAndReturn(rhport, edpt_desc(0x81), true);
  }
  expect_cdc_data_open();
  tud_task();
  TEST_ASSERT_TRUE(tud_mounted());
}

void test_get_status_real_endpoint_answers(void) {
  configure_cdc(TU_BIT(6), cfg_cdc_notif_86, false);

  setup_request(0x82, TUSB_REQ_GET_STATUS, 0, EDPT_DATA_IN, 2);
  dcd_edpt_xfer_ExpectAndReturn(rhport, EDPT_CTRL_IN, NULL, 2, false, true);
  dcd_edpt_xfer_IgnoreArg_buffer();
  tud_task();
}

void test_get_status_placeholder_past_max_stalls(void) {
  configure_cdc(TU_BIT(6), cfg_cdc_notif_86, false);

  setup_request(0x82, TUSB_REQ_GET_STATUS, 0, 0x86, 2);
  expect_stall_ep0();
  tud_task();
}

void test_get_status_in_range_unregistered_answers(void) {
  configure_cdc(0, cfg_cdc_notif_81, true);

  setup_request(0x82, TUSB_REQ_GET_STATUS, 0, 0x81, 2);
  dcd_edpt_xfer_ExpectAndReturn(rhport, EDPT_CTRL_IN, NULL, 2, false, true);
  dcd_edpt_xfer_IgnoreArg_buffer();
  tud_task();
}

void test_get_status_in_range_placeholder_stalls(void) {
  configure_cdc(TU_BIT(1), cfg_cdc_notif_81, false);

  setup_request(0x82, TUSB_REQ_GET_STATUS, 0, 0x81, 2);
  expect_stall_ep0();
  tud_task();
}

void test_halt_feature_placeholder_stalls(void) {
  configure_cdc(TU_BIT(1), cfg_cdc_notif_81, false);

  setup_request(0x02, TUSB_REQ_SET_FEATURE, TUSB_REQ_FEATURE_EDPT_HALT, 0x81, 0);
  expect_stall_ep0();
  tud_task();

  setup_request(0x02, TUSB_REQ_CLEAR_FEATURE, TUSB_REQ_FEATURE_EDPT_HALT, 0x81, 0);
  expect_stall_ep0();
  tud_task();
}

//--------------------------------------------------------------------+
// Class-facing endpoint API
//--------------------------------------------------------------------+

void test_class_api_refused_without_dcd(void) {
  configure_cdc(TU_BIT(6), cfg_cdc_notif_86, false);
  uint8_t buf[8] = {0};

  tu_fifo_t ff;
  uint8_t   ff_buf[8];
  tu_fifo_config(&ff, ff_buf, sizeof(ff_buf), false);

  TEST_ASSERT_FALSE(usbd_edpt_claim(rhport, 0x86));
  TEST_ASSERT_FALSE(usbd_edpt_release(rhport, 0x86));
  TEST_ASSERT_FALSE(usbd_edpt_xfer(rhport, 0x86, buf, sizeof(buf), false));
  TEST_ASSERT_FALSE(usbd_edpt_xfer_fifo(rhport, 0x86, &ff, sizeof(buf), false));
  TEST_ASSERT_TRUE(usbd_edpt_busy(rhport, 0x86));
  TEST_ASSERT_FALSE(usbd_edpt_ready(rhport, 0x86));
  usbd_edpt_stall(rhport, 0x86);
  TEST_ASSERT_FALSE(usbd_edpt_stalled(rhport, 0x86));
  usbd_edpt_clear_stall(rhport, 0x86);
  usbd_edpt_abort(rhport, 0x86);

  cdc_notify_uart_state_t state = {.value = 1};
  TEST_ASSERT_FALSE(tud_cdc_n_notify_uart_state(0, &state));
}

// In range, so that only the placeholder check, not the CFG_TUD_ENDPPOINT_MAX bound, can refuse it
void test_iso_placeholder_refused(void) {
  TEST_ASSERT_TRUE(register_placeholders(TU_BIT(1), 0));
  const tusb_desc_endpoint_t iso = {.bLength          = sizeof(tusb_desc_endpoint_t),
                                    .bDescriptorType  = TUSB_DESC_ENDPOINT,
                                    .bEndpointAddress = 0x81,
                                    .bmAttributes     = {.xfer = TUSB_XFER_ISOCHRONOUS},
                                    .wMaxPacketSize   = 64,
                                    .bInterval        = 1};

  TEST_ASSERT_FALSE(usbd_edpt_open(rhport, &iso));
  TEST_ASSERT_FALSE(usbd_edpt_iso_alloc(rhport, 0x81, 64));
  TEST_ASSERT_FALSE(usbd_edpt_iso_activate(rhport, &iso));
}

//--------------------------------------------------------------------+
// Registration
//--------------------------------------------------------------------+

void test_configure_refuses_ep0_and_null(void) {
  TEST_ASSERT_FALSE(register_placeholders(TU_BIT(0), 0));
  TEST_ASSERT_FALSE(register_placeholders(0, TU_BIT(0)));
  TEST_ASSERT_FALSE(tud_configure(rhport, TUD_CFGID_EDPT_PLACEHOLDER, NULL));
}

void test_configure_refused_while_mounted(void) {
  configure_cdc(TU_BIT(6), cfg_cdc_notif_86, false);
  TEST_ASSERT_FALSE(register_placeholders(TU_BIT(7), 0));
}

void test_configure_other_id_reaches_dcd(void) {
  const tud_configure_dwc2_t dwc2 = {.bm_double_buffered = 0, .vbus_sensing = false};
  dcd_configure_ExpectAndReturn(rhport, TUD_CFGID_DWC2, &dwc2, true);
  TEST_ASSERT_TRUE(tud_configure(rhport, TUD_CFGID_DWC2, &dwc2));
}
