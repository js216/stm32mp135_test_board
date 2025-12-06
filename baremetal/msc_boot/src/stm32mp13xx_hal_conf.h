/* SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file stm32mp13xx_hal_conf.h
 * @brief HAL configuration.
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#ifndef __STM32MP13xx_HAL_CONF_H
#define __STM32MP13xx_HAL_CONF_H

#define HSE_VALUE            24000000U
#define HSE_STARTUP_TIMEOUT  100U
#define HSI_VALUE            64000000U
#define LSI_VALUE            32000U
#define LSE_VALUE            32768U
#define LSE_STARTUP_TIMEOUT  5000U
#define CSI_VALUE            4000000U
#define EXTERNAL_CLOCK_VALUE 12288000U

#define TICK_INT_PRIORITY    0x0FU

#define USE_RTOS             0
#define USE_SD_TRANSCEIVER   0
#define USBD_CLASS_BOS_ENABLED 0
#define USE_HAL_PCD_REGISTER_CALLBACKS 0
#define USE_HAL_UART_REGISTER_CALLBACKS 0
#define USE_HAL_I2C_REGISTER_CALLBACKS 0

#define assert_param(expr)                                                     \
   ((expr) ? (void)0U : assert_failed((int *)__FILE__, __LINE__))

void assert_failed(int *file, int line);

#endif /* __STM32MP13xx_HAL_CONF_H */
