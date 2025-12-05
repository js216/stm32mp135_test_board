/* SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file hal_conf.h
 * @brief HAL configuration.
 * @author Jakob Kastelic
 * @copyright 2021 STMicroelectronics
 */

#ifndef __STM32MP13xx_HAL_CONF_H
#define __STM32MP13xx_HAL_CONF_H

#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_def.h"
#include <stdint.h>

// Oscillator Values adaptation
#define HSE_VALUE            ((uint32_t)24000000U)
#define HSE_STARTUP_TIMEOUT  ((uint32_t)100U)
#define HSI_VALUE            ((uint32_t)64000000U)
#define LSI_VALUE            32000U
#define LSE_VALUE            ((uint32_t)32768U)
#define LSE_STARTUP_TIMEOUT  ((uint32_t)5000U)
#define CSI_VALUE            4000000U
#define EXTERNAL_CLOCK_VALUE 12288000U

// System Configuration
#define TICK_INT_PRIORITY 0x0FU /*!< tick interrupt priority */

#define assert_param(expr)                                                     \
   ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);

#endif /* __STM32MP13xx_HAL_CONF_H */
