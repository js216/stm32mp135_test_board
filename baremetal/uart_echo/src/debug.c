// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file debug.c
 * @brief Debugging and diagnostics.
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#include "debug.h"
#include "printf.h"
#include "stm32mp135fxx_ca7.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_gpio.h"

void error_msg(const char *file, const int line, const char *msg)
{
   my_printf("File %s line %d: %s.\r\n", file, line, msg);
   while (1) {
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(25);
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(25);

      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(100);
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(100);

      my_printf("ERROR: %s\r\n", msg);
   }
}
