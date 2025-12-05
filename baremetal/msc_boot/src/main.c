// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file main.c
 * @brief Application entry point
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#include "setup.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_etzpc.h"
#include <ctype.h>
#include <stdio.h>

void print_ddr(const int num_words)
{
   for (int i = 0; i < num_words / 4; i += 4) {
      // print address
      printf("0x%08x : ", 4 * i);

      // print in hex
      for (int j = 0; j < 4; j++) {
         uint32_t *p  = (uint32_t *)(DRAM_MEM_BASE + 4 * (i + j));
         const int i0 = *p;
         for (int k = 0; k < 4; k++) {
            const char c = (i0 & (0xff << k * 8)) >> k * 8;
            printf("%02x ", c);
         }
         printf(" ");
      }

      // print as ASCII
      for (int j = 0; j < 4; j++) {
         uint32_t *p  = (uint32_t *)(DRAM_MEM_BASE + 4 * (i + j));
         const int i0 = *p;
         for (int k = 0; k < 4; k++) {
            const char c = (i0 & (0xff << k * 8)) >> k * 8;
            if (isprint(c))
               printf("%c", c);
            else
               printf(".");
         }
      }

      printf("\r\n");
   }
}

void fill_dram(const int num_bytes)
{
   uint32_t *p = (uint32_t *)DRAM_MEM_BASE;
   int i       = 0;

   // write 4 bytes per word
   while (i < num_bytes) {
      uint8_t b0 = (uint8_t)(i & 0xFF);
      uint8_t b1 = (uint8_t)((i + 1) & 0xFF);
      uint8_t b2 = (uint8_t)((i + 2) & 0xFF);
      uint8_t b3 = (uint8_t)((i + 3) & 0xFF);

      uint32_t word = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
      *p++          = word;

      i += 4;
   }
}

void read_sd_blocking(void)
{
   const int app_offset   = 0;
   const int num_blocks   = 1;
   const int read_timeout = 3000;

   static uint8_t block[BLOCKSIZE]; // static array for one block

   if (HAL_SD_ReadBlocks(&SDHandle, block, app_offset, num_blocks,
                         read_timeout) != HAL_OK) {
      printf("Error in HAL_SD_ReadBlocks()\r\n");
      Error_Handler();
   }

   // Copy to DRAM in 32-bit words
   // (Copying byte by byte cause weird data corruption)
   uint32_t *p = (uint32_t *)DRAM_MEM_BASE;
   for (int i = 0; i < 512; i += 4) {
      uint32_t word = (block[i + 3] << 24) | (block[i + 2] << 16) |
                      (block[i + 1] << 8) | block[i];
      *p++ = word;
   }
}

int main(void)
{
   HAL_Init();
   SystemClock_Config();
   PeriphCommonClock_Config();
   MX_UART4_Init();
   __HAL_RCC_GPIOA_CLK_ENABLE();
   setup_ddr();
   SDHandle = setup_sd();
   usb_init();

   HAL_Delay(1000);
   printf("Hello, world!\r\n");

   // SD and DDR test
   read_sd_blocking();
   print_ddr(BLOCKSIZE / 4);

   while (1) {
      printf(":");
      fflush(stdout);
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(1000);
   }
}
