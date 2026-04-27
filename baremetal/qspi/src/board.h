// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file board.h
 * @brief QSPI pin map (per-board)
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 *
 * AF assignments are scattered across AF9/10/11/13 -- this irregularity is
 * real (DS13483 Table 9), not a copy/paste mistake.
 */

#ifndef BOARD_H
#define BOARD_H

#include "stm32mp13xx_hal.h"

#ifdef EVB /* STM32MP135F-DK Discovery, CN8 connector mapping */

/* WARNING: NCS=PD1 and IO2=PH6 also serve I2C5/CTP -- this mapping
 * conflicts with on-board LCD/CTP and is only useful when those are off. */
#define QSPI_CLK_PORT GPIOF
#define QSPI_CLK_PIN  GPIO_PIN_10
#define QSPI_CLK_AF   GPIO_AF9_QUADSPI
#define QSPI_NCS_PORT GPIOD
#define QSPI_NCS_PIN  GPIO_PIN_1
#define QSPI_NCS_AF   GPIO_AF9_QUADSPI
#define QSPI_IO0_PORT GPIOH
#define QSPI_IO0_PIN  GPIO_PIN_3
#define QSPI_IO0_AF   GPIO_AF13_QUADSPI
#define QSPI_IO1_PORT GPIOF
#define QSPI_IO1_PIN  GPIO_PIN_9
#define QSPI_IO1_AF   GPIO_AF10_QUADSPI
#define QSPI_IO2_PORT GPIOH
#define QSPI_IO2_PIN  GPIO_PIN_6
#define QSPI_IO2_AF   GPIO_AF9_QUADSPI
#define QSPI_IO3_PORT GPIOH
#define QSPI_IO3_PIN  GPIO_PIN_7
#define QSPI_IO3_AF   GPIO_AF13_QUADSPI

#else /* SR835-MB custom board */

#define QSPI_CLK_PORT GPIOF
#define QSPI_CLK_PIN  GPIO_PIN_10
#define QSPI_CLK_AF   GPIO_AF9_QUADSPI
#define QSPI_NCS_PORT GPIOE
#define QSPI_NCS_PIN  GPIO_PIN_14
#define QSPI_NCS_AF   GPIO_AF9_QUADSPI
#define QSPI_IO0_PORT GPIOF
#define QSPI_IO0_PIN  GPIO_PIN_8
#define QSPI_IO0_AF   GPIO_AF10_QUADSPI
#define QSPI_IO1_PORT GPIOF
#define QSPI_IO1_PIN  GPIO_PIN_9
#define QSPI_IO1_AF   GPIO_AF10_QUADSPI
#define QSPI_IO2_PORT GPIOD
#define QSPI_IO2_PIN  GPIO_PIN_7
#define QSPI_IO2_AF   GPIO_AF11_QUADSPI
#define QSPI_IO3_PORT GPIOH
#define QSPI_IO3_PIN  GPIO_PIN_7
#define QSPI_IO3_AF   GPIO_AF13_QUADSPI

#endif

#endif
