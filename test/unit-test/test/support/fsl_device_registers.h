/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 TinyUSB contributors
 * SPDX-License-Identifier: MIT
 */

// Stands in for the NXP SDK header when a test builds dcd_lpc_ip3511.c on the host: the LPC55S69's
// USB feature counts, with both controllers' registers in RAM the test defines.

#ifndef FSL_DEVICE_REGISTERS_H_
#define FSL_DEVICE_REGISTERS_H_

#include <stdint.h>

#define __I                          volatile const
#define __IO                         volatile

#define FSL_FEATURE_USB_EP_NUM       5
#define FSL_FEATURE_SOC_USBHSD_COUNT 1
#define FSL_FEATURE_USBHSD_EP_NUM    6

typedef enum {
  USB0_IRQn = 28,
  USB1_IRQn = 47,
} IRQn_Type;

extern uint32_t ip3511_test_usb0_regs[16];
extern uint32_t ip3511_test_usb1_regs[16];

#define USB0_BASE   (ip3511_test_usb0_regs)
#define USBHSD_BASE (ip3511_test_usb1_regs)

static inline void NVIC_EnableIRQ(IRQn_Type irq) {
  (void)irq;
}

static inline void NVIC_DisableIRQ(IRQn_Type irq) {
  (void)irq;
}

static inline void NVIC_ClearPendingIRQ(IRQn_Type irq) {
  (void)irq;
}

#endif
