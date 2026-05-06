// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Jakob Kastelic

#include <stddef.h>

#include "console.h"
#include "connectivity_mpu_replay.h"
#include "printf.h"
#include "setup.h"
#include "stm32mp13xx_hal.h"

int gpio_connectivity_mpu_replay_stub_run(void);
int gpio_connectivity_mpu_replay_io0_sample_report(void);
int gpio_connectivity_mpu_replay_io1_sample_report(void);
int gpio_connectivity_mpu_replay_io2_sample_report(void);
int gpio_connectivity_mpu_replay_io3_sample_report(void);
int gpio_connectivity_mpu_replay_ncs_drive_report(void);
int gpio_connectivity_mpu_replay_sclk_drive_report(void);

static volatile size_t replay_command_count;
static volatile int replay_status;

static void gpio_test_handle_command(char command)
{
   if (command == 's') {
      gpio_connectivity_mpu_replay_sclk_drive_report();
   }
}

static void gpio_test_poll_commands(void)
{
   while (!console_rx_empty()) {
      gpio_test_handle_command(console_rx_get());
   }
}

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
      gpio_test_poll_commands();
      gpio_connectivity_mpu_replay_io0_sample_report();
      gpio_connectivity_mpu_replay_io1_sample_report();
      gpio_connectivity_mpu_replay_io2_sample_report();
      gpio_connectivity_mpu_replay_io3_sample_report();
      gpio_connectivity_mpu_replay_ncs_drive_report();
      my_printf("gpio_test ready\r\n");
      HAL_Delay(1000);
   }

   return replay_status;
}
