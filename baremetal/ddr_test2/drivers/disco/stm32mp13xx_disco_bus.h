#ifndef STM32MP13XX_DISCO_BUS_H
#define STM32MP13XX_DISCO_BUS_H


#include "stm32mp13xx_disco_errno.h"
#include "stm32mp_util_conf.h"

#define AUDIO_I2C_ADDRESS                ((uint16_t)0x36U)
#define STPMU1_I2C_ADDRESS               ((0x33U & 0x7FU) << 1 )

/* I2C clock speed configuration (in Hz)
WARNING:
Make sure that this define is not already declared in other files (ie.
stm32mp13xx_disco.h file). It can be used in parallel by other modules. */
#ifndef I2C_SPEED
#define I2C_SPEED                        ((uint32_t)100000U)
#endif /* I2C_SPEED */

#define BUS_I2C_INSTANCE                    I2C4
#define BUS_I2C_CLK_ENABLE()                __HAL_RCC_I2C4_CLK_ENABLE()
#define BUS_I2C_CLK_DISABLE()               __HAL_RCC_I2C4_CLK_DISABLE()
#define BUS_I2C_FORCE_RESET()               __HAL_RCC_I2C4_FORCE_RESET()
#define BUS_I2C_RELEASE_RESET()             __HAL_RCC_I2C4_RELEASE_RESET()
#define BUS_I2C_EV_IRQn                     I2C4_EV_IRQn
#define BUS_I2C_ER_IRQn                     I2C4_ER_IRQn

#define BUS_I2C_SCL_PIN                       UTIL_PMIC_I2C_SCL_PIN

#define BUS_I2C_SCL_GPIO_PORT               GPIOE
#define BUS_I2C_SCL_GPIO_CLK_ENABLE()       __HAL_RCC_GPIOE_CLK_ENABLE()
#define BUS_I2C_SCL_GPIO_CLK_DISABLE()      __HAL_RCC_GPIOE_CLK_DISABLE()

#define BUS_I2C_SCL_AF                        UTIL_PMIC_I2C_SCL_AF

#define BUS_I2C_SDA_PIN                       UTIL_PMIC_I2C_SDA_PIN

#define BUS_I2C_SDA_GPIO_PORT               GPIOB
#define BUS_I2C_SDA_GPIO_CLK_ENABLE()       __HAL_RCC_GPIOB_CLK_ENABLE()
#define BUS_I2C_SDA_GPIO_CLK_DISABLE()      __HAL_RCC_GPIOB_CLK_DISABLE()

#define BUS_I2C_SDA_AF                      UTIL_PMIC_I2C_SDA_AF

#define BUS_I2C_FREQUENCY  100000U /* Frequency of I2Cn = 100 KHz*/

#define BUS_I2Cx_TIMING                      ((uint32_t)0x10805E89U)

/** @defgroup STM32MP13XX_DISCO_BUS_Exported_Types BUS Exported Types
 * @{
 */
#if (USE_HAL_I2C_REGISTER_CALLBACKS == 1)
typedef struct
{
   pI2C_CallbackTypeDef  pMspI2cInitCb;
   pI2C_CallbackTypeDef  pMspI2cDeInitCb;
}BSP_I2C_Cb_t;
#endif /* (USE_HAL_I2C_REGISTER_CALLBACKS == 1) */

int32_t BSP_I2C_Init(void);
int32_t BSP_I2C_DeInit(void);
int32_t BSP_I2C_WriteReg(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Length);
int32_t BSP_I2C_ReadReg(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Length);
int32_t BSP_I2C_WriteReg16(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Length);
int32_t BSP_I2C_ReadReg16(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Length);
int32_t BSP_I2C_Recv(uint16_t DevAddr, uint16_t Reg, uint16_t MemAddSize, uint8_t *pData, uint16_t Length);
int32_t BSP_I2C_Send(uint16_t DevAddr, uint16_t Reg, uint16_t MemAddSize, uint8_t *pData, uint16_t Length);
int32_t BSP_I2C_IsReady(uint16_t DevAddr, uint32_t Trials);

int32_t BSP_GetTick(void);

#if (USE_HAL_I2C_REGISTER_CALLBACKS == 1)
int32_t BSP_I2C_RegisterDefaultMspCallbacks (void);
int32_t BSP_I2C_RegisterMspCallbacks (BSP_I2C_Cb_t *Callback);
#endif /* USE_HAL_I2C_REGISTER_CALLBACKS */
__weak HAL_StatusTypeDef MX_I2C_Init(I2C_HandleTypeDef *phI2c);

#endif
