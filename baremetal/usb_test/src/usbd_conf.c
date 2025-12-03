/**
 ******************************************************************************
 * @file    usbd_conf_template.c
 * @author  MCD Application Team
 * @brief   USB Device configuration and interface file
 *          This template should be copied to the user folder,
 *          renamed and customized following user needs.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2015 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "stm32mp13xx.h"
#include "usbd_core.h"
#include "usbd_conf.h"
#include "usbd_msc.h" /* Include class header file */
#include "stm32mp13xx_hal.h"
#include "setup.h"

#include "stm32mp13xx_hal_def.h"
#include "stm32mp13xx_hal_pcd.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
PCD_HandleTypeDef hpcd_USB_OTG_HS;


/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

void OTG_IRQHandler(void)
{
   HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}

/*******************************************************************************
  PCD BSP Routines
 *******************************************************************************/

void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{

   RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
   if(pcdHandle->Instance==USB_OTG_HS)
   {
      /** Initializes the peripherals clock
      */
      PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBPHY;
      PeriphClkInitStruct.UsbphyClockSelection = RCC_USBPHYCLKSOURCE_HSE;
      if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
      {
         Error_Handler();
      }

      /* Enable USBPHY Clock */
      __HAL_RCC_USBPHY_CLK_ENABLE();

      PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBO;
      PeriphClkInitStruct.UsboClockSelection = RCC_USBOCLKSOURCE_PHY;
      if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
      {
         Error_Handler();
      }

      /* USB_OTG_HS clock enable */
      __HAL_RCC_USBO_CLK_ENABLE();
      __HAL_RCC_USBO_FORCE_RESET();
      __HAL_RCC_USBO_RELEASE_RESET();

      /* USB_OTG_HS interrupt Init */
      IRQ_SetPriority(OTG_IRQn, 6);
      IRQ_Enable(OTG_IRQn);
   }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{

   if(pcdHandle->Instance==USB_OTG_HS)
   {
      /* Peripheral clock disable */
      __HAL_RCC_USBO_CLK_DISABLE();

      __HAL_RCC_USBO_FORCE_RESET();

      /* USB_OTG_HS interrupt Deinit */
      IRQ_Disable(OTG_IRQn);
   }
}
/**
 * @brief  Initializes the Low Level portion of the Device driver.
 * @param  pdev: Device handle
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
   memset(&hpcd_USB_OTG_HS, 0x0, sizeof(PCD_HandleTypeDef));

   hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
   hpcd_USB_OTG_HS.Init.dev_endpoints = 9;
   hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_HIGH;
   hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
   hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
   hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
   hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
   hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
   hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
   hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
   hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;

   hpcd_USB_OTG_HS.pData = pdev;
   pdev->pData = &hpcd_USB_OTG_HS;

   // Force this to null, so ST USB library doesn't try to de-init it
   // TODO ???
   ((USBD_HandleTypeDef *)hpcd_USB_OTG_HS.pData)->pClassData = NULL;

   if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK) {
      Error_Handler();
      return USBD_FAIL;
   }

   // TODO ????
   // HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x200);
   // HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, 0x40);
   // HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, 0x100);

   return USBD_OK;
}

/**
 * @brief  De-Initializes the Low Level portion of the Device driver.
 * @param  pdev: Device handle
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
   HAL_PCD_DeInit(pdev->pData);
   return USBD_OK;
}

/**
 * @brief  Starts the Low Level portion of the Device driver.
 * @param  pdev: Device handle
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
   HAL_PCD_Start(pdev->pData);
   return USBD_OK;
}

/**
 * @brief  Stops the Low Level portion of the Device driver.
 * @param  pdev: Device handle
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
   HAL_PCD_Stop(pdev->pData); 
   return USBD_OK;
}

/**
 * @brief  Opens an endpoint of the Low Level Driver.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @param  ep_type: Endpoint Type
 * @param  ep_mps: Endpoint Max Packet Size
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
      uint8_t ep_type, uint16_t ep_mps)
{
   HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);
   return USBD_OK;
}

/**
 * @brief  Closes an endpoint of the Low Level Driver.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
   HAL_PCD_EP_Close(pdev->pData, ep_addr);
   return USBD_OK;
}

/**
 * @brief  Flushes an endpoint of the Low Level Driver.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
   HAL_PCD_EP_Flush(pdev->pData, ep_addr);
   return USBD_OK;
}

/**
 * @brief  Sets a Stall condition on an endpoint of the Low Level Driver.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
   HAL_PCD_EP_SetStall(pdev->pData, ep_addr);
   return USBD_OK;
}

/**
 * @brief  Clears a Stall condition on an endpoint of the Low Level Driver.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,
      uint8_t ep_addr)
{
   HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);
   return USBD_OK;
}

/**
 * @brief  Returns Stall condition.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @retval Stall (1: Yes, 0: No)
 */
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
   PCD_HandleTypeDef *hpcd = pdev->pData;

   if ((ep_addr & 0x80) == 0x80) {
      return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
   } else {
      return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
   }
}

/**
 * @brief  Assigns a USB address to the device.
 * @param  pdev: Device handle
 * @param  dev_addr: Endpoint Number
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,
      uint8_t dev_addr)
{
   HAL_PCD_SetAddress(pdev->pData, dev_addr);
   return USBD_OK;
}

/**
 * @brief  Transmits data over an endpoint.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @param  pbuf: Pointer to data to be sent
 * @param  size: Data size
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
      uint8_t *pbuf, uint32_t size)
{
   HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);
   return USBD_OK;
}

/**
 * @brief  Prepares an endpoint for reception.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @param  pbuf: Pointer to data to be received
 * @param  size: Data size
 * @retval USBD Status
 */
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
      uint8_t ep_addr, uint8_t *pbuf,
      uint32_t size)
{
   HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);
   return USBD_OK;
}

/**
 * @brief  Returns the last transferred packet size.
 * @param  pdev: Device handle
 * @param  ep_addr: Endpoint Number
 * @retval Received Data Size
 */
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
   return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

/**
 * @brief  Static single allocation.
 * @param  size: Size of allocated memory
 * @retval None
 */
void *USBD_static_malloc(uint32_t size)
{
   static uint32_t mem[(sizeof(USBD_MSC_BOT_HandleTypeDef) / 4) + 1]; /* On 32-bit boundary */
   return mem;
}

/**
 * @brief  Dummy memory free
 * @param  p: Pointer to allocated  memory address
 * @retval None
 */
void USBD_static_free(void *p)
{

}

/**
 * @brief  Delays routine for the USB Device Library.
 * @param  Delay: Delay in ms
 * @retval None
 */
void USBD_LL_Delay(uint32_t Delay)
{
   UNUSED(Delay);
}

