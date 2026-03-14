/**
  ******************************************************************************
  * @file    stm32mp257f_eval_stpmic.c
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

/* Includes ----------------------------------------------------------------------*/
#include "stm32mp13xx_disco.h"
#include "stm32mp13xx_disco_bus.h"
#include "stm32mp13xx_disco_stpmic1l.h"
#include <string.h>
#include <stdio.h>

/** @addtogroup BSP
  * @{
  */

/** @addtogroup STM32MP257F_EV1
  * @{
  */

/** @addtogroup STPMIC
  * @{
  */


/** @defgroup STPMIC_Private_Constants Private Constants
  * @{
  */
/* Driver for PMIC ---------------------------------------------------------------*/

/* Board Configuration ------------------------------------------------------------*/
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

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* I2C handler declaration */
I2C_HandleTypeDef I2cHandle;
extern I2C_HandleTypeDef hI2c4;

static STPMIC1L_Drv_t *Stpmic2lDrv = NULL;
static void         *CompObj = NULL;


/*
  Table of EVAL board regulators
*/

board_regul_struct_t board_regulators_table[] =
{
  {
    "VDDCORE",     VDDCORE,     STPMIC_BUCK1,  BUCK1_MAIN_CR1,  BUCK1_MAIN_CR2,  \
    BUCK1_ALT_CR1,  BUCK1_ALT_CR2,  BUCK1_PWRCTRL_CR,  900,  1250
  },
  {
    "VDD_DDR",      VDD_DDR,    STPMIC_BUCK2,  BUCK2_MAIN_CR1,  BUCK2_MAIN_CR2,  \
    BUCK2_ALT_CR1,  BUCK2_ALT_CR2,  BUCK2_PWRCTRL_CR, 1350, 1350
  },
  {
    "VDD",          VDD,  STPMIC_LDO2,   LDO2_MAIN_CR,    LDO2_MAIN_CR,    \
    LDO2_ALT_CR,    LDO2_ALT_CR,    LDO2_PWRCTRL_CR,  3300, 3300
  },
  {
    "VTT_DDR",     VTT_DDR,     STPMIC_LDO3,   LDO3_MAIN_CR,    LDO3_MAIN_CR,    \
    LDO3_ALT_CR,    LDO3_ALT_CR,    LDO3_PWRCTRL_CR,  600, 600
  },
  {
    "VDD3V3_USB",  VDD3V3_USB,  STPMIC_LDO4,   LDO4_MAIN_CR,    LDO4_MAIN_CR,    \
    LDO4_ALT_CR,    LDO4_ALT_CR,    LDO4_PWRCTRL_CR,   3300, 3300
  },
  {
    "VDD_SD",    VDD_SD,    STPMIC_LDO5,   LDO5_MAIN_CR,    LDO5_MAIN_CR,    \
    LDO5_ALT_CR,    LDO5_ALT_CR,    LDO5_PWRCTRL_CR,   3300, 3300
  },
    {
    "EN1",    EN1,    STPMIC_GPO1,   GPO1_MAIN_CR,    GPO1_MAIN_CR,    \
    GPO1_ALT_CR,    GPO1_ALT_CR,    GPO1_PWRCTRL_CR,   0, 0
  },
    {
    "EN2",    EN2,    STPMIC_GPO2,   GPO2_MAIN_CR,    GPO2_MAIN_CR,    \
    GPO2_ALT_CR,    GPO2_ALT_CR,    GPO2_PWRCTRL_CR,   0, 0
  },
};


/**
  * @}
 */

/** @defgroup STPMIC_Private_Functions Private Functionss
  * @{
  */

/* Private function prototypes -----------------------------------------------*/

STPMIC1L_Object_t   STPMIC1LObj = { 0 };
static int32_t STPMIC_Probe();
void STPMIC_Enable_Interrupt(PMIC_IRQn IRQn);
void STPMIC_Disable_Interrupt(PMIC_IRQn IRQn);
void STPMIC_INTn_Callback(PMIC_IRQn IRQn);

/* Private functions ---------------------------------------------------------*/


