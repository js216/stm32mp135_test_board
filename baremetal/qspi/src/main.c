// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file main.c
 * @brief QSPI bring-up.  After cold-boot init, configure QUADSPI at ~1 MHz
 *        and run a UART CLI shell that exercises every command the Linux
 *        m25p80 driver would issue.  In the background (when no CLI command
 *        is busy) keep auto-issuing JEDEC reads so a passive monitor can
 *        confirm the bus stays alive.
 * @author Jakob Kastelic
 * @copyright 2026 Jakob Kastelic
 */

#include "cli.h"
#include "ddr.h"
#include "printf.h"
#include "qspi.h"
#include "setup.h"
#include "stm32mp13xx_hal.h"
#include <stdint.h>

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

   my_printf("\r\nqspi bring-up\r\n");

   if (qspi_init(203U, 23U, 1U, 0U, false) != HAL_OK) {
      my_printf("qspi_init failed\r\n");
      while (1)
         ;
   }
   my_printf("qspi: presc=203 fsize=23 csht=1\r\n");

   const qspi_cmd_t id_cmd = {
       .instruction = 0x9FU,
       .inst_lines  = QSPI_LINES_1,
       .data_lines  = QSPI_LINES_1,
       .data_len    = 3U,
   };

   cli_init();

   uint32_t last_print = 0;
   uint32_t iters      = 0;
   uint8_t  id[3]      = {0};

   while (1) {
      cli_poll();

      if (cli_idle_jedec_enabled() && !cli_busy()) {
         (void)qspi_xfer(&id_cmd, QSPI_FMODE_READ, id, 100U);
         iters++;
         const uint32_t t = HAL_GetTick();
         if (t - last_print >= 1000U) {
            last_print = t;
            my_printf("\r\nJEDEC ID: %02x %02x %02x  (%lu reads)\r\n> ",
                      id[0], id[1], id[2], (unsigned long)iters);
         }
      }
   }
}
