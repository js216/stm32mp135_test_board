// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file setup.c
 * @brief Driver and board low-level setup
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#include "setup.h"
#include "stm32mp135fxx_ca7.h"
#include "stm32mp13xx.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_ddr.h"
#include "stm32mp13xx_hal_def.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_gpio_ex.h"
#include "stm32mp13xx_hal_rcc.h"
#include "stm32mp13xx_hal_sd.h"
#include "stm32mp13xx_hal_uart.h"
#include "stm32mp13xx_hal_uart_ex.h"
#include "stm32mp13xx_ll_etzpc.h"
#include "stm32mp13xx_ll_sdmmc.h"
#include "usbd_core.h"
#include "usbd_def.h"
#include "usbd_desc.h"
#include "usbd_msc.h"
#include "usbd_msc_storage.h"
#include <stdint.h>
#include "printf.h"

// global variables
UART_HandleTypeDef huart4;
SD_HandleTypeDef sd_handle;
USBD_HandleTypeDef usbd_device;

static void error_msg(const char *msg)
{
   while (1) {
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(25);
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(25);

      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(100);
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
      HAL_Delay(100);

      printf("ERROR: %s\r\n", msg);
   }
}

void Error_Handler(void)
{
   error_msg("Error_Handler");
   HAL_Delay(1000);
}

void assert_failed(int *file, int line)
{
   printf("File %s line %d: assert failed.\r\n", file, line);

   HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_13);
   HAL_Delay(50);
}

