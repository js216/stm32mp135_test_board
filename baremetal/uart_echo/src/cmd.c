// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file cmd.c
 * @brief Line-edit shell with a single command (`reset`).  Echoes
 *        typed characters, supports backspace, Ctrl-L, Ctrl-C.  Lines
 *        that don't match a command are rejected as Unknown command.
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#include "cmd.h"
#include "console.h"
#include "ddr.h"
#include "printf.h"
#include "stm32mp135fxx_ca7.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CMD_MAX_LEN 32

struct cmd {
   const char *name;
   void (*handler)(int, uint32_t, uint32_t, uint32_t);
};

static const struct cmd cmd_list[] = {
    {.name = "reset",   .handler = cmd_reset},
    {.name = "ddrtest", .handler = ddr_align_test},
};

static char line_buf[CMD_MAX_LEN];
static size_t line_len = 0;

static void cmd_prompt(void)
{
   line_len = 0;
   memset(line_buf, 0, CMD_MAX_LEN);
   my_printf("> ");
}

void cmd_init(void)
{
   my_printf("\r\n");
   cmd_prompt();
}

static void execute_command(void)
{
   if (line_len == 0) {
      cmd_prompt();
      return;
   }

   line_buf[line_len] = '\0';

   for (size_t i = 0; i < sizeof(cmd_list) / sizeof(cmd_list[0]); i++) {
      if (strcmp(line_buf, cmd_list[i].name) == 0) {
         cmd_list[i].handler(0, 0, 0, 0);
         cmd_prompt();
         return;
      }
   }

   my_printf("Unknown command '%s'.\r\n", line_buf);
   cmd_prompt();
}

void cmd_poll(void)
{
   while (!console_rx_empty()) {
      char byte = console_rx_get();

      if (byte == '\r' || byte == '\n') {
         my_printf("\r\n");
         execute_command();
      } else if (byte == '\b' || byte == 0x7F) {
         if (line_len > 0) {
            line_len--;
            line_buf[line_len] = '\0';
            my_printf("\b \b");
         }
      } else if (byte >= 0x20 && byte <= 0x7E) {
         if (line_len < CMD_MAX_LEN - 1) {
            line_buf[line_len++] = byte;
            my_printf("%c", byte);
         }
      } else if (byte == 0x0C) { /* Ctrl-L */
         my_printf("%c", byte);
         cmd_prompt();
      }
   }

   if (console_interrupted())
      cmd_prompt();
}

void cmd_reset(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
   (void)argc;
   (void)arg1;
   (void)arg2;
   (void)arg3;

   my_printf("System reset requested...\r\n");

   __asm__ volatile("dsb sy");
   RCC->MP_GRSTCSETR = RCC_MP_GRSTCSETR_MPSYSRST;

   while (1)
      __asm__ volatile("wfe");
}
