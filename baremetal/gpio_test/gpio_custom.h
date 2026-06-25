/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Jakob Kastelic */

#ifndef GPIO_CUSTOM_H
#define GPIO_CUSTOM_H

/* Custom-board (SRS STM32MP135 PCB) GPIO connectivity sweep. Drives each
 * SPI/QUADSPI pin routed to J503 individually so the hx8k FPGA sampler can
 * discover which of its balls each one lands on. Never returns. */
void gpio_custom_run(void);

#endif /* GPIO_CUSTOM_H */
