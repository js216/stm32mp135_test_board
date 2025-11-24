#include "stm32mp13xx_hal.h"
#include "stm32mp_util_conf.h"

/* Activation of PMIC */
#define USE_STPMIC1x                        UTIL_USE_PMIC

#define BSP_ENTER_CRITICAL_SECTION(periph)       ((void)0)
#define BSP_EXIT_CRITICAL_SECTION(periph)        ((void)0)

/* IRQ priorities */
#define BSP_PMU1_IT_PRIORITY                   0x03UL
#define BSP_BUTTON_WAKEUP_IT_PRIORITY          0x0FUL
#define BSP_BUTTON_USER_IT_PRIORITY            0x0FUL
#define BSP_BUTTON_USER2_IT_PRIORITY           0x0FUL
#define BSP_BUTTON_TAMPER_IT_PRIORITY          0x0FUL

/* LCD FB_StartAddress (DDR) */
#define LCD_FB_START_ADDRESS                  ((uint32_t)0xD0000000)