void SystemClock_Config(void)
{
   HAL_RCC_DeInit();
   RCC_ClkInitTypeDef rcc_clkinitstructure;
   RCC_OscInitTypeDef rcc_oscinitstructure;

   /* Enable all available oscillators*/
   rcc_oscinitstructure.OscillatorType =
       (RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSE |
        RCC_OSCILLATORTYPE_CSI | RCC_OSCILLATORTYPE_LSI);

   rcc_oscinitstructure.HSIState = RCC_HSI_ON;
   rcc_oscinitstructure.HSEState = RCC_HSE_ON;
   rcc_oscinitstructure.LSEState = RCC_LSE_OFF;
   rcc_oscinitstructure.LSIState = RCC_LSI_ON;
   rcc_oscinitstructure.CSIState = RCC_CSI_ON;

   rcc_oscinitstructure.HSICalibrationValue = 0x00; // Default reset value
   rcc_oscinitstructure.CSICalibrationValue = 0x10; // Default reset value
   rcc_oscinitstructure.HSIDivValue         = RCC_HSI_DIV1; // Default value

   /* PLL configuration */
   rcc_oscinitstructure.PLL.PLLState  = RCC_PLL_ON;
   rcc_oscinitstructure.PLL.PLLSource = RCC_PLL12SOURCE_HSE;
   rcc_oscinitstructure.PLL.PLLM      = 3;
   rcc_oscinitstructure.PLL.PLLN      = 81;
   rcc_oscinitstructure.PLL.PLLP      = 1;
   rcc_oscinitstructure.PLL.PLLQ      = 2;
   rcc_oscinitstructure.PLL.PLLR      = 2;
   rcc_oscinitstructure.PLL.PLLFRACV  = 0x800;
   rcc_oscinitstructure.PLL.PLLMODE   = RCC_PLL_FRACTIONAL;

   rcc_oscinitstructure.PLL2.PLLState  = RCC_PLL_ON;
   rcc_oscinitstructure.PLL2.PLLSource = RCC_PLL12SOURCE_HSE;
   rcc_oscinitstructure.PLL2.PLLM      = 3;
   rcc_oscinitstructure.PLL2.PLLN      = 66;
   rcc_oscinitstructure.PLL2.PLLP      = 2;
   rcc_oscinitstructure.PLL2.PLLQ      = 2;
   rcc_oscinitstructure.PLL2.PLLR      = 1;
   rcc_oscinitstructure.PLL2.PLLFRACV  = 0x1400;
   rcc_oscinitstructure.PLL2.PLLMODE   = RCC_PLL_FRACTIONAL;

   rcc_oscinitstructure.PLL3.PLLState  = RCC_PLL_ON;
   rcc_oscinitstructure.PLL3.PLLSource = RCC_PLL3SOURCE_HSE;
   rcc_oscinitstructure.PLL3.PLLM      = 2;
   rcc_oscinitstructure.PLL3.PLLN      = 34;
   rcc_oscinitstructure.PLL3.PLLP      = 2;
   rcc_oscinitstructure.PLL3.PLLQ      = 17;
   rcc_oscinitstructure.PLL3.PLLR      = 2;
   rcc_oscinitstructure.PLL3.PLLRGE    = RCC_PLL3IFRANGE_1;
   rcc_oscinitstructure.PLL3.PLLFRACV  = 0x1a04;
   rcc_oscinitstructure.PLL3.PLLMODE   = RCC_PLL_FRACTIONAL;

   rcc_oscinitstructure.PLL4.PLLState  = RCC_PLL_ON;
   rcc_oscinitstructure.PLL4.PLLSource = RCC_PLL4SOURCE_HSE;
   rcc_oscinitstructure.PLL4.PLLM      = 2;
   rcc_oscinitstructure.PLL4.PLLN      = 40;
   rcc_oscinitstructure.PLL4.PLLP      = 10;
   rcc_oscinitstructure.PLL4.PLLQ      = 10;
   rcc_oscinitstructure.PLL4.PLLR      = 10;
   rcc_oscinitstructure.PLL4.PLLRGE    = RCC_PLL4IFRANGE_1;
   rcc_oscinitstructure.PLL4.PLLFRACV  = 0;
   rcc_oscinitstructure.PLL4.PLLMODE   = RCC_PLL_INTEGER;

   /* Enable access to RTC and backup registers */
   SET_BIT(PWR->CR1, PWR_CR1_DBP);

   if (HAL_RCC_OscConfig(&rcc_oscinitstructure) != HAL_OK) {
      error_msg("HAL RCC Osc configuration error");
   }

   /* Select PLLx as MPU, AXI and MCU clock sources */
   rcc_clkinitstructure.ClockType =
       (RCC_CLOCKTYPE_MPU | RCC_CLOCKTYPE_ACLK | RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5 | RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK6 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3);

   rcc_clkinitstructure.MPUInit.MPU_Clock     = RCC_MPUSOURCE_PLL1;
   rcc_clkinitstructure.MPUInit.MPU_Div       = RCC_MPU_DIV2;
   rcc_clkinitstructure.AXISSInit.AXI_Clock   = RCC_AXISSOURCE_PLL2;
   rcc_clkinitstructure.AXISSInit.AXI_Div     = RCC_AXI_DIV1;
   rcc_clkinitstructure.MLAHBInit.MLAHB_Clock = RCC_MLAHBSSOURCE_PLL3;
   rcc_clkinitstructure.MLAHBInit.MLAHB_Div   = RCC_MLAHB_DIV1;
   rcc_clkinitstructure.APB1_Div              = RCC_APB1_DIV2;
   rcc_clkinitstructure.APB2_Div              = RCC_APB2_DIV2;
   rcc_clkinitstructure.APB3_Div              = RCC_APB3_DIV2;
   rcc_clkinitstructure.APB4_Div              = RCC_APB4_DIV2;
   rcc_clkinitstructure.APB5_Div              = RCC_APB5_DIV4;
   rcc_clkinitstructure.APB6_Div              = RCC_APB6_DIV2;

   if (HAL_RCC_ClockConfig(&rcc_clkinitstructure) != HAL_OK) {
      error_msg("HAL RCC Clk configuration error");
   }

   /*
Note : The activation of the I/O Compensation Cell is recommended with
communication  interfaces (GPIO, SPI, FMC, QSPI ...)  when  operating at  high
frequencies(please refer to product datasheet) The I/O Compensation Cell
activation  procedure requires :
- The activation of the CSI clock
- The activation of the SYSCFG clock
- Enabling the I/O Compensation Cell : setting bit[0] of register SYSCFG_CCCSR

To do this please uncomment the following code
*/

   /*
      __HAL_RCC_CSI_ENABLE() ;
      __HAL_RCC_SYSCFG_CLK_ENABLE() ;
      HAL_EnableCompensationCell();
      */
}