static int32_t STPMIC_Probe()
{
  int32_t ret = STPMIC1L_OK;
  STPMIC1L_IO_t       IOCtx;
  uint8_t                id;

  /* Only perform the init if the object already exist */
  if (!CompObj)
  {
    /* Configure the I2C driver */
    IOCtx.Address     = STPMIC_I2C_ADDRESS;
    IOCtx.Init        = BSP_I2C4_Init;
    IOCtx.DeInit      = BSP_I2C4_DeInit;
    IOCtx.ReadReg     = BSP_I2C4_ReadReg;
    IOCtx.WriteReg    = BSP_I2C4_WriteReg;
    IOCtx.GetTick     = BSP_GetTick;

    ret = STPMIC1L_RegisterBusIO(&STPMIC1LObj, &IOCtx);
    if (ret != STPMIC1L_OK)
    {
      return BSP_ERROR_COMPONENT_FAILURE;
    }

    ret = STPMIC1L_ReadID(&STPMIC1LObj, &id);
    if (ret != STPMIC1L_OK)
    {
      return BSP_ERROR_COMPONENT_FAILURE;
    }

    if ((id & 0xF0) != STPMIC1L_ID)
    {
      return BSP_ERROR_UNKNOWN_COMPONENT;
    }

    Stpmic2lDrv = (STPMIC1L_Drv_t *) &STPMIC1L_Driver;
    CompObj = &STPMIC1LObj;
  }

  ret = Stpmic2lDrv->Init(CompObj);
  if (ret != STPMIC1L_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  return BSP_ERROR_NONE;
}

uint32_t BSP_PMIC_Init(void)
{
  int32_t status = BSP_ERROR_NONE;

  status = STPMIC_Probe();
  if (status != BSP_ERROR_NONE)
  {
    return status;
  }

#ifdef USE_WAKEUP_PIN

#else
  GPIO_InitTypeDef  GPIO_InitStruct;

  /* INTn - Interrupt Line - Active Low (Falling Edge) */
  PMIC_INTN_CLK_ENABLE();

  GPIO_InitStruct.Pin       = PMIC_INTN_PIN;
  GPIO_InitStruct.Mode      = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull      = GPIO_PULLUP; /* GPIO_NOPULL */
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = 0 ;
  HAL_GPIO_Init(PMIC_INTN_PORT, &GPIO_InitStruct);

  /* Enable and set INTn EXTI Interrupt  */
#if defined (CORE_CM33) || defined (CORE_CM0PLUS)
  HAL_NVIC_SetPriority(PMIC_INTN_EXTI_IRQ, 0, 0);
  HAL_NVIC_EnableIRQ(PMIC_INTN_EXTI_IRQ);
#endif /* defined (CORE_CM33) || defined (CORE_CM0PLUS) */
#endif /* USE_WAKEUP_PIN */

  STPMIC_Enable_Interrupt(STPMIC1L_IT_PONKEY_F);
  STPMIC_Enable_Interrupt(STPMIC1L_IT_PONKEY_R);

  return BSP_ERROR_NONE;
}

uint32_t BSP_PMIC_DeInit(void)
{
  uint32_t  status = BSP_ERROR_NONE;

  status = Stpmic2lDrv->DeInit(CompObj);
  if (status != STPMIC1L_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  return status;
}

uint32_t BSP_PMIC_ReadReg(uint8_t reg, uint8_t *pdata)
{
  int32_t  status = BSP_ERROR_NONE;

  status = Stpmic2lDrv->ReadReg(CompObj, reg, pdata);
  if (status != STPMIC1L_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }
  return status;
}

uint32_t BSP_PMIC_WriteReg(uint8_t reg, uint8_t data)
{
  int32_t  status = BSP_ERROR_NONE;

  status = Stpmic2lDrv->WriteReg(CompObj, reg, data);
  if (status != STPMIC1L_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }
  return status;
}

uint32_t BSP_PMIC_UpdateReg(uint8_t reg, uint8_t mask)
{
  int32_t  status = BSP_ERROR_NONE;

  status = Stpmic2lDrv->UpdateReg(CompObj, reg, mask);
  if (status != STPMIC1L_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }
  return status;
}

/*
 *
 * PMIC registers dump
 *
 */
/* following are configurations */
uint32_t BSP_PMIC_DumpRegs(void)
{
  uint32_t  status = BSP_ERROR_NONE;

  status = Stpmic2lDrv->DumpRegs(CompObj);
  if (status != STPMIC1L_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  return status;
}

/*
 *
 * @brief BSP_PMIC_DDR_Power_Init initialize DDR power
 *
 * DDR power on sequence is:
 * enable VTT_DDR
 * enable VDD_DDR
 *
 * @param  None
 * @retval status
 *
 */
/* following are configurations */
uint32_t BSP_PMIC_DDR_Power_Init()
{
  uint32_t  status = BSP_ERROR_NONE;

  /* VTT_DDR ==> LDO3 ==> 600mV */
  if (BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].control_reg1, MAIN_CR_SNK_SRC) != BSP_ERROR_NONE)
  {
    return BSP_ERROR_PMIC;
  }

  /* enable VTT_DDR */
  status = BSP_PMIC_REGU_Set_On(VTT_DDR);
  if (status != BSP_ERROR_NONE)
  {
    return BSP_ERROR_PMIC;
  }

  /* VDD_DDR ==> BUCK2 ==> 1350mV */
  if (BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].control_reg1, 0x55) != BSP_ERROR_NONE)
  {
    return BSP_ERROR_PMIC;
  }

  return BSP_PMIC_REGU_Set_On(VDD_DDR);
}

