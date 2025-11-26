#ifndef __STM32MP13XX_DISCO_CONFIG_H
#define __STM32MP13XX_DISCO_CONFIG_H

#include "stm32mp13xx_hal.h"

/* Activation of PMIC */
#define USE_STPMIC1x                        1U

/* Activation of Critical Section */
#define BSP_ENTER_CRITICAL_SECTION(periph)  ((void)0)
#define BSP_EXIT_CRITICAL_SECTION(periph)   ((void)0)

/* IRQ priorities */
#define BSP_PMU1_IT_PRIORITY                0x03UL
#define BSP_BUTTON_WAKEUP_IT_PRIORITY       0x0FUL
#define BSP_BUTTON_USER_IT_PRIORITY         0x0FUL
#define BSP_BUTTON_USER2_IT_PRIORITY        0x0FUL
#define BSP_BUTTON_TAMPER_IT_PRIORITY       0x0FUL

#endif
