// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jakob Kastelic
// main.c --- minimal MP135 PRBS skeleton (LFSR step only).
#include <stdint.h>

#define PRBS_POLY 0x80200003u
#define PRBS_SEED 0x00000001u

static inline uint32_t prbs_step(uint32_t state)
{
   if (state & 1u) {
      return (state >> 1) ^ PRBS_POLY;
   }
   return (state >> 1);
}

void main(void)
{
   volatile uint32_t state = PRBS_SEED;
   state = prbs_step(state);
   (void)state;
   while (1) {
   }
}

// end file main.c