uint32_t BSP_PMIC_REGU_Set_On(board_regul_t regu)
{
  uint8_t data;

  switch (regu)
  {
    case VDD:
    case VTT_DDR:
    case VDD3V3_USB:
    case VDD_SD:
    case EN1:
    case EN2:
      /* set enable bit */
      if (BSP_PMIC_UpdateReg(board_regulators_table[regu].control_reg1, MAIN_CR_EN) != BSP_ERROR_NONE)
      {
        return BSP_ERROR_PMIC;
      }
      break;
    case VDDCORE:
    case VDD_DDR:
      /* set enable bit */
      if (BSP_PMIC_UpdateReg(board_regulators_table[regu].control_reg2, MAIN_CR_EN) != BSP_ERROR_NONE)
      {
        return BSP_ERROR_PMIC;
      }
      break;
    default:
      return BSP_ERROR_WRONG_PARAM ;
      break;
  }

  /* Default ramp delay of 1ms */
  HAL_Delay(1);

  return BSP_ERROR_NONE;
}

uint32_t BSP_PMIC_REGU_Set_Off(board_regul_t regu)
{
  uint8_t data;

  switch (regu)
  {
    case VDD:
    case VTT_DDR:
    case VDD3V3_USB:
    case VDD_SD:
    case EN1:
    case EN2:
      /* disable ==> clear enable bit */
      if (BSP_PMIC_ReadReg(board_regulators_table[regu].control_reg1,
                           &data) != BSP_ERROR_NONE) /* read control reg to save data */
      {
        return BSP_ERROR_PMIC;
      }

      data &= ~MAIN_CR_EN; /* clear enable bit */
      if (BSP_PMIC_WriteReg(board_regulators_table[regu].control_reg1,
                            data) != BSP_ERROR_NONE) /* write control reg to clear enable bit */
      {
        return BSP_ERROR_PMIC;
      }
      break;

    case VDDCORE:
    case VDD_DDR:
      /* disable ==> clear enable bit */
      if (BSP_PMIC_ReadReg(board_regulators_table[regu].control_reg2,
                           &data) != BSP_ERROR_NONE) /* read control reg to save data */
      {
        return BSP_ERROR_PMIC;
      }

      data &= ~MAIN_CR_EN; /* clear enable bit */
      if (BSP_PMIC_WriteReg(board_regulators_table[regu].control_reg2,
                            data) != BSP_ERROR_NONE) /* write control reg to clear enable bit */
      {
        return BSP_ERROR_PMIC;
      }
      break;
    default:
      return BSP_ERROR_WRONG_PARAM ;
      break;
  }

  /* Default ramp delay of 1ms */
  HAL_Delay(1);

  return BSP_ERROR_NONE;
}

