// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file setup.c
 * @brief Driver and board low-level setup
 * @author Jakob Kastelic
 * @copyright 2025-2026 Jakob Kastelic
 */

#include "setup.h"
#include "debug.h"
#include "irq.h"
#include "irq_ctrl.h"
#include "mmu_stm32mp13xx.h"
#include "printf.h"
#include "stm32mp135fxx_ca7.h"
#include "stm32mp13xx.h"
#include "stm32mp13xx_disco_stpmic1.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_def.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_gpio_ex.h"
#include "stm32mp13xx_hal_rcc.h"
#include "stm32mp13xx_hal_rcc_ex.h"
#include "stm32mp13xx_hal_uart.h"
#include "stm32mp13xx_hal_uart_ex.h"
#include "stm32mp13xx_ll_etzpc.h"
#include <stdint.h>

UART_HandleTypeDef huart4;

/* Set to 1 once HAL_UART_Init has succeeded.  Fault handlers in
 * startup_stm32mp135fxx_ca7.c poll this flag to decide whether it is
 * safe to push a single ASCII marker char out over UART4 before
 * spinning, so we get visible evidence a fault triggered. */
volatile uint8_t uart_ready = 0;

/* No console RX -- the demo never reads from UART4. */
void UART4_IRQHandler(void)
{
}

/* IRQ table stubs.  None of these are enabled, the entries just need to
 * link cleanly. */
void EXTI5_IRQHandler(void)  {}
void EXTI12_IRQHandler(void) {}
void SDMMC1_IRQHandler(void) {}
void OTG_IRQHandler(void)    {}

void error_msg(const char *file, const int line, const char *msg)
{
   my_printf("File %s line %d: %s.\r\n", file, line, msg);
   while (1)
      ;
}

static void uart_write_char(char ch)
{
   while (!(UART4->ISR & USART_ISR_TXE))
      ;
   UART4->TDR = (uint8_t)ch;
   while (!(UART4->ISR & USART_ISR_TC))
      ;
}

