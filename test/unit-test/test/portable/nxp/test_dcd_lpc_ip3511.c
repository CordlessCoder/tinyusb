/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 TinyUSB contributors
 * SPDX-License-Identifier: MIT
 */

// Endpoint number bounds of the IP3511 device driver, built for the LPC55S69: USB0 has five
// endpoint pairs and USB1 six, while TUP_DCD_ENDPOINT_MAX is six for the whole MCU.

#include <string.h>

#include "unity.h"

#include "device/dcd.h"
TEST_SOURCE_FILE("dcd_lpc_ip3511.c")

#include "fsl_device_registers.h"

uint32_t ip3511_test_usb0_regs[16];
uint32_t ip3511_test_usb1_regs[16];

enum {
  REG_INTEN = 0x24 / 4,
};

// usbd's event sink: only the interrupt handler reports events, and these tests never run it.
void dcd_event_handler(const dcd_event_t *event, bool in_isr) {
  (void)event;
  (void)in_isr;
  TEST_FAIL_MESSAGE("unexpected dcd event");
}

static tusb_desc_endpoint_t edpt_desc(uint8_t ep_addr, uint8_t xfer) {
  tusb_desc_endpoint_t desc = {.bLength          = sizeof(tusb_desc_endpoint_t),
                               .bDescriptorType  = TUSB_DESC_ENDPOINT,
                               .bEndpointAddress = ep_addr,
                               .wMaxPacketSize   = 64,
                               .bInterval        = 1};
  desc.bmAttributes.xfer    = xfer;
  return desc;
}

void setUp(void) {
  memset(ip3511_test_usb0_regs, 0, sizeof(ip3511_test_usb0_regs));
  memset(ip3511_test_usb1_regs, 0, sizeof(ip3511_test_usb1_regs));
  // every entry of the shared list disabled, as after a bus reset on USB1
  dcd_edpt_close_all(1);
}

void tearDown(void) {
}

void test_usb0_opens_ep4(void) {
  const tusb_desc_endpoint_t desc = edpt_desc(0x84, TUSB_XFER_BULK);
  TEST_ASSERT_TRUE(dcd_edpt_open(0, &desc));
  TEST_ASSERT_EQUAL_HEX32(TU_BIT(9), ip3511_test_usb0_regs[REG_INTEN]);
}

void test_usb0_refuses_ep5(void) {
  const tusb_desc_endpoint_t desc = edpt_desc(0x85, TUSB_XFER_BULK);
  TEST_ASSERT_FALSE(dcd_edpt_open(0, &desc));
  TEST_ASSERT_EQUAL_HEX32(0, ip3511_test_usb0_regs[REG_INTEN]);
}

void test_usb1_opens_ep5(void) {
  const tusb_desc_endpoint_t desc = edpt_desc(0x85, TUSB_XFER_BULK);
  TEST_ASSERT_TRUE(dcd_edpt_open(1, &desc));
  TEST_ASSERT_EQUAL_HEX32(TU_BIT(11), ip3511_test_usb1_regs[REG_INTEN]);
}

void test_usb1_refuses_ep6(void) {
  const tusb_desc_endpoint_t desc = edpt_desc(0x86, TUSB_XFER_INTERRUPT);
  TEST_ASSERT_FALSE(dcd_edpt_open(1, &desc));
  TEST_ASSERT_EQUAL_HEX32(0, ip3511_test_usb1_regs[REG_INTEN]);
}

void test_usb0_iso_alloc_refuses_ep5(void) {
  TEST_ASSERT_FALSE(dcd_edpt_iso_alloc(0, 0x85, 64));
  TEST_ASSERT_EQUAL_HEX32(0, ip3511_test_usb0_regs[REG_INTEN]);
}

void test_usb1_iso_alloc_ep5(void) {
  TEST_ASSERT_TRUE(dcd_edpt_iso_alloc(1, 0x85, 64));
  TEST_ASSERT_EQUAL_HEX32(TU_BIT(11), ip3511_test_usb1_regs[REG_INTEN]);
}