uint32_t BSP_PMIC_DDR_Power_Off()
{
  uint32_t  status = BSP_ERROR_NONE;

  /* disable VTT_DDR */
  status = BSP_PMIC_REGU_Set_Off(VTT_DDR);
  if (status != BSP_ERROR_NONE)
  {
    return BSP_ERROR_PMIC;
  }

  /* disable VDD_DDR */
  return BSP_PMIC_REGU_Set_Off(VDD_DDR);
}

/**
  * @brief  BSP_PMIC_Set_Power_Mode Set PMIC run/low power mode
  * @param  mode  mode to set
  * @retval status
  */

uint32_t BSP_PMIC_Power_Mode_Init()
{
  uint32_t  status = BSP_ERROR_NONE;
  uint8_t TEMP;
  /* Fail-safe overcurrent protection */
  status = BSP_PMIC_WriteReg(FS_OCP_CR1, FS_OCP_BUCK2 | FS_OCP_BUCK1);
  status = BSP_PMIC_WriteReg(FS_OCP_CR2, FS_OCP_LDO3 | FS_OCP_LDO2);


  /* Mask reset LDO control register (LDOS_MRST_CR) for VDD (LDO2) */
  status = BSP_PMIC_WriteReg(LDOS_MRST_CR, LDO2_MRST);


  /* VDDCORE: BUCK1, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=1.25, State=ON_AUTO */
  status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].control_reg1, 0x4B);
  status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].control_reg2, \
                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);
  /* Add for multiple standby issue */
  status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].pwr_control_reg, \
                             PWRCTRL_CR_SEL1 | PWRCTRL_CR_DLY_H0 | PWRCTRL_CR_DLY_L0 | PWRCTRL_CR_EN);

  BSP_PMIC_ReadReg(BUCK1_PWRCTRL_CR,&TEMP);

  /* VDD_DDR: BUCK2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=1.35, State=ON_AUTO */
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].control_reg1, 0x55);
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].control_reg2, \
                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].pwr_control_reg, \
                             PWRCTRL_CR_SEL1 | PWRCTRL_CR_DLY_H0 | PWRCTRL_CR_DLY_L0 | PWRCTRL_CR_EN);

  /* VDD: LDO2, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
  PWRCTRL_RST=0, Msk_Rst=1, Source=NA, V=3.3, State=ON */
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD].control_reg1, \
                             (0x18 << 1) | MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD].pwr_control_reg, 0x0);

  /* VTT_DDR: LDO3, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=SINK_SOURCE, State=ON */
  status = BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].control_reg1, \
                             MAIN_CR_SNK_SRC | MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].pwr_control_reg, \
                             PWRCTRL_CR_SEL1 | PWRCTRL_CR_DLY_H0 | PWRCTRL_CR_DLY_L0 | PWRCTRL_CR_EN);

  /* VDD3V3_USB: LDO4, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD3V3_USB].control_reg1, \
                             MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD3V3_USB].pwr_control_reg, 0x0);

  /* VDD_SD: LDO5, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=3.3, State=ON */
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD_SD].control_reg1, \
                             (0x18 << 1) |  MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[VDD_SD].pwr_control_reg, \
                             PWRCTRL_CR_SEL1 | PWRCTRL_CR_DLY_H0 | PWRCTRL_CR_DLY_L0 | PWRCTRL_CR_EN);

  /* EN1: GPO1, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
  status = BSP_PMIC_WriteReg(board_regulators_table[EN1].control_reg1, MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[EN1].pwr_control_reg, 0x00);

  /* EN2: GPO2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
  PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
  status = BSP_PMIC_WriteReg(board_regulators_table[EN2].control_reg1, MAIN_CR_EN);
  status = BSP_PMIC_WriteReg(board_regulators_table[EN2].pwr_control_reg, \
                             PWRCTRL_CR_SEL1 | PWRCTRL_CR_DLY_H0 | PWRCTRL_CR_DLY_L0 | PWRCTRL_CR_EN);

  return status;
}

uint32_t BSP_PMIC_Set_Power_Mode(uint32_t mode)
{
  uint32_t  status = BSP_ERROR_NONE;
  uint8_t data,TEMP;

  switch (mode)
  {

    case STPMIC1L_LP_STOP:

    /* VDDCORE: BUCK1, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=1.25, State=ON_AUTO */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg1, 0x4B); 
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg2, \
                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);

    /* VDD_DDR: BUCK2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=1.35, State=ON_AUTO */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg1, 0x55);
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg2, \
                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);

    /* VDD: LDO2, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=1, Source=NA, V=3.3, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD].control_reg1, \
                             (0x18 << 1) | MAIN_CR_EN);

    /* VTT_DDR: LDO3, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].alt_control_reg1, 0x0);

    /* VDD3V3_USB: LDO4, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD3V3_USB].control_reg1, \
                             MAIN_CR_EN);

    /* VDD_SD: LDO5, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=3.3, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_SD].alt_control_reg1, \
                             (0x18 << 1) |  MAIN_CR_EN);

    /* EN1: GPO1, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN1].control_reg1, MAIN_CR_EN);


    /* EN2: GPO2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN2].alt_control_reg1, MAIN_CR_EN);


    /* STANDBY_PWRCTRL_SEL[1:0] = 0 */
    status = BSP_PMIC_ReadReg(MAIN_CR, &data); /* read MAIN_CR to save data */
    data &= ~STANDBY_PWRCTRL_SEL_3; /* clear STANDBY_PWRCTRL_SEL bits */
    status = BSP_PMIC_WriteReg(MAIN_CR, data); /* write MAIN_CR to set STANDBY_PWRCTRL_SEL[1:0] = 0 */

    break;


    case STPMIC1L_LPLV_STOP:
    /* VDDCORE: BUCK1, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=0.9, State=ON_AUTO */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg1, 0x28);
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg2, \
    	                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);


    /* VDD_DDR: BUCK2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=1.35, State=ON_AUTO */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg1, 0x55);
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg2, \
                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);
    /* VDD: LDO2, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=1, Source=NA, V=3.3, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD].control_reg1, \
                             (0x18 << 1) | MAIN_CR_EN);

    /* VTT_DDR: LDO3, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].alt_control_reg1, 0x0);

    /* VDD3V3_USB: LDO4, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD3V3_USB].control_reg1, \
                             MAIN_CR_EN);

    /* VDD_SD: LDO5, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=3.3, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_SD].alt_control_reg1, \
                             (0x18 << 1) |  MAIN_CR_EN);

    /* EN1: GPO1, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN1].control_reg1, MAIN_CR_EN);


    /* EN2: GPO2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN2].alt_control_reg1, MAIN_CR_EN);


    /* STANDBY_PWRCTRL_SEL[1:0] = 0 */
    status = BSP_PMIC_ReadReg(MAIN_CR, &data); /* read MAIN_CR to save data */
    data &= ~STANDBY_PWRCTRL_SEL_3; /* clear STANDBY_PWRCTRL_SEL bits */
    status = BSP_PMIC_WriteReg(MAIN_CR, data); /* write MAIN_CR to set STANDBY_PWRCTRL_SEL[1:0] = 0 */

    break;

    case STPMIC1L_STANDBY_DDR_SR:

    /* VDDCORE: BUCK1, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg1, 0x0); 
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg2, 0x0);


    /* VDD_DDR: BUCK2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=1.35, State=ON_AUTO */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg1, 0x55);
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg2, \
                             MAIN_CR_PREG_AUTO_MODE | MAIN_CR_EN);

    /* VDD: LDO2, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=1, Source=NA, V=3.3, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD].control_reg1, \
                             (0x18 << 1) | MAIN_CR_EN);

    /* VTT_DDR: LDO3, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].alt_control_reg1, 0x0);

    /* VDD3V3_USB: LDO4, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD3V3_USB].control_reg1,0x0);

    /* VDD_SD: LDO5, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_SD].alt_control_reg1, 0x0);

    /* EN1: GPO1, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN1].control_reg1, MAIN_CR_EN);


    /* EN2: GPO2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN2].alt_control_reg1, MAIN_CR_EN);


    /* STANDBY_PWRCTRL_SEL[1:0] = 0 */
    status = BSP_PMIC_ReadReg(MAIN_CR, &data); /* read MAIN_CR to save data */
    data &= ~STANDBY_PWRCTRL_SEL_3; /* clear STANDBY_PWRCTRL_SEL bits */
    status = BSP_PMIC_WriteReg(MAIN_CR, data); /* write MAIN_CR to set STANDBY_PWRCTRL_SEL[1:0] = 0 */

    break;

    case STPMIC1L_STANDBY_DDR_OFF:

    /* VDDCORE: BUCK1, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg1, 0x0); 
    status = BSP_PMIC_WriteReg(board_regulators_table[VDDCORE].alt_control_reg2, 0x0);


    /* VDD_DDR: BUCK2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg1, 0x0);
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_DDR].alt_control_reg2, 0x0);

    /* VDD: LDO2, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=1, Source=NA, V=3.3, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD].control_reg1, \
                             (0x18 << 1) | MAIN_CR_EN);

    /* VTT_DDR: LDO3, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VTT_DDR].alt_control_reg1, 0x0);

    /* VDD3V3_USB: LDO4, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD3V3_USB].control_reg1,0x0);

    /* VDD_SD: LDO5, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=OFF */
    status = BSP_PMIC_WriteReg(board_regulators_table[VDD_SD].alt_control_reg1, 0x0);

    /* EN1: GPO1, PWRCTRL_SEL=0, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=0,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN1].control_reg1, MAIN_CR_EN);


    /* EN2: GPO2, PWRCTRL_SEL=1, PWRCTRL_DLY_H=0, PWRCTRL_DLY_L=0, PWRCTRL_EN=1,
    PWRCTRL_RST=0, Msk_Rst=0, Source=NA, V=NA, State=ON */
    status = BSP_PMIC_WriteReg(board_regulators_table[EN2].alt_control_reg1, MAIN_CR_EN);

    /* STANDBY_PWRCTRL_SEL[1:0] = 0 */
    status = BSP_PMIC_ReadReg(MAIN_CR, &data); /* read MAIN_CR to save data */
    data &= ~STANDBY_PWRCTRL_SEL_3; /* clear STANDBY_PWRCTRL_SEL bits */
    status = BSP_PMIC_WriteReg(MAIN_CR, data| \
                                 STANDBY_PWRCTRL_SEL_1); /* write MAIN_CR to set STANDBY_PWRCTRL_SEL[1:0] = 1 */

    break;

    default :
      status = BSP_ERROR_WRONG_PARAM;
  }

  return status;
}

