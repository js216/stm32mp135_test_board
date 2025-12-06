// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file setup.h
 * @brief Driver and board low-level setup
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#ifndef SETUP_H
#define SETUP_H

#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_ddr.h"
#include "stm32mp13xx_hal_pcd.h"
#include "stm32mp13xx_hal_sd.h"
#include "stm32mp13xx_hal_uart.h"

// global variables
extern SD_HandleTypeDef sd_handle;

// clocks and memory
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
void setup_ddr(void);

// HAL MSP
int HAL_DDR_MspInit(ddr_type type);
void HAL_UART_MspInit(UART_HandleTypeDef *huart);
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart);
void HAL_SD_MspInit(SD_HandleTypeDef *hsd);

// pin mux
void MX_UART4_Init(void);

// debugging
int __io_putchar(int ch);
int __io_getchar(void);
void Error_Handler(void);

// SD
void setup_sd(void);

// USB
void usb_init(void);

#endif // SETUP_H

// end file setup.h
