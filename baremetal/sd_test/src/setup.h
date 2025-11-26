#ifndef SETUP_H
#define SETUP_H

#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_disco.h"
#include "stm32mp13xx_disco_stpmic1.h"

// clocks and memory
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
void setup_ddr(void);

// HAL MSP
int HAL_DDR_MspInit(ddr_type type);
void HAL_UART_MspInit(UART_HandleTypeDef* huart);
void HAL_UART_MspDeInit(UART_HandleTypeDef* huart);
void HAL_SD_MspInit(SD_HandleTypeDef *hsd);

// pin mux
void MX_UART4_Init(void);

// debugging
int __io_putchar(int ch);
int __io_getchar (void);
void Error_Handler(void);

#endif // SETUP_H

// end file setup.h

