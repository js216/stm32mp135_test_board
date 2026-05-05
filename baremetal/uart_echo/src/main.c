// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file main.c
 * @brief Application entry point: bare UART echo CLI.
 * @author Jakob Kastelic
 * @copyright 2026 Jakob Kastelic
 */

#include "cmd.h"
#include "ddr.h"
#include "setup.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_gpio.h"
#include <stdint.h>

static void blink(void)
{
   static uint32_t last_blink = 0;
   uint32_t now               = HAL_GetTick();
   if (now - last_blink >= 1000) {
      last_blink = now;
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
   }
}

int main(void)
{
   HAL_Init();
   sysclk_init();
   pmic_init();
   perclk_init();
   uart4_init();
   etzpc_init();
   gic_init();
   gpio_init();
   ddr_init();
   mmu_init();

   cmd_init();

   while (1) {
      cmd_poll();
      blink();
   }

   return 0;
}
