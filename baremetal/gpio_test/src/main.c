// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Jakob Kastelic

#include <stddef.h>

#include "console.h"
#include "connectivity_mpu_replay.h"
#include "printf.h"
#include "setup.h"
#include "stm32mp13xx_hal.h"

int gpio_connectivity_mpu_replay_stub_run(void);
int gpio_connectivity_mpu_replay_io0_high_report(void);
int gpio_connectivity_mpu_replay_io0_low_report(void);
int gpio_connectivity_mpu_replay_io1_high_report(void);
int gpio_connectivity_mpu_replay_io1_low_report(void);
int gpio_connectivity_mpu_replay_io2_high_report(void);
int gpio_connectivity_mpu_replay_io2_low_report(void);
int gpio_connectivity_mpu_replay_io3_high_report(void);
int gpio_connectivity_mpu_replay_io0_sample_report(void);
int gpio_connectivity_mpu_replay_io1_sample_report(void);
int gpio_connectivity_mpu_replay_io2_sample_report(void);
int gpio_connectivity_mpu_replay_io3_sample_report(void);
int gpio_connectivity_mpu_replay_ncs_drive_report(void);
int gpio_connectivity_mpu_replay_ncs_low_report(void);
int gpio_connectivity_mpu_replay_sclk_drive_report(void);
int gpio_connectivity_mpu_replay_sclk_low_report(void);

static volatile size_t replay_command_count;
static volatile int replay_status;
static volatile int io0_hold_high;
static volatile int io0_hold_low;
static volatile int io1_hold_high;
static volatile int io1_hold_low;
static volatile int io2_hold_high;
static volatile int io2_hold_low;
static volatile int io3_hold_high;
static volatile int ncs_hold_low;

static void gpio_test_handle_command(char command)
{
   if (command == 's') {
      gpio_connectivity_mpu_replay_sclk_drive_report();
   } else if (command == 'l') {
      gpio_connectivity_mpu_replay_sclk_low_report();
   } else if (command == '0') {
      if (gpio_connectivity_mpu_replay_io0_high_report())
         io0_hold_high = 1;
   } else if (command == 'q') {
      if (gpio_connectivity_mpu_replay_io0_low_report())
         io0_hold_low = 1;
   } else if (command == '1') {
      if (gpio_connectivity_mpu_replay_io1_high_report())
         io1_hold_high = 1;
   } else if (command == 'r') {
      if (gpio_connectivity_mpu_replay_io1_low_report())
         io1_hold_low = 1;
   } else if (command == '2') {
      if (gpio_connectivity_mpu_replay_io2_high_report())
         io2_hold_high = 1;
   } else if (command == 'a') {
      if (gpio_connectivity_mpu_replay_io2_low_report())
         io2_hold_low = 1;
   } else if (command == '3') {
      if (gpio_connectivity_mpu_replay_io3_high_report())
         io3_hold_high = 1;
   } else if (command == 'n') {
      if (gpio_connectivity_mpu_replay_ncs_low_report())
         ncs_hold_low = 1;
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
      if (!io0_hold_high)
         if (!io0_hold_low)
            gpio_connectivity_mpu_replay_io0_sample_report();
      if (!io1_hold_high)
         if (!io1_hold_low)
            gpio_connectivity_mpu_replay_io1_sample_report();
      if (!io2_hold_high)
         if (!io2_hold_low)
            gpio_connectivity_mpu_replay_io2_sample_report();
      if (!io3_hold_high)
         gpio_connectivity_mpu_replay_io3_sample_report();
      if (!ncs_hold_low)
         gpio_connectivity_mpu_replay_ncs_drive_report();
      my_printf("gpio_test ready\r\n");
      HAL_Delay(1000);
   }

   return replay_status;
}