void STPMIC_Enable_Interrupt(PMIC_IRQn IRQn)
{
  uint8_t irq_reg;
  uint8_t irq_reg_value;
  uint8_t mask ;

  if (IRQn >= IRQ_NR)
  {
    return ;
  }

  /* IRQ register is IRQ Number divided by 8 */
  irq_reg = IRQn >> 3 ;

  /* value to be set in IRQ register corresponds to BIT(N) where N is the Interrupt id modulo 8 */
  irq_reg_value = 1 << (IRQn % 8);

  /* Get active mask from register */
  BSP_PMIC_ReadReg(INT_MASK_R1 + irq_reg, &mask);
  mask &=  ~irq_reg_value ;
  /* Clear relevant mask to enable interrupt */
  BSP_PMIC_WriteReg(INT_MASK_R1 + irq_reg, mask);

}

void STPMIC_Disable_Interrupt(PMIC_IRQn IRQn)
{
  uint8_t irq_reg;
  uint8_t irq_reg_value;
  uint8_t mask ;

  if (IRQn >= IRQ_NR)
  {
    return ;
  }

  /* IRQ register is IRQ Number divided by 8 */
  irq_reg = IRQn >> 3 ;

  /* value to be set in IRQ register corresponds to BIT(N) where N is the Interrupt id modulo 8 */
  irq_reg_value = 1 << (IRQn % 8);

  /* Get active mask from register */
  BSP_PMIC_ReadReg(INT_MASK_R1 + irq_reg, &mask);
  mask |=  irq_reg_value ;
  /* Set relevant mask to disable interrupt */
  BSP_PMIC_WriteReg(INT_MASK_R1 + irq_reg, mask);

}


