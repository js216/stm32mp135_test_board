/**
 ******************************************************************************
 * @file    usbd_msc_storage_template.c
 * @author  MCD Application Team
 * @brief   Memory management layer
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

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}{adafruit}_sd.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_msc_storage.h"
#include "stm32mp13xx_hal_def.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Extern function prototypes ------------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

#define STORAGE_LUN_NBR 1U
#define STORAGE_BLK_NBR 0x100000
#define STORAGE_BLK_SIZ 0x200U

__attribute__((section(".virtdrive"))) static volatile uint8_t
    virtdrive[STORAGE_BLK_NBR * STORAGE_BLK_SIZ];

int8_t STORAGE_Init(uint8_t lun);

int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num,
                           uint16_t *block_size);

int8_t STORAGE_IsReady(uint8_t lun);

int8_t STORAGE_IsWriteProtected(uint8_t lun);

int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr,
                    uint16_t blk_len);

int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr,
                     uint16_t blk_len);

int8_t STORAGE_GetMaxLun(void);

/* USB Mass storage Standard Inquiry Data */
int8_t STORAGE_Inquirydata[] = /* 36 */
    {

        /* LUN 0 */
        0x00, 0x80, 0x02, 0x02, (STANDARD_INQUIRY_DATA_LEN - 5),
        0x00, 0x00, 0x00, 'S',  'T',
        'M',  ' ',  ' ',  ' ',  ' ',
        ' ', /* Manufacturer : 8 bytes */
        'P',  'r',  'o',  'd',  'u',
        'c',  't',  ' ', /* Product      : 16 Bytes */
        ' ',  ' ',  ' ',  ' ',  ' ',
        ' ',  ' ',  ' ',  '0',  '.',
        '0',  '1', /* Version      : 4 Bytes */
};

USBD_StorageTypeDef USBD_MSC_fops = {
    STORAGE_Init,      STORAGE_GetCapacity,
    STORAGE_IsReady,   STORAGE_IsWriteProtected,
    STORAGE_Read,      STORAGE_Write,
    STORAGE_GetMaxLun, STORAGE_Inquirydata,

};

/**
 * @brief  Initializes the storage unit (medium)
 * @param  lun: Logical unit number
 * @retval Status (0 : OK / -1 : Error)
 */
int8_t STORAGE_Init(uint8_t lun)
{
   UNUSED(lun);

   return (0);
}

/**
 * @brief  Returns the medium capacity.
 * @param  lun: Logical unit number
 * @param  block_num: Number of total block number
 * @param  block_size: Block size
 * @retval Status (0: OK / -1: Error)
 */
int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num,
                           uint16_t *block_size)
{
   UNUSED(lun);

   *block_num  = STORAGE_BLK_NBR;
   *block_size = STORAGE_BLK_SIZ;
   return (0);
}

/**
 * @brief  Checks whether the medium is ready.
 * @param  lun: Logical unit number
 * @retval Status (0: OK / -1: Error)
 */
int8_t STORAGE_IsReady(uint8_t lun)
{
   UNUSED(lun);

   return (0);
}

/**
 * @brief  Checks whether the medium is write protected.
 * @param  lun: Logical unit number
 * @retval Status (0: write enabled / -1: otherwise)
 */
int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
   UNUSED(lun);

   return 0;
}

/**
 * @brief  Reads data from the medium.
 * @param  lun: Logical unit number
 * @param  buf: data buffer
 * @param  blk_addr: Logical block address
 * @param  blk_len: Blocks number
 * @retval Status (0: OK / -1: Error)
 */
int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr,
                    uint16_t blk_len)
{
   const uint32_t *src =
       (const uint32_t *)&virtdrive[blk_addr * STORAGE_BLK_SIZ];
   uint8_t *dst = buf;

   for (uint32_t blk = 0; blk < blk_len; blk++) {
      for (int i = 0; i < STORAGE_BLK_SIZ; i += 4) {
         uint32_t w = *src++; /* 32-bit aligned read */

         dst[0] = (uint8_t)(w & 0xff);
         dst[1] = (uint8_t)((w >> 8) & 0xff);
         dst[2] = (uint8_t)((w >> 16) & 0xff);
         dst[3] = (uint8_t)((w >> 24) & 0xff);

         dst += 4;
      }
   }

   return USBD_OK;
}

/**
 * @brief  Writes data into the medium.
 * @param  lun: Logical unit number
 * @param  buf: data buffer
 * @param  blk_addr: Logical block address
 * @param  blk_len: Blocks number
 * @retval Status (0 : OK / -1 : Error)
 */
int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr,
                     uint16_t blk_len)
{
   uint8_t *src  = buf;
   uint32_t *dst = (uint32_t *)&virtdrive[blk_addr * STORAGE_BLK_SIZ];

   for (uint32_t blk = 0; blk < blk_len; blk++) {
      for (int i = 0; i < STORAGE_BLK_SIZ; i += 4) {
         uint32_t w = ((uint32_t)src[i + 3] << 24) |
                      ((uint32_t)src[i + 2] << 16) |
                      ((uint32_t)src[i + 1] << 8) | ((uint32_t)src[i + 0]);
         *dst++ = w; // 32-bit aligned write
      }
      src += STORAGE_BLK_SIZ;
   }

   return USBD_OK;
}

/**
 * @brief  Returns the Max Supported LUNs.
 * @param  None
 * @retval Lun(s) number
 */
int8_t STORAGE_GetMaxLun(void)
{
   return (STORAGE_LUN_NBR - 1);
}
