// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file main.c
 * @brief Application entry point
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#include "setup.h"
#include "stm32mp135fxx_ca7.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_def.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_rcc.h"
#include "stm32mp13xx_hal_sd.h"
#include <stdint.h>
#include "printf.h"

static void print_ddr(const int num_words)
{
   for (int i = 0; i < num_words / 4; i += 4) {
      // print address
      printf("0x%08x : ", 4 * i);

      // print in hex
      for (int j = 0; j < 4; j++) {
         uint32_t *p       = (uint32_t *)(DRAM_MEM_BASE + (4 * (i + j)));
         const uint32_t i0 = *p;
         for (int k = 0; k < 4; k++) {
            const char c = (i0 & (0XFFU << k * 8U)) >> k * 8U;
            printf("%02x ", c);
         }
         printf(" ");
      }

      // print as ASCII
      for (int j = 0; j < 4; j++) {
         uint32_t *p       = (uint32_t *)(DRAM_MEM_BASE + (4 * (i + j)));
         const uint32_t i0 = *p;
         for (int k = 0; k < 4; k++) {
            const char c = (i0 & (0XFFU << k * 8U)) >> k * 8U;
            if (c >= 0x20 && c <= 0x7e)
               printf("%c", c);
            else
               printf(".");
         }
      }

      printf("\r\n");
   }
}

static void read_sd_blocking(void)
{
   const int app_offset   = 0;
   const int num_blocks   = 1;
   const int read_timeout = 3000;

   static uint8_t block[BLOCKSIZE]; // static array for one block

   if (HAL_SD_ReadBlocks(&sd_handle, block, app_offset, num_blocks,
                         read_timeout) != HAL_OK) {
      printf("Error in HAL_SD_ReadBlocks()\r\n");
      Error_Handler();
   }

   // Copy to DRAM in 32-bit words
   // (Copying byte by byte cause weird data corruption)
   uint32_t *p = (uint32_t *)DRAM_MEM_BASE;
   for (unsigned int i = 0; i < 512; i += 4) {
      uint32_t word = ((uint32_t)block[i + 3U] << 24U) |
                      ((uint32_t)block[i + 2U] << 16U) |
                      ((uint32_t)block[i + 1U] << 8U) | (uint32_t)block[i];
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
   setup_sd();
   usb_init();

   HAL_Delay(1000);
   printf("Hello, world!\r\n");

   // SD and DDR test
   read_sd_blocking();
   print_ddr(BLOCKSIZE / 4);

   while (1) {
      printf(":");
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(1000);
   }
}
