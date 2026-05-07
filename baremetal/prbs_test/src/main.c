// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jakob Kastelic
// main.c --- MP135 PRBS skeleton with UART ready banner.
#include <stdint.h>

#include "console.h"
#include "printf.h"
#include "setup.h"
#include "stm32mp13xx_hal.h"

#define PRBS_POLY 0x80200003u
#define PRBS_SEED 0x00000001u

typedef struct {
   uint32_t state;
   uint32_t checksum;
} prbs_state_t;

static inline uint32_t prbs_step(uint32_t state)
{
   if (state & 1u) {
      return (state >> 1) ^ PRBS_POLY;
   }
   return (state >> 1);
}

static inline void prbs_step_with_checksum(prbs_state_t *s)
{
   uint32_t prev = s->state;
   s->state = prbs_step(prev);
   s->checksum ^= prev;
}

static void prbs_test_handle_command(volatile prbs_state_t *s, char command)
{
   switch (command) {
   case 'r':
      s->state = PRBS_SEED;
      s->checksum = 0;
      my_printf("prbs reset\r\n");
      break;
   default:
      break;
   }
}

static void prbs_test_poll_commands(volatile prbs_state_t *s)
{
   while (!console_rx_empty()) {
      prbs_test_handle_command(s, console_rx_get());
   }
}

int main(void)
{
   HAL_Init();
   sysclk_init();
   perclk_init();
   uart4_init();

   my_printf("prbs_test ready\r\n");

   volatile uint32_t state = PRBS_SEED;
   state = prbs_step(state);
   (void)state;
   volatile prbs_state_t pstate = { PRBS_SEED, 0 };
   prbs_step_with_checksum((prbs_state_t *)&pstate);
   prbs_step_with_checksum((prbs_state_t *)&pstate);
   prbs_step_with_checksum((prbs_state_t *)&pstate);
   (void)pstate.checksum;
   while (1) {
      prbs_test_poll_commands(&pstate);
   }

   return 0;
}

// end file main.c