void sysclk_init(void)
{
   HAL_RCC_DeInit();
   RCC_ClkInitTypeDef rcc_clkinitstructure;
   RCC_OscInitTypeDef rcc_oscinitstructure;

   rcc_oscinitstructure.OscillatorType =
       (RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSE |
        RCC_OSCILLATORTYPE_CSI | RCC_OSCILLATORTYPE_LSI);
   rcc_oscinitstructure.HSIState            = RCC_HSI_ON;
   rcc_oscinitstructure.HSEState            = RCC_HSE_ON;
   rcc_oscinitstructure.LSIState            = RCC_LSI_ON;
   rcc_oscinitstructure.CSIState            = RCC_CSI_ON;
   rcc_oscinitstructure.HSICalibrationValue = 0x00;
   rcc_oscinitstructure.CSICalibrationValue = 0x10;
   rcc_oscinitstructure.HSIDivValue         = RCC_HSI_DIV1;

   rcc_oscinitstructure.PLL.PLLState  = RCC_PLL_ON;
   rcc_oscinitstructure.PLL.PLLSource = RCC_PLL12SOURCE_HSE;
   rcc_oscinitstructure.PLL.PLLM      = 2;
   rcc_oscinitstructure.PLL.PLLN      = 83;
   rcc_oscinitstructure.PLL.PLLP      = 1;
   rcc_oscinitstructure.PLL.PLLQ      = 2;
   rcc_oscinitstructure.PLL.PLLR      = 2;
   rcc_oscinitstructure.PLL.PLLFRACV  = 0xaaa;
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
   rcc_oscinitstructure.PLL3.PLLN      = 50;
   rcc_oscinitstructure.PLL3.PLLP      = 2;
   rcc_oscinitstructure.PLL3.PLLQ      = 12;
   rcc_oscinitstructure.PLL3.PLLR      = 2;
   rcc_oscinitstructure.PLL3.PLLRGE    = RCC_PLL3IFRANGE_1;
   rcc_oscinitstructure.PLL3.PLLFRACV  = 0x1a04;
   rcc_oscinitstructure.PLL3.PLLMODE   = RCC_PLL_FRACTIONAL;

   /* PLL4 retuned to drive QSPI kernel clock at 656 MHz (P out), so
    * prescaler=3 yields the 164 MHz Fmax_QSPI per DS13483 Table 76.
    * HAL programs PLLN as N-1; the hardware effective multiplier here
    * is 82, so PLL4P is 24/3 x 82 = 656 MHz.  ETH and SDMMC are not
    * used by this demo, so the prior PLL4 = 47/23.5 MHz settings are
    * not load-bearing.  Override with -DPLL4_PLLN=<N> at build time
    * for per-byte overhead clock-pacing experiments. */
#ifndef PLL4_PLLN
#define PLL4_PLLN 82
#endif
   rcc_oscinitstructure.PLL4.PLLState  = RCC_PLL_ON;
   rcc_oscinitstructure.PLL4.PLLSource = RCC_PLL4SOURCE_HSE;
   rcc_oscinitstructure.PLL4.PLLM      = 3;
   rcc_oscinitstructure.PLL4.PLLN      = PLL4_PLLN;
   rcc_oscinitstructure.PLL4.PLLP      = 1;
   rcc_oscinitstructure.PLL4.PLLQ      = 1;
   rcc_oscinitstructure.PLL4.PLLR      = 1;
   rcc_oscinitstructure.PLL4.PLLRGE    = RCC_PLL4IFRANGE_1;
   rcc_oscinitstructure.PLL4.PLLFRACV  = 0;
   rcc_oscinitstructure.PLL4.PLLMODE   = RCC_PLL_INTEGER;

   SET_BIT(PWR->CR1, PWR_CR1_DBP);

   if (HAL_RCC_OscConfig(&rcc_oscinitstructure) != HAL_OK)
      ERROR("HAL RCC Osc configuration error");

   rcc_clkinitstructure.ClockType =
       (RCC_CLOCKTYPE_MPU | RCC_CLOCKTYPE_ACLK | RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5 | RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK6 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3);
   rcc_clkinitstructure.MPUInit.MPU_Clock     = RCC_MPUSOURCE_PLL1;
   rcc_clkinitstructure.MPUInit.MPU_Div       = RCC_MPU_DIV2;
   rcc_clkinitstructure.AXISSInit.AXI_Clock   = RCC_AXISSOURCE_PLL2;
#ifndef AXI_DIV
#define AXI_DIV RCC_AXI_DIV1
#endif
   rcc_clkinitstructure.AXISSInit.AXI_Div     = AXI_DIV;
   rcc_clkinitstructure.MLAHBInit.MLAHB_Clock = RCC_MLAHBSSOURCE_PLL3;
#ifndef MLAHB_DIV
#define MLAHB_DIV RCC_MLAHB_DIV1
#endif
   rcc_clkinitstructure.MLAHBInit.MLAHB_Div   = MLAHB_DIV;
   rcc_clkinitstructure.APB1_Div              = RCC_APB1_DIV2;
   rcc_clkinitstructure.APB2_Div              = RCC_APB2_DIV2;
   rcc_clkinitstructure.APB3_Div              = RCC_APB3_DIV2;
   rcc_clkinitstructure.APB4_Div              = RCC_APB4_DIV2;
   rcc_clkinitstructure.APB5_Div              = RCC_APB5_DIV4;
   rcc_clkinitstructure.APB6_Div              = RCC_APB6_DIV2;

   RCC->SECCFGR = 0x00000000;

   if (HAL_RCC_ClockConfig(&rcc_clkinitstructure) != HAL_OK)
      ERROR("HAL RCC Clk configuration error");
}

void pmic_init(void)
{
   if (BSP_PMIC_Init())
      return;
   BSP_PMIC_InitRegulators();
   STPMU1_Regulator_Voltage_Set(STPMU1_BUCK2, 1350);
   STPMU1_Regulator_Enable(STPMU1_BUCK2);
   HAL_Delay(1);
   STPMU1_Regulator_Enable(STPMU1_VREFDDR);
   HAL_Delay(1);
}

