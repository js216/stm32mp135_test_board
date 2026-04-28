// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file irq.h
 * @brief Interrupt handlers.
 * @author Jakob Kastelic
 * @copyright 2025-2026 Stanford Research Systems, Inc.
 */

#ifndef IRQ_H
#define IRQ_H

#define ENTER_CRITICAL_SECTION(periph) ((void)0)
#define EXIT_CRITICAL_SECTION(periph)  ((void)0)

enum {
   PRIO_STPMIC = 0,
   PRIO_SD     = 1,
   PRIO_ETH    = 4,
   PRIO_USB    = 5,
   PRIO_UART   = 7,
   PRIO_TICK   = 10,
   PRIO_GPIO   = 15,
   PRIO_LTDC   = 16,
};

void reset_handler(void) __attribute__((naked, target("arm")));
void vectors(void) __attribute__((naked, section("RESET")));

void fiq_handler(void);
void rsvd_handler(void);
void dabt_handler(void);
void pabt_handler(void);
void svc_handler(void);
void undef_handler(void);
void irq_handler(void);

void OTG_IRQHandler(void);
void SDMMC1_IRQHandler(void);
void SecurePhysicalTimer_IRQHandler(void);
void EXTI8_IRQHandler(void);
void UART4_IRQHandler(void);
void EXTI12_IRQHandler(void);
void EXTI5_IRQHandler(void);

#endif // IRQ_H
