/* SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file stm32mp13xx_hal_conf.h
 * @brief HAL configuration
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#ifndef __STM32MP13xx_HAL_CONF_H
#define __STM32MP13xx_HAL_CONF_H

#define CORE_CA7
#define STM32MP1
#define STM32MP135Fxx

#define HSE_VALUE            24000000U
#define HSE_STARTUP_TIMEOUT  100U
#define HSI_VALUE            64000000U
#define LSI_VALUE            32000U
#define LSE_VALUE            32768U
#define LSE_STARTUP_TIMEOUT  1000U
#define CSI_VALUE            4000000U
#define EXTERNAL_CLOCK_VALUE 12288000U

#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define USE_RTOS                        0
#define USE_HAL_UART_REGISTER_CALLBACKS 0
#define USE_HAL_I2C_REGISTER_CALLBACKS  0

#endif /* __STM32MP13xx_HAL_CONF_H */
