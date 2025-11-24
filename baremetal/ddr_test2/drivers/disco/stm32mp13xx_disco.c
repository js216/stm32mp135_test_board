#include "stm32mp13xx_disco.h"
#include "stdio.h"

typedef void (* BSP_EXTI_LineCallback)(void);

#define STM32MP13XX_DISCO_BSP_VERSION_MAIN   (0x01U) /*!< [31:24] main version */
#define STM32MP13XX_DISCO_BSP_VERSION_SUB1   (0x02U) /*!< [23:16] sub1 version */
#define STM32MP13XX_DISCO_BSP_VERSION_SUB2   (0x00U) /*!< [15:8]  sub2 version */
#define STM32MP13XX_DISCO_BSP_VERSION_RC     (0x00U) /*!< [7:0]  release candidate */
#define STM32MP13XX_DISCO_BSP_VERSION         ((STM32MP13XX_DISCO_BSP_VERSION_MAIN << 24)\
                                             |(STM32MP13XX_DISCO_BSP_VERSION_SUB1 << 16)\
                                             |(STM32MP13XX_DISCO_BSP_VERSION_SUB2 << 8 )\
                                             |(STM32MP13XX_DISCO_BSP_VERSION_RC))



