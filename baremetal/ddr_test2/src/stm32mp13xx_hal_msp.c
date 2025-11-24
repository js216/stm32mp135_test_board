#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_disco_stpmic1.h"
#include "stm32mp13xx_power.h"
#include "log.h"
#include "ddr_tests.h"
#include "ddr_tool_util.h"
#include "stm32mp_util_conf.h"

#define USARTx_TX_PIN                    UTIL_UART_TX_PIN
#define USARTx_TX_AF                     UTIL_UART_TX_AF
#define USARTx_RX_PIN                    UTIL_UART_RX_PIN
#define USARTx_RX_AF                     UTIL_UART_RX_AF

#define USARTx_CLK_ENABLE()              __HAL_RCC_UART4_CLK_ENABLE()
#define USARTx_FORCE_RESET()             __HAL_RCC_UART4_FORCE_RESET()
#define USARTx_RELEASE_RESET()           __HAL_RCC_UART4_RELEASE_RESET()

#define USARTx_TX_GPIO_PORT              GPIOD
#define USARTx_TX_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOD_CLK_ENABLE()

#define USARTx_RX_GPIO_PORT              GPIOD
#define USARTx_RX_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOD_CLK_ENABLE()

void HAL_MspInit(void)
{
  BSEC_Check_Crypto();
}

void HAL_MspDeInit(void)
{
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef  GPIO_InitStruct;

  RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit;

  /*##-1- Enable peripherals and GPIO Clocks #################################*/
  /* Enable GPIO TX/RX clock */
  USARTx_TX_GPIO_CLK_ENABLE();
  USARTx_RX_GPIO_CLK_ENABLE();

  /* Select SysClk as source of UART4 clocks */
#if (UTIL_UART_INSTANCE == UTIL_UART4)
  RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_UART4;
  RCC_PeriphClkInit.Uart4ClockSelection  = RCC_UART4CLKSOURCE_HSI;
#elif (UTIL_UART_INSTANCE == UTIL_UART5)
  RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_UART35;
  RCC_PeriphClkInit.Uart35ClockSelection = RCC_UART35CLKSOURCE_HSI;
#elif (UTIL_UART_INSTANCE == UTIL_UART7)
  RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_UART78;
  RCC_PeriphClkInit.Uart78ClockSelection = RCC_UART78CLKSOURCE_HSI;
#elif (UTIL_UART_INSTANCE == UTIL_UART8)
  RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_UART78;
  RCC_PeriphClkInit.Uart78ClockSelection = RCC_UART78CLKSOURCE_HSI;
#endif
  HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit);

  /* Enable USARTx clock */
  USARTx_CLK_ENABLE();

  /*##-2- Configure peripheral GPIO ##########################################*/
  /* UART TX GPIO pin configuration  */
  GPIO_InitStruct.Pin       = USARTx_TX_PIN;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_PULLUP;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = USARTx_TX_AF;

  HAL_GPIO_Init(USARTx_TX_GPIO_PORT, &GPIO_InitStruct);

  /* UART RX GPIO pin configuration  */
  GPIO_InitStruct.Pin = USARTx_RX_PIN;
  GPIO_InitStruct.Alternate = USARTx_RX_AF;

  HAL_GPIO_Init(USARTx_RX_GPIO_PORT, &GPIO_InitStruct);
}

/**
  * @brief UART MSP De-Initialization
  *        This function frees the hardware resources used in this example:
  *          - Disable the Peripheral's clock
  *          - Revert GPIO and NVIC configuration to their default state
  * @param huart: UART handle pointer
  * @retval None
  */
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  /*##-1- Reset peripherals ##################################################*/
  USARTx_FORCE_RESET();
  USARTx_RELEASE_RESET();

  /*##-2- Disable peripherals and GPIO Clocks ################################*/
  /* Configure UART Tx as alternate function  */
  HAL_GPIO_DeInit(USARTx_TX_GPIO_PORT, USARTx_TX_PIN);
  /* Configure UART Rx as alternate function  */
  HAL_GPIO_DeInit(USARTx_RX_GPIO_PORT, USARTx_RX_PIN);

}

#define STPMIC1_DEFAULT_START_UP_DELAY_MS  1

/**
  * @brief  This function handles System Power configuration.
  * @param  DDR type
  * @retval
  *  0: Test passed
  *  Value different from 0: Test failed
  */
int HAL_DDR_MspInit(ddr_type type)
{
#if (UTIL_USE_PMIC)
  uint16_t buck2_mv = 1350;

  switch (type) {
  case STM32MP_DDR3:
    STPMU1_Regulator_Voltage_Set(STPMU1_BUCK2, buck2_mv);

    STPMU1_Regulator_Enable(STPMU1_BUCK2);

    HAL_Delay(STPMIC1_DEFAULT_START_UP_DELAY_MS);

    STPMU1_Regulator_Enable(STPMU1_VREFDDR);

    HAL_Delay(STPMIC1_DEFAULT_START_UP_DELAY_MS);

    break;

  case STM32MP_LPDDR2_16:
  case STM32MP_LPDDR2_32:
  case STM32MP_LPDDR3_16:
  case STM32MP_LPDDR3_32:
    /* Set BUCK2 to 1.2V (16 bits) or 1.25V (32 bits) */
    switch (type)
    {
    case STM32MP_LPDDR2_32:
    case STM32MP_LPDDR3_32:
      buck2_mv = 1250;
      break;
    default:
    case STM32MP_LPDDR2_16:
    case STM32MP_LPDDR3_16:
      buck2_mv = 1200;
      break;
    }

    STPMU1_Regulator_Voltage_Set(STPMU1_BUCK2, buck2_mv);

    STPMU1_Regulator_Enable(STPMU1_BUCK2);

    HAL_Delay(STPMIC1_DEFAULT_START_UP_DELAY_MS);

    STPMU1_Regulator_Enable(STPMU1_VREFDDR);

    HAL_Delay(STPMIC1_DEFAULT_START_UP_DELAY_MS);
    break;

  default:
    break;
  }
#endif /* UTIL_USE_PMIC */

  return 0;
}