void STPMIC_IrqHandler(void)
{
  uint8_t irq_reg;
  uint8_t mask;
  uint8_t pending_events;
  uint8_t i;

  for (irq_reg = 0 ; irq_reg < STM32_PMIC_NUM_IRQ_REGS ; irq_reg++)
  {
    /* Get pending events & active mask */
    BSP_PMIC_ReadReg(INT_MASK_R1 + irq_reg, &mask);
    BSP_PMIC_ReadReg(INT_PENDING_R1 + irq_reg, &pending_events);
    pending_events &=  ~mask ;

    /* Go through all bits for each register */
    for (i = 0 ; i < 8 ; i++)
    {
      if (pending_events & (1 << i))
      {
        /* Callback with parameter computes as "PMIC Interrupt" enum */
        STPMIC_INTn_Callback((PMIC_IRQn)(irq_reg * 8 + (7 - i)));
      }
    }
    /* Clear events in appropriate register for the event with mask set */
    BSP_PMIC_WriteReg(INT_CLEAR_R1 + irq_reg, pending_events);
  }

}

void STPMIC_Sw_Reset(void)
{
  /* Write 1 in bit 0 of MAIN_CONTROL Register */
  BSP_PMIC_UpdateReg(MAIN_CR, SOFTWARE_SWITCH_OFF_ENABLED);
  return ;
}