void PeriphCommonClock_Config(void)
{
   RCC_PeriphCLKInitTypeDef pclk = {0};

   pclk.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
   pclk.CkperClockSelection  = RCC_CKPERCLKSOURCE_HSE;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("CKPER");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_ETH1;
   pclk.Eth1ClockSelection   = RCC_ETH1CLKSOURCE_PLL4;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("ETH1");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_ETH2;
   pclk.Eth2ClockSelection   = RCC_ETH2CLKSOURCE_PLL4;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("ETH2");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_SDMMC1;
   pclk.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_PLL4;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("SDMMC1");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_SDMMC2;
   pclk.Sdmmc2ClockSelection = RCC_SDMMC2CLKSOURCE_PLL4;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("SDMMC2");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_STGEN;
   pclk.StgenClockSelection  = RCC_STGENCLKSOURCE_HSE;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("STGEN");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
   pclk.I2c4ClockSelection   = RCC_I2C4CLKSOURCE_HSI;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("I2C4");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_ADC2;
   pclk.Adc2ClockSelection   = RCC_ADC2CLKSOURCE_PER;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("ADC2");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_I2C12;
   pclk.I2c12ClockSelection  = RCC_I2C12CLKSOURCE_HSI;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("I2C12");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_USART2;
   pclk.Usart2ClockSelection = RCC_USART2CLKSOURCE_HSI;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("USART2");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_UART4;
   pclk.Uart4ClockSelection  = RCC_UART4CLKSOURCE_HSI;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("UART4");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_SAES;
   pclk.SaesClockSelection   = RCC_SAESCLKSOURCE_ACLK;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("SAES");
   }

   pclk.PeriphClockSelection = RCC_PERIPHCLK_LPTIM3;
   pclk.Lptim3ClockSelection = RCC_LPTIM3CLKSOURCE_PCLK3;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
      error_msg("LPTIM3");
   }
}

void setup_ddr(void)
{
   // MCE and TZC config
   __HAL_RCC_MCE_CLK_ENABLE();
   __HAL_RCC_TZC_CLK_ENABLE();

   // configure TZC to allow DDR Region0 R/W non secure for all IDs
   TZC->GATE_KEEPER = 0;
   TZC->REG_ID_ACCESSO =
       0xFFFFFFFF; // Allow DDR Region0 R/W non secure for all IDs
   TZC->REG_ATTRIBUTESO = 0xC0000001;
   TZC->GATE_KEEPER |= 1U; // Enable the access in secure Mode

   // enable ETZPC & BACKUP SRAM for security
   __HAL_RCC_ETZPC_CLK_ENABLE();
   __HAL_RCC_BKPSRAM_CLK_ENABLE();

   // unlock debugger
   BSEC->BSEC_DENABLE = 0x47f;

   // enable clock debug CK_DBG
   RCC->DBGCFGR |= RCC_DBGCFGR_DBGCKEN;

   // init DDR
   static DDR_InitTypeDef hddr;
   hddr.wakeup_from_standby = false;
   hddr.self_refresh        = false;
   hddr.zdata               = 0;
   hddr.clear_bkp           = false;

   if (HAL_DDR_Init(&hddr) != HAL_OK)
      error_msg("DDR Init");
}

int32_t HAL_DDR_MspInit(ddr_type type)
{
   (void)type;
   return HAL_OK;
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
   GPIO_InitTypeDef gpio_init;
   if (huart->Instance == UART4) {
      /* Peripheral clock enable */
      __HAL_RCC_UART4_CLK_ENABLE();
      __HAL_RCC_UART4_FORCE_RESET();
      __HAL_RCC_UART4_RELEASE_RESET();

      __HAL_RCC_GPIOD_CLK_ENABLE();

      /**UART4 GPIO Configuration
        PD8     ------> UART4_TX
        PD6     ------> UART4_RX
        */
      gpio_init.Pin       = GPIO_PIN_6;
      gpio_init.Mode      = GPIO_MODE_AF_PP;
      gpio_init.Pull      = GPIO_PULLUP;
      gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
      gpio_init.Alternate = GPIO_AF8_UART4;
      HAL_GPIO_Init(GPIOD, &gpio_init);

      gpio_init.Pin       = GPIO_PIN_8;
      gpio_init.Mode      = GPIO_MODE_AF_PP;
      gpio_init.Pull      = GPIO_PULLUP;
      gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
      gpio_init.Alternate = GPIO_AF8_UART4;
      HAL_GPIO_Init(GPIOD, &gpio_init);
   }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
   if (huart->Instance == UART4) {
      __HAL_RCC_UART4_FORCE_RESET();
      __HAL_RCC_UART4_RELEASE_RESET();
      __HAL_RCC_UART4_CLK_DISABLE();

      /**UART4 GPIO Configuration
        PD8     ------> UART4_TX
        PD6     ------> UART4_RX
        */
      HAL_GPIO_DeInit(GPIOD, GPIO_PIN_6);
      HAL_GPIO_DeInit(GPIOD, GPIO_PIN_8);
   }
}

