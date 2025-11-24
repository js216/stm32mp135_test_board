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

static GPIO_TypeDef* LED_PORT[LEDn] = {LED3_GPIO_PORT,
                                       LED4_GPIO_PORT,
                                      };

static const uint16_t LED_PIN[LEDn] = {LED3_PIN,
                                       LED4_PIN,
                                      };





int32_t BSP_LED_DeInit(Led_TypeDef Led)
{
  int32_t  status = BSP_ERROR_NONE;
  GPIO_InitTypeDef  gpio_init_structure;
  /* Turn off LED */
  BSP_LED_Off(Led);
    /* Configure the GPIO_LED pin */
  gpio_init_structure.Pin = LED_PIN[Led];
  BSP_ENTER_CRITICAL_SECTION(LED_PORT[Led]);
  HAL_GPIO_DeInit(LED_PORT[Led], gpio_init_structure.Pin);
  BSP_EXIT_CRITICAL_SECTION(LED_PORT[Led]);

return status;
}

int32_t BSP_LED_On(Led_TypeDef Led)
{
  int32_t  status = BSP_ERROR_NONE;
  if((Led == LED3) || (Led == LED4))
  {
    HAL_GPIO_WritePin(LED_PORT[Led], LED_PIN[Led], GPIO_PIN_RESET);
  }
  return status;
}

int32_t BSP_LED_Off(Led_TypeDef Led)
{
  int32_t  status = BSP_ERROR_NONE;
  if((Led == LED3) || (Led == LED4))
  {
    HAL_GPIO_WritePin(LED_PORT[Led], LED_PIN[Led], GPIO_PIN_SET);
  }
  return status;
}

int32_t BSP_LED_Toggle(Led_TypeDef Led)
{
  int32_t  status = BSP_ERROR_NONE;
  /* Toggle GPIO Led Pin */
  if((Led == LED3) || (Led == LED4))
  {
    HAL_GPIO_TogglePin(LED_PORT[Led], LED_PIN[Led]);
  }
  return status;
}

int32_t BSP_LED_GetState(Led_TypeDef Led)
{
  int32_t status;

  status = (HAL_GPIO_ReadPin(LED_PORT[Led], LED_PIN[Led]) == GPIO_PIN_RESET) ? 1 : 0;

  return status;
}