__weak void BSP_PMIC_INTn_Callback(PMIC_IRQn IRQn)
{
  switch (IRQn)
  {
    case STPMIC1L_IT_PONKEY_F:
      printf("STPMIC1L_IT_PONKEY_F");
      break;
    case STPMIC1L_IT_PONKEY_R:
      printf("STPMIC1L_IT_PONKEY_R");
      break;
    case STPMIC1L_IT_VINLOW_FA:
      printf("STPMIC1L_IT_VINLOW_FA");
      break;
    case STPMIC1L_IT_VINLOW_RI:
      printf("STPMIC1L_IT_VINLOW_RI");
      break;
    case STPMIC1L_IT_THW_FA:
      printf("STPMIC1L_IT_THW_FA");
      break;
    case STPMIC1L_IT_THW_RI:
      printf("STPMIC1L_IT_THW_RI");
      break;
    case STPMIC1L_IT_BUCK1_OCP:
      printf("STPMIC1L_IT_BUCK1_OCP");
      break;
    case STPMIC1L_IT_BUCK2_OCP:
      printf("STPMIC1L_IT_BUCK2_OCP");
      break;
    case STPMIC1L_IT_LDO2_OCP:
      printf("STPMIC1L_IT_LDO2_OCP");
      break;
    case STPMIC1L_IT_LDO3_OCP:
      printf("STPMIC1L_IT_LDO3_OCP");
      break;
    case STPMIC1L_IT_LDO4_OCP:
      printf("STPMIC1L_IT_LDO4_OCP");
      break;
    case STPMIC1L_IT_LDO5_OCP:
      printf("STPMIC1L_IT_LDO5_OCP");
      break;
    default:
      printf("Unknown IRQ %d", IRQn);
      break;
  }
  printf(" Interrupt received\n\r");
}

void STPMIC_INTn_Callback(PMIC_IRQn IRQn)
{
  BSP_PMIC_INTn_Callback(IRQn);
};
#if 0 /* SBA test */
void BSP_PMIC_INTn_IRQHandler(void)
{
  /*printf("%s:%d\n\r", __FUNCTION__, __LINE__);*/
  /* Call HAL EXTI IRQ Handler to clear appropriate flags */
  HAL_GPIO_EXTI_IRQHandler(PMIC_INTN_PIN);

  STPMIC_IrqHandler();
}
#endif /* 0 */

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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