void perclk_init(void)
{
   RCC_PeriphCLKInitTypeDef pclk = {0};

   pclk.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
   pclk.CkperClockSelection  = RCC_CKPERCLKSOURCE_HSE;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
      ERROR("CKPER");

   pclk.PeriphClockSelection = RCC_PERIPHCLK_STGEN;
   pclk.StgenClockSelection  = RCC_STGENCLKSOURCE_HSE;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
      ERROR("STGEN");

   pclk.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
   pclk.I2c4ClockSelection   = RCC_I2C4CLKSOURCE_HSI;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
      ERROR("I2C4");

   pclk.PeriphClockSelection = RCC_PERIPHCLK_UART4;
   pclk.Uart4ClockSelection  = RCC_UART4CLKSOURCE_HSI;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
      ERROR("UART4");

   /* QSPI ker_ck = PLL4P = 656 MHz (see PLL4 retune in sysclk_init).
    * Default after reset is ACLK (= 180 MHz on this PLL), which only
    * lets us reach 90 MHz at prescaler=1 -- short of the 166 MHz
    * Fmax_QSPI spec.  Routing via PLL4 instead lifts the ceiling. */
   pclk.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
   pclk.QspiClockSelection   = RCC_QSPICLKSOURCE_PLL4;
   if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
      ERROR("QSPI");
}

void etzpc_init(void)
{
   __HAL_RCC_ETZPC_CLK_ENABLE();
   LL_ETZPC_SetSecureSysRamSize(ETZPC, 0);
   LL_ETZPC_Set_All_PeriphProtection(
       ETZPC, LL_ETZPC_PERIPH_PROTECTION_READ_WRITE_NONSECURE);
}

void gpio_init(void)
{
   __HAL_RCC_GPIOA_CLK_ENABLE();
   __HAL_RCC_GPIOB_CLK_ENABLE();
   __HAL_RCC_GPIOC_CLK_ENABLE();
   __HAL_RCC_GPIOD_CLK_ENABLE();
   __HAL_RCC_GPIOE_CLK_ENABLE();
   __HAL_RCC_GPIOF_CLK_ENABLE();
   __HAL_RCC_GPIOG_CLK_ENABLE();
   __HAL_RCC_GPIOH_CLK_ENABLE();
   __HAL_RCC_GPIOI_CLK_ENABLE();

   GPIOA->SECCFGR = 0;
   GPIOB->SECCFGR = 0;
   GPIOC->SECCFGR = 0;
   GPIOD->SECCFGR = 0;
   GPIOE->SECCFGR = 0;
   GPIOF->SECCFGR = 0;
   GPIOG->SECCFGR = 0;
   GPIOH->SECCFGR = 0;
   GPIOI->SECCFGR = 0;
}

void uart4_init(void)
{
   __HAL_RCC_UART4_CLK_ENABLE();
   __HAL_RCC_UART4_FORCE_RESET();
   __HAL_RCC_UART4_RELEASE_RESET();
   __HAL_RCC_GPIOD_CLK_ENABLE();

   GPIO_InitTypeDef g;
   g.Pin       = GPIO_PIN_6;
   g.Mode      = GPIO_MODE_AF_PP;
   g.Pull      = GPIO_PULLUP;
   g.Speed     = GPIO_SPEED_FREQ_HIGH;
   g.Alternate = GPIO_AF8_UART4;
   HAL_GPIO_Init(GPIOD, &g);

   g.Pin = GPIO_PIN_8;
   HAL_GPIO_Init(GPIOD, &g);

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

   if (HAL_UART_Init(&huart4) != HAL_OK)
      ERROR("UART4");
   if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) !=
       HAL_OK)
      ERROR("FIFO TX Threshold");
   if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) !=
       HAL_OK)
      ERROR("FIFO RX Threshold");
   if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK)
      ERROR("Disable FIFO");

   printf_set_output(uart_write_char);
   uart_ready = 1;
}

void gic_init(void)
{
   GICDistributor->CTLR |= 0x03U;
   GICInterface->CTLR |= 0x03U;
   GICInterface->PMR = 0xFFU;
}

void mmu_init(void)
{
#ifdef MMU_USE
   MMU_CreateTranslationTable();
   MMU_Enable();
#endif
}