void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
   (void)hsd;

   /* Enable and reset SDMMC Periheral Clock */
   __HAL_RCC_SDMMC1_CLK_ENABLE();
   __HAL_RCC_SDMMC1_FORCE_RESET();
   __HAL_RCC_SDMMC1_RELEASE_RESET();

   /* Enable GPIOs clock */
   __HAL_RCC_GPIOC_CLK_ENABLE();
   __HAL_RCC_GPIOD_CLK_ENABLE();

   /* Common GPIO configuration */
   GPIO_InitTypeDef gpio_init;
   gpio_init.Mode  = GPIO_MODE_AF_PP;
   gpio_init.Pull  = GPIO_NOPULL;
   gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

   /* D0 D1 D2 D3 CK on PC8 PC9 PC10 PC11 PC12 - AF12 PULLUP */
   gpio_init.Pull      = GPIO_PULLUP;
   gpio_init.Alternate = GPIO_AF12_SDIO1;
   gpio_init.Pin =
       GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
   HAL_GPIO_Init(GPIOC, &gpio_init);

   /* CMD on PD2 - AF12 NOPULL since there's an external pullup */
   gpio_init.Pull      = GPIO_NOPULL;
   gpio_init.Alternate = GPIO_AF12_SDIO1;
   gpio_init.Pin       = GPIO_PIN_2;
   HAL_GPIO_Init(GPIOD, &gpio_init);
}

void MX_UART4_Init(void)
{
   huart4.Instance                    = UART4;
   huart4.Init.BaudRate               = 115200;
   huart4.Init.WordLength             = UART_WORDLENGTH_8B;
   huart4.Init.StopBits               = UART_STOPBITS_1;
   huart4.Init.Parity                 = UART_PARITY_NONE;
   huart4.Init.Mode                   = UART_MODE_TX_RX;
   huart4.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
   huart4.Init.OverSampling           = UART_OVERSAMPLING_8;
   huart4.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
   huart4.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
   huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

   if (HAL_UART_Init(&huart4) != HAL_OK) {
      error_msg("UART4");
   }
   if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) !=
       HAL_OK) {
      error_msg("FIFO TX Threshold");
   }
   if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) !=
       HAL_OK) {
      error_msg("FIFO RX Threshold");
   }
   if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK) {
      error_msg("Disable FIFO");
   }
}

void _putchar(char ch)
{
   HAL_UART_Transmit(&huart4, (uint8_t *)(&ch), 1, 0xFFFF);
}

int __io_getchar(void)
{
   uint8_t ch = 0;
   __HAL_UART_CLEAR_OREFLAG(&huart4);
   HAL_UART_Receive(&huart4, &ch, 1, 0xFFFF);
   HAL_UART_Transmit(&huart4, &ch, 1, 0xFFFF);
   return ch;
}

void setup_sd(void)
{
   // unsecure SYSRAM so that SDMMC1 (which we configure as non-secure) can
   // access it
   LL_ETZPC_SetSecureSysRamSize(ETZPC, 0);

   // unsecure SDMMC1
   LL_ETZPC_Set_SDMMC1_PeriphProtection(
       ETZPC, LL_ETZPC_PERIPH_PROTECTION_READ_WRITE_NONSECURE);

   // initialize SDMMC1
   sd_handle.Instance = SDMMC1;
   HAL_SD_DeInit(&sd_handle);

   // SDMMC IP clock xx MHz, SDCard clock xx MHz
   sd_handle.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
   sd_handle.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
   sd_handle.Init.BusWide             = SDMMC_BUS_WIDE_4B;
   sd_handle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
   sd_handle.Init.ClockDiv            = SDMMC_NSPEED_CLK_DIV;

   if (HAL_SD_Init(&sd_handle) != HAL_OK) {
      error_msg("HAL_SD_Init");
   }

   while (HAL_SD_GetCardState(&sd_handle) != HAL_SD_CARD_TRANSFER)
      ;
}

void usb_init(void)
{
   USBD_Init(&usbd_device, &MSC_Desc, 0);
   USBD_RegisterClass(&usbd_device, USBD_MSC_CLASS);
   USBD_MSC_RegisterStorage(&usbd_device, &USBD_MSC_fops);
   USBD_Start(&usbd_device);
}

// end file setup.c
