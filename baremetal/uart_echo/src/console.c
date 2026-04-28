// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file console.c
 * @brief UART receive buffer and interrupt flag
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#include "console.h"
#include "printf.h"
#include <stdint.h>

#define RXBUF_SIZE 64U

static volatile uint8_t rx_buf[RXBUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

static volatile int interrupt_flag = 0;
static volatile int key_flag       = 0;

void console_push(char c)
{
   if (c == 0x03) {
      interrupt_flag = 1;
      my_printf("^C\r\n");
      return;
   }
   uint8_t next = (rx_head + 1U) % RXBUF_SIZE;
   if (next != rx_tail) {
      rx_buf[rx_head] = (uint8_t)c;
      rx_head         = next;
   }
   key_flag = 1;
}

int console_interrupted(void)
{
   if (interrupt_flag) {
      interrupt_flag = 0;
      return 1;
   }
   return 0;
}

int console_key_pressed(void)
{
   if (key_flag) {
      key_flag = 0;
      return 1;
   }
   return 0;
}

int console_rx_empty(void)
{
   return rx_tail == rx_head;
}

char console_rx_get(void)
{
   char c  = (char)rx_buf[rx_tail];
   rx_tail = (rx_tail + 1U) % RXBUF_SIZE;
   return c;
}

// end file console.c
