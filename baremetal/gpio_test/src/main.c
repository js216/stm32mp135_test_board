// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Jakob Kastelic

#include <stddef.h>

#include "connectivity_mpu_replay.h"
#include "printf.h"
#include "setup.h"
#include "stm32mp13xx_hal.h"

int gpio_connectivity_mpu_replay_stub_run(void);

static volatile size_t replay_command_count;
static volatile int replay_status;

int main(void)
{
   HAL_Init();
   sysclk_init();
   perclk_init();
   uart4_init();
   etzpc_init();
   gic_init();
   gpio_init();

   my_printf("gpio_test ready\r\n");

   replay_command_count = gpio_connectivity_mpu_replay_count;
   replay_status        = gpio_connectivity_mpu_replay_stub_run();
   my_printf("gpio_test replay %s\r\n", replay_status == 0 ? "ok" : "fail");

   while (1) {
      my_printf("gpio_test ready\r\n");
      HAL_Delay(1000);
   }

   return replay_status;
}
