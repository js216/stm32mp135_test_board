/**
  ******************************************************************************
  * @file    stm32mp257f_eval_stpmic.h
  * @author  MCD Application Team
  * @brief   stpmu driver functions used for ST internal validation
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef STM32MP13XX_DISCO_STPMIC1L_H
#define STM32MP13XX_DISCO_STPMIC1L_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32mp13xx_hal.h"
#include "stpmic1l.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup STM32MP257F_EV1
  * @{
  */

/** @addtogroup STPMIC
  * @{
  */

/** @defgroup STPMIC_Exported_Types Exported Types
  * @{
  */
typedef enum
{
  VDDCORE = 0,
  VDD_DDR,
  VDD,
  VTT_DDR,
  VDD3V3_USB,
  VDD_SD,
  EN1,
  EN2
} board_regul_t;

typedef struct
{
  char                 boardRegulName[15]; /* board regulator name */
  board_regul_t        boardRegulId;  /* board regulator Id */
  PMIC_RegulId_TypeDef pmicRegul;  /* pmic regulator */
  uint8_t              control_reg1;
  uint8_t              control_reg2;
  uint8_t              alt_control_reg1;
  uint8_t              alt_control_reg2;
  uint8_t              pwr_control_reg;
  uint32_t             regulMin; /* regulator-min-mvolt */
  uint32_t             regulMax; /* regulator-max-mvolt */
} board_regul_struct_t;

/* Exported types ------------------------------------------------------------*/
/**
  * @}
  */

/** @defgroup STPMIC_Exported_Constants Exported Constants
  * @{
  */
/* Board Configuration ------------------------------------------------------------*/
/* Definition of PMIC <=> stm32mp2 Signals */
#define PMIC_INTN_PIN               GPIO_PIN_8
#define PMIC_INTN_PORT              GPIOF
#define PMIC_INTN_CLK_ENABLE()      __HAL_RCC_GPIOF_CLK_ENABLE()
#define PMIC_INTN_CLK_DISABLE()     __HAL_RCC_GPIOF_CLK_DISABLE()

#ifdef USE_WAKEUP_PIN
#define PMIC_INTN_EXTI_IRQ          EXTI8_IRQn   /* CPU1_WAKEUP_PIN_IRQn */
#define BSP_PMIC_INTN_IRQHANDLER    HAL_PWREx_WKUP1_Callback
#else
#define PMIC_INTN_EXTI_IRQ          EXTI8_IRQn   /* CPU1_WAKEUP_PIN_IRQn */
#define BSP_PMIC_INTN_IRQHANDLER    EXTI8_IRQHandler
#endif /* USE_WAKEUP_PIN */

#define BSP_PMIC_PWRCTRL_PIN_ASSERT()   HAL_GPIO_WritePin(PMIC_PWRCTRL_PORT, \
                                                          PMIC_PWRCTRL_PIN, GPIO_PIN_RESET);
#define BSP_PMIC_PWRCTRL_PIN_PULL()     HAL_GPIO_WritePin(PMIC_PWRCTRL_PORT, \
                                                          PMIC_PWRCTRL_PIN, GPIO_PIN_SET);

/**
  * @brief  STPMIC1L product ID
  */
#define  STPMIC1L_ID           0x10U /* Default PMIC product ID: 0x2X (X depends on PMIC variant) */


/**
  * @brief  STPMIC1L LOW POWER MODES
  */

#define  STPMIC1L_LP_STOP              0x0U /*  */
#define  STPMIC1L_LPLV_STOP            0x1U /*  */
#define  STPMIC1L_STANDBY_DDR_SR       0x2U /*  */
#define  STPMIC1L_STANDBY_DDR_OFF      0x3U /*  */


/**
  * @}
  */

/** @defgroup STPMIC_Private_Defines Private Defines
  * @{
  */
/* Private typedef -----------------------------------------------------------*/


/* Private define ------------------------------------------------------------*/
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/**
  * @}
  */

/** @defgroup STPMIC_Private_Variables Private Variables
  * @{
  */

/* Exported constants --------------------------------------------------------*/

/**
  * @}
  */

/** @defgroup STPMIC_Exported_Functions Exported Functions
  * @{
  */

/* Exported functions --------------------------------------------------------*/
uint32_t BSP_PMIC_Init(void);
uint32_t BSP_PMIC_DeInit(void);
uint32_t BSP_PMIC_DDR_Power_Init();
uint32_t BSP_PMIC_DumpRegs(void);
uint32_t BSP_PMIC_Power_Mode_Init(void);
uint32_t BSP_PMIC_DDR_Power_Off(void);
uint32_t BSP_PMIC_REGU_Set_Off(board_regul_t regu);
uint32_t BSP_PMIC_REGU_Set_On(board_regul_t regu);
uint32_t BSP_PMIC_Set_Power_Mode(uint32_t mode);
void BSP_PMIC_INTn_Callback(PMIC_IRQn IRQn);
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif /* STM32MP13XX_DISCO_STPMIC1L_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
