#ifndef __STM32MP13xx_HAL_CONF_H
#define __STM32MP13xx_HAL_CONF_H

#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_rcc.h"
#include "stm32mp13xx_hal_uart.h"

#define HSE_VALUE            ((uint32_t)24000000U) /*!< Value of the External oscillator in Hz */
#define HSE_STARTUP_TIMEOUT  ((uint32_t)100U)   /*!< Time out for HSE start up, in ms */
#define HSI_VALUE            ((uint32_t)64000000U) /*!< Value of the Internal oscillator in Hz*/
#define LSI_VALUE            32000U
#define LSI_STARTUP_TIME     130UL      /*!< Time out for LSI start up, in ms */
#define LSE_VALUE            ((uint32_t)32768U) /*!< Value of the External oscillator in Hz*/
#define LSE_STARTUP_TIMEOUT  ((uint32_t)5000U)   /*!< Time out for LSE start up, in ms */
#define CSI_VALUE            4000000U /*!< Value of the Internal oscillator in Hz*/
#define EXTERNAL_CLOCK_VALUE 12288000U /*!< Value of the External clock in Hz*/
#define TICK_INT_PRIORITY    0x0FU /*!< tick interrupt priority */

#define assert_param(expr) ((void)0)

#endif /* __STM32MP13xx_HAL_CONF_H */
