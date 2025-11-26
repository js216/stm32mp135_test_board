#include <stdio.h>
#include <ctype.h>
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_disco_stpmic1.h"
#include "setup.h"

void print_ddr(const int num_words)
{
   for (int i=0; i<num_words/4; i+=4) {
      // print address
      printf("0x%08x : ", 4*i);

      // print in hex
      for (int j=0; j<4; j++) {
         uint32_t *p = (uint32_t*)(DRAM_MEM_BASE + 4*(i+j));
         const int i0 = *p;
         for (int k=0; k<4; k++) {
            const char c = (i0 & (0xff << k*8)) >> k*8;
            printf("%02x ", c);
         }
         printf(" ");
      }

      // print as ASCII
      for (int j=0; j<4; j++) {
         uint32_t *p = (uint32_t*)(DRAM_MEM_BASE + 4*(i+j));
         const int i0 = *p;
         for (int k=0; k<4; k++) {
            const char c = (i0 & (0xff << k*8)) >> k*8;
            if (isprint(c))
               printf("%c", c);
            else
               printf(".");
         }
      }

      printf("\r\n");
   }
}


void read_sd_blocking(const int app_offset, const int num_blocks)
{
   const int read_timeout = 3000;
   SD_HandleTypeDef SDHandle = setup_sd();
   if (HAL_SD_ReadBlocks(&SDHandle, (uint8_t*)DRAM_MEM_BASE, app_offset, num_blocks, read_timeout) != HAL_OK) {
      printf("Error in HAL_SD_ReadBlocks()\r\n");
      Error_Handler();
   }
}

void print_sd(const int offs, const int num_blocks)
{
   read_sd_blocking(0, 132);
   print_ddr(BLOCKSIZE / 4);
}

int main(void)
{
   HAL_Init();
   SystemClock_Config();
   BSP_PMIC_Init();
   BSP_PMIC_InitRegulators();
   PeriphCommonClock_Config();
   MX_UART4_Init();
   __HAL_RCC_GPIOA_CLK_ENABLE();
   setup_ddr();
   setup_sd();

   while (1) {
      printf("\r\nHello World!\r\n");
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(500);
      print_sd(0, 10);
   }
}
