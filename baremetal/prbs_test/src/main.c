// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jakob Kastelic
// main.c --- minimal MP135 PRBS skeleton (LFSR step + XOR checksum).
#include <stdint.h>

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

void main(void)
{
   volatile uint32_t state = PRBS_SEED;
   state = prbs_step(state);
   (void)state;
   volatile prbs_state_t pstate = { PRBS_SEED, 0 };
   prbs_step_with_checksum((prbs_state_t *)&pstate);
   prbs_step_with_checksum((prbs_state_t *)&pstate);
   prbs_step_with_checksum((prbs_state_t *)&pstate);
   (void)pstate.checksum;
   while (1) {
   }
}

// end file main.c
