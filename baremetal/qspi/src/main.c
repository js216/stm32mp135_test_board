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
   /* Custom-board bring-up: clocks + PMIC (DDR rails) + DDR for the 4-lane
    * MDMA->DDR stream, UART + ETZPC + GIC for the CLI. The MMU/caches stay
    * off so the polled CLI sees live UART registers and the stream stays
    * coherent (qspi_mdma_finish_no_inval + volatile DDR reads, CRC via the
    * CRC-DMA); the EVB keeps gpio_init + MMU/caches. */
   HAL_Init();
   sysclk_init();
#ifdef EVB
   pmic_init();
#endif
   perclk_init();
   uart4_init();
   etzpc_init();
   gic_init();
#ifdef EVB
   gpio_init();
#endif
   ddr_init();
#ifdef EVB
   mmu_init();
#endif

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
   (void)last_print;
   (void)iters;
   (void)id;
   (void)id_cmd;

   while (1) {
      cli_poll();

      /* The idle JEDEC liveness read is a 1-1-1 flash command. The custom
       * board talks to an FPGA streaming slave (not a flash); that 1-1-1 read
       * path drains the FIFO via a TCF-guarded 32-bit DR read, which stalls
       * the AHB on the custom wiring. Skip it there -- the CLI transfer
       * commands use the FLEVEL-guarded quad read path, which cannot stall. */
#ifndef BOARD_CUSTOM
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
#endif
   }
}
