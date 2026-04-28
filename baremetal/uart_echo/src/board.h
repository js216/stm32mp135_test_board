/* SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file board.h
 * @brief Board specific definitions: pins, devices, etc.
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#ifndef BOARD_H
#define BOARD_H

#include "defaults.h"

#ifdef EVB // STM32MP135F-DK: Discovery kit with STM32MP135F MPU

#undef REG_PRINTOUT
#define LCD_DISPLAY
#undef NAND_FLASH
#define USE_MCP23x17 1

/* DDR REG VALUES TO BE SAVED */
#define DDR_MEM_NAME  "DDR3-1066 bin F 1x4Gb 533MHz v1.53"
#define DDR_MEM_SPEED 533000
#define DDR_MEM_SIZE  0x20000000
/* ctl.static */
#define DDR_MSTR       0x00040401
#define DDR_MRCTRL0    0x00000010
#define DDR_MRCTRL1    0x00000000
#define DDR_DERATEEN   0x00000000
#define DDR_DERATEINT  0x00800000
#define DDR_PWRCTL     0x00000000
#define DDR_PWRTMG     0x00400010
#define DDR_HWLPCTL    0x00000000
#define DDR_RFSHCTL0   0x00210000
#define DDR_RFSHCTL3   0x00000000
#define DDR_CRCPARCTL0 0x00000000
#define DDR_ZQCTL0     0xc2000040
#define DDR_DFITMG0    0x02060105
#define DDR_DFITMG1    0x00000202
#define DDR_DFILPCFG0  0x07000000
#define DDR_DFIUPD0    0xc0400003
#define DDR_DFIUPD1    0x00000000
#define DDR_DFIUPD2    0x00000000
#define DDR_DFIPHYMSTR 0x00000000
#define DDR_ODTMAP     0x00000001
#define DDR_DBG0       0x00000000
#define DDR_DBG1       0x00000000
#define DDR_DBGCMD     0x00000000
#define DDR_POISONCFG  0x00000000
#define DDR_PCCFG      0x00000010
/* ctl.timing */
#define DDR_RFSHTMG   0x0081008b
#define DDR_DRAMTMG0  0x121b2414
#define DDR_DRAMTMG1  0x000a041c
#define DDR_DRAMTMG2  0x0608090f
#define DDR_DRAMTMG3  0x0050400c
#define DDR_DRAMTMG4  0x08040608
#define DDR_DRAMTMG5  0x06060403
#define DDR_DRAMTMG6  0x02020002
#define DDR_DRAMTMG7  0x00000202
#define DDR_DRAMTMG8  0x00001005
#define DDR_DRAMTMG14 0x000000a0
#define DDR_ODTCFG    0x06000600
/* ctl.perf */
#define DDR_SCHED       0x00000f01
#define DDR_SCHED1      0x00000000
#define DDR_PERFHPR1    0x00000001
#define DDR_PERFLPR1    0x04000200
#define DDR_PERFWR1     0x08000400
#define DDR_PCFGR_0     0x00000000
#define DDR_PCFGW_0     0x00000000
#define DDR_PCFGQOS0_0  0x00100009
#define DDR_PCFGQOS1_0  0x00000020
#define DDR_PCFGWQOS0_0 0x01100b03
#define DDR_PCFGWQOS1_0 0x01000200
/* ctl.map */
#define DDR_ADDRMAP1  0x00080808
#define DDR_ADDRMAP2  0x00000000
#define DDR_ADDRMAP3  0x00000000
#define DDR_ADDRMAP4  0x00001f1f
#define DDR_ADDRMAP5  0x07070707
#define DDR_ADDRMAP6  0x0f070707
#define DDR_ADDRMAP9  0x00000000
#define DDR_ADDRMAP10 0x00000000
#define DDR_ADDRMAP11 0x00000000
/* phy.static */
#define DDR_PGCR   0x01442e02
#define DDR_ACIOCR 0x10400812
#define DDR_DXCCR  0x00000c40
#define DDR_DSGCR  0xf200011f
#define DDR_DCR    0x0000000b
#define DDR_ODTCR  0x00010000
#define DDR_ZQ0CR1 0x00000038
#define DDR_DX0GCR 0x0000ce81
#define DDR_DX1GCR 0x0000ce81
/* phy.timing */
#define DDR_PTR0  0x0022aa5b
#define DDR_PTR1  0x04841104
#define DDR_PTR2  0x042da068
#define DDR_DTPR0 0x38d488d0
#define DDR_DTPR1 0x098b00d8
#define DDR_DTPR2 0x10023600
#define DDR_MR0   0x00000840
#define DDR_MR1   0x00000000
#define DDR_MR2   0x00000208
#define DDR_MR3   0x00000000
/* /!\ No need to copy DDR dynamic registers to conf file */
/* ctl.dyn */
#define DDR_STAT    0x00000001
#define DDR_INIT0   0x4002004e
#define DDR_DFIMISC 0x00000001
#define DDR_DFISTAT 0x00000001
#define DDR_SWCTL   0x00000001
#define DDR_SWSTAT  0x00000001
#define DDR_PCTRL_0 0x00000001
/* phy.dyn */
#define DDR_PIR      0x00000000
#define DDR_PGSR     0x0000001f
#define DDR_ZQ0SR0   0x80021d4e
#define DDR_ZQ0SR1   0x00000000
#define DDR_DX0GSR0  0x00008001
#define DDR_DX0GSR1  0x00000000
#define DDR_DX0DLLCR 0x40000000
#define DDR_DX0DQTR  0xffffffff
#define DDR_DX0DQSTR 0x3db02001
#define DDR_DX1GSR0  0x00008001
#define DDR_DX1GSR1  0x00000000
#define DDR_DX1DLLCR 0x40000000
#define DDR_DX1DQTR  0xffffffff
#define DDR_DX1DQSTR 0x3db02001

#define CTP_INT_PORT    GPIOF
#define CTP_INT_PIN     GPIO_PIN_5
#define CTP_SCL_PORT    GPIOD
#define CTP_SCL_PIN     GPIO_PIN_1
#define CTP_SDA_PORT    GPIOH
#define CTP_SDA_PIN     GPIO_PIN_6
#define CTP_AF          GPIO_AF4_I2C5
#define CTP_RST_PORT    GPIOH
#define CTP_RST_PIN     GPIO_PIN_2
#define CTP_INT_IRQn    EXTI5_IRQn
#define CTP_INT_PIN_NUM 5U
#define ctp_irqhandler  EXTI5_IRQHandler
#define ctp_unused      EXTI12_IRQHandler

#define LCD_WIDTH  480U // LCD PIXEL WIDTH
#define LCD_HEIGHT 272U // LCD PIXEL HEIGHT
#define LCD_HSYNC  41U  // Horizontal synchronization
#define LCD_HBP    13U  // Horizontal back porch
#define LCD_HFP    32U  // Horizontal front porch
#define LCD_VSYNC  10U  // Vertical synchronization
#define LCD_VBP    2U   // Vertical back porch
#define LCD_VFP    2U   // Vertical front porch

#define LCD_BL_PORT    GPIOE
#define LCD_BL_PIN     GPIO_PIN_12
#define LCD_DISP_PORT  GPIOI
#define LCD_DISP_PIN   GPIO_PIN_7
#define LCD_CLK_PORT   GPIOD
#define LCD_CLK_PIN    GPIO_PIN_9
#define LCD_CLK_AF     GPIO_AF13_LCD
#define LCD_HSYNC_PORT GPIOC
#define LCD_HSYNC_PIN  GPIO_PIN_6
#define LCD_HSYNC_AF   GPIO_AF14_LCD
#define LCD_VSYNC_PORT GPIOG
#define LCD_VSYNC_PIN  GPIO_PIN_4
#define LCD_VSYNC_AF   GPIO_AF11_LCD
#define LCD_DE_PORT    GPIOH
#define LCD_DE_PIN     GPIO_PIN_9
#define LCD_DE_AF      GPIO_AF11_LCD
#define LCD_R3_PORT    GPIOB
#define LCD_R3_PIN     GPIO_PIN_12
#define LCD_R3_AF      GPIO_AF13_LCD
#define LCD_R4_PORT    GPIOD
#define LCD_R4_PIN     GPIO_PIN_14
#define LCD_R4_AF      GPIO_AF14_LCD
#define LCD_R5_PORT    GPIOE
#define LCD_R5_PIN     GPIO_PIN_7
#define LCD_R5_AF      GPIO_AF14_LCD
#define LCD_R6_PORT    GPIOE
#define LCD_R6_PIN     GPIO_PIN_13
#define LCD_R6_AF      GPIO_AF14_LCD
#define LCD_R7_PORT    GPIOE
#define LCD_R7_PIN     GPIO_PIN_9
#define LCD_R7_AF      GPIO_AF14_LCD
#define LCD_G2_PORT    GPIOH
#define LCD_G2_PIN     GPIO_PIN_3
#define LCD_G2_AF      GPIO_AF14_LCD
#define LCD_G3_PORT    GPIOF
#define LCD_G3_PIN     GPIO_PIN_3
#define LCD_G3_AF      GPIO_AF14_LCD
#define LCD_G4_PORT    GPIOD
#define LCD_G4_PIN     GPIO_PIN_5
#define LCD_G4_AF      GPIO_AF14_LCD
#define LCD_G5_PORT    GPIOG
#define LCD_G5_PIN     GPIO_PIN_0
#define LCD_G5_AF      GPIO_AF14_LCD
#define LCD_G6_PORT    GPIOC
#define LCD_G6_PIN     GPIO_PIN_7
#define LCD_G6_AF      GPIO_AF14_LCD
#define LCD_G7_PORT    GPIOA
#define LCD_G7_PIN     GPIO_PIN_15
#define LCD_G7_AF      GPIO_AF11_LCD
#define LCD_B3_PORT    GPIOF
#define LCD_B3_PIN     GPIO_PIN_2
#define LCD_B3_AF      GPIO_AF14_LCD
#define LCD_B4_PORT    GPIOH
#define LCD_B4_PIN     GPIO_PIN_4
#define LCD_B4_AF      GPIO_AF11_LCD
#define LCD_B5_PORT    GPIOE
#define LCD_B5_PIN     GPIO_PIN_0
#define LCD_B5_AF      GPIO_AF14_LCD
#define LCD_B6_PORT    GPIOB
#define LCD_B6_PIN     GPIO_PIN_6
#define LCD_B6_AF      GPIO_AF14_LCD
#define LCD_B7_PORT    GPIOF
#define LCD_B7_PIN     GPIO_PIN_1
#define LCD_B7_AF      GPIO_AF13_LCD

#define ETH_CLK_SRC     HAL_ETH1_REF_CLK_RX_CLK_PIN
#define ETH_RX_CLK_PORT GPIOA
#define ETH_RX_CLK_PIN  GPIO_PIN_1
#define ETH_RX_CLK_AF   GPIO_AF11_ETH
#define ETH_MDIO_PORT   GPIOA
#define ETH_MDIO_PIN    GPIO_PIN_2
#define ETH_MDIO_AF     GPIO_AF11_ETH
#define ETH_TX_EN_PORT  GPIOB
#define ETH_TX_EN_PIN   GPIO_PIN_11
#define ETH_TX_EN_AF    GPIO_AF11_ETH
#define ETH_CRS_DV_PORT GPIOC
#define ETH_CRS_DV_PIN  GPIO_PIN_1
#define ETH_CRS_DV_AF   GPIO_AF10_ETH
#define ETH_RXD0_PORT   GPIOC
#define ETH_RXD0_PIN    GPIO_PIN_4
#define ETH_RXD0_AF     GPIO_AF11_ETH
#define ETH_RXD1_PORT   GPIOC
#define ETH_RXD1_PIN    GPIO_PIN_5
#define ETH_RXD1_AF     GPIO_AF11_ETH
#define ETH_MDC_PORT    GPIOG
#define ETH_MDC_PIN     GPIO_PIN_2
#define ETH_MDC_AF      GPIO_AF11_ETH
#define ETH_TXD0_PORT   GPIOG
#define ETH_TXD0_PIN    GPIO_PIN_13
#define ETH_TXD0_AF     GPIO_AF11_ETH
#define ETH_TXD1_PORT   GPIOG
#define ETH_TXD1_PIN    GPIO_PIN_14
#define ETH_TXD1_AF     GPIO_AF11_ETH
#define ETH_NRST_PIN    MCP_PIN_9

#define FMC_MAKER             0x00U
#define FMC_DEV               0x00U
#define FMC_3RD               0x00U
#define FMC_4TH               0x00U
#define FMC_PAGE_SIZE_BYTES   4096U
#define FMC_OOB_SIZE_BYTES    256U
#define FMC_BLOCK_SIZE_PAGES  64U
#define FMC_PLANE_SIZE_BLOCKS 1024U
#define FMC_PLANE_NBR         2U
#define FMC_SECTOR_SIZE       512U
#define FMC_SCRATCH_ADDR      DEF_DDR_BASE

#else

#undef REG_PRINTOUT
#undef LCD_DISPLAY
#define NAND_FLASH
#define USE_MCP23x17    0

/* DDR REG VALUES TO BE SAVED */
#define DDR_MEM_NAME    "DDR3-1066 bin F 1x4Gb 533MHz v1.53"
#define DDR_MEM_SPEED   533000
#define DDR_MEM_SIZE    0x20000000
/* ctl.static */
#define DDR_MSTR        0x00040401
#define DDR_MRCTRL0     0x00000010
#define DDR_MRCTRL1     0x00000000
#define DDR_DERATEEN    0x00000000
#define DDR_DERATEINT   0x00800000
#define DDR_PWRCTL      0x00000000
#define DDR_PWRTMG      0x00400010
#define DDR_HWLPCTL     0x00000000
#define DDR_RFSHCTL0    0x00210000
#define DDR_RFSHCTL3    0x00000000
#define DDR_CRCPARCTL0  0x00000000
#define DDR_ZQCTL0      0xc2000040
#define DDR_DFITMG0     0x02060105
#define DDR_DFITMG1     0x00000202
#define DDR_DFILPCFG0   0x07000000
#define DDR_DFIUPD0     0xc0400003
#define DDR_DFIUPD1     0x00000000
#define DDR_DFIUPD2     0x00000000
#define DDR_DFIPHYMSTR  0x00000000
#define DDR_ODTMAP      0x00000001
#define DDR_DBG0        0x00000000
#define DDR_DBG1        0x00000000
#define DDR_DBGCMD      0x00000000
#define DDR_POISONCFG   0x00000000
#define DDR_PCCFG       0x00000010
/* ctl.timing */
#define DDR_RFSHTMG     0x0081008b
#define DDR_DRAMTMG0    0x121b2414
#define DDR_DRAMTMG1    0x000a041c
#define DDR_DRAMTMG2    0x0608090f
#define DDR_DRAMTMG3    0x0050400c
#define DDR_DRAMTMG4    0x08040608
#define DDR_DRAMTMG5    0x06060403
#define DDR_DRAMTMG6    0x02020002
#define DDR_DRAMTMG7    0x00000202
#define DDR_DRAMTMG8    0x00001005
#define DDR_DRAMTMG14   0x000000a0
#define DDR_ODTCFG      0x06000600
/* ctl.perf */
#define DDR_SCHED       0x00000f01
#define DDR_SCHED1      0x00000000
#define DDR_PERFHPR1    0x00000001
#define DDR_PERFLPR1    0x04000200
#define DDR_PERFWR1     0x08000400
#define DDR_PCFGR_0     0x00000000
#define DDR_PCFGW_0     0x00000000
#define DDR_PCFGQOS0_0  0x00100009
#define DDR_PCFGQOS1_0  0x00000020
#define DDR_PCFGWQOS0_0 0x01100b03
#define DDR_PCFGWQOS1_0 0x01000200
/* ctl.map */
#define DDR_ADDRMAP1    0x00080808
#define DDR_ADDRMAP2    0x00000000
#define DDR_ADDRMAP3    0x00000000
#define DDR_ADDRMAP4    0x00001f1f
#define DDR_ADDRMAP5    0x07070707
#define DDR_ADDRMAP6    0x0f070707
#define DDR_ADDRMAP9    0x00000000
#define DDR_ADDRMAP10   0x00000000
#define DDR_ADDRMAP11   0x00000000
/* phy.static */
#define DDR_PGCR        0x01442e02
#define DDR_ACIOCR      0x10400812
#define DDR_DXCCR       0x00000c40
#define DDR_DSGCR       0xf200011f
#define DDR_DCR         0x0000000b
#define DDR_ODTCR       0x00010000
#define DDR_ZQ0CR1      0x00000038
#define DDR_DX0GCR      0x0000ce81
#define DDR_DX1GCR      0x0000ce81
/* phy.timing */
#define DDR_PTR0        0x0022aa5b
#define DDR_PTR1        0x04841104
#define DDR_PTR2        0x042da068
#define DDR_DTPR0       0x38d488d0
#define DDR_DTPR1       0x098b00d8
#define DDR_DTPR2       0x10023600
#define DDR_MR0         0x00000840
#define DDR_MR1         0x00000000
#define DDR_MR2         0x00000208
#define DDR_MR3         0x00000000
/* /!\ No need to copy DDR dynamic registers to conf file */
/* ctl.dyn */
#define DDR_STAT        0x00000001
#define DDR_INIT0       0x4002004e
#define DDR_DFIMISC     0x00000001
#define DDR_DFISTAT     0x00000001
#define DDR_SWCTL       0x00000001
#define DDR_SWSTAT      0x00000001
#define DDR_PCTRL_0     0x00000001
/* phy.dyn */
#define DDR_PIR         0x00000000
#define DDR_PGSR        0x0000001f
#define DDR_ZQ0SR0      0x80021d4e
#define DDR_ZQ0SR1      0x00000000
#define DDR_DX0GSR0     0x00008001
#define DDR_DX0GSR1     0x00000000
#define DDR_DX0DLLCR    0x40000000
#define DDR_DX0DQTR     0xffffffff
#define DDR_DX0DQSTR    0x3db02001
#define DDR_DX1GSR0     0x00008001
#define DDR_DX1GSR1     0x00000000
#define DDR_DX1DLLCR    0x40000000
#define DDR_DX1DQTR     0xffffffff
#define DDR_DX1DQSTR    0x3db02001

#define CTP_INT_PORT    GPIOH
#define CTP_INT_PIN     GPIO_PIN_12
#define CTP_SCL_PORT    GPIOH
#define CTP_SCL_PIN     GPIO_PIN_13
#define CTP_SDA_PORT    GPIOF
#define CTP_SDA_PIN     GPIO_PIN_3
#define CTP_AF          GPIO_AF4_I2C5
#define CTP_RST_PORT    GPIOB
#define CTP_RST_PIN     GPIO_PIN_7
#define CTP_INT_IRQn    EXTI12_IRQn
#define CTP_INT_PIN_NUM 12U
#define ctp_irqhandler  EXTI12_IRQHandler
#define ctp_unused      EXTI5_IRQHandler

#define LCD_WIDTH  800U
#define LCD_HEIGHT 480U
#define LCD_HSYNC  1U
#define LCD_HBP    8U
#define LCD_HFP    8U
#define LCD_VSYNC  1U
#define LCD_VBP    16U
#define LCD_VFP    16U

#define LCD_BL_PORT    GPIOB
#define LCD_BL_PIN     GPIO_PIN_15
#define LCD_DISP_PORT  GPIOG
#define LCD_DISP_PIN   GPIO_PIN_7
#define LCD_CLK_PORT   GPIOD
#define LCD_CLK_PIN    GPIO_PIN_9
#define LCD_CLK_AF     GPIO_AF13_LCD
#define LCD_HSYNC_PORT GPIOE
#define LCD_HSYNC_PIN  GPIO_PIN_1
#define LCD_HSYNC_AF   GPIO_AF9_LCD
#define LCD_VSYNC_PORT GPIOE
#define LCD_VSYNC_PIN  GPIO_PIN_12
#define LCD_VSYNC_AF   GPIO_AF9_LCD
#define LCD_DE_PORT    GPIOG
#define LCD_DE_PIN     GPIO_PIN_6
#define LCD_DE_AF      GPIO_AF13_LCD
#define LCD_R3_PORT    GPIOD
#define LCD_R3_PIN     GPIO_PIN_9
#define LCD_R3_AF      GPIO_AF13_LCD
#define LCD_R4_PORT    GPIOE
#define LCD_R4_PIN     GPIO_PIN_3
#define LCD_R4_AF      GPIO_AF13_LCD
#define LCD_R5_PORT    GPIOF
#define LCD_R5_PIN     GPIO_PIN_5
#define LCD_R5_AF      GPIO_AF14_LCD
#define LCD_R6_PORT    GPIOF
#define LCD_R6_PIN     GPIO_PIN_0
#define LCD_R6_AF      GPIO_AF13_LCD
#define LCD_R7_PORT    GPIOF
#define LCD_R7_PIN     GPIO_PIN_6
#define LCD_R7_AF      GPIO_AF13_LCD
#define LCD_G2_PORT    GPIOF
#define LCD_G2_PIN     GPIO_PIN_7
#define LCD_G2_AF      GPIO_AF14_LCD
#define LCD_G3_PORT    GPIOE
#define LCD_G3_PIN     GPIO_PIN_6
#define LCD_G3_AF      GPIO_AF14_LCD
#define LCD_G4_PORT    GPIOG
#define LCD_G4_PIN     GPIO_PIN_5
#define LCD_G4_AF      GPIO_AF11_LCD
#define LCD_G5_PORT    GPIOG
#define LCD_G5_PIN     GPIO_PIN_0
#define LCD_G5_AF      GPIO_AF14_LCD
#define LCD_G6_PORT    GPIOA
#define LCD_G6_PIN     GPIO_PIN_12
#define LCD_G6_AF      GPIO_AF14_LCD
#define LCD_G7_PORT    GPIOA
#define LCD_G7_PIN     GPIO_PIN_15
#define LCD_G7_AF      GPIO_AF11_LCD
#define LCD_B3_PORT    GPIOG
#define LCD_B3_PIN     GPIO_PIN_15
#define LCD_B3_AF      GPIO_AF14_LCD
#define LCD_B4_PORT    GPIOB
#define LCD_B4_PIN     GPIO_PIN_2
#define LCD_B4_AF      GPIO_AF14_LCD
#define LCD_B5_PORT    GPIOH
#define LCD_B5_PIN     GPIO_PIN_9
#define LCD_B5_AF      GPIO_AF9_LCD
#define LCD_B6_PORT    GPIOF
#define LCD_B6_PIN     GPIO_PIN_4
#define LCD_B6_AF      GPIO_AF13_LCD
#define LCD_B7_PORT    GPIOB
#define LCD_B7_PIN     GPIO_PIN_6
#define LCD_B7_AF      GPIO_AF14_LCD

#define ETH_CLK_SRC HAL_ETH1_REF_CLK_RCC
#undef ETH_RX_CLK_PORT
#undef ETH_RX_CLK_PIN
#undef ETH_RX_CLK_AF
#define ETH_CLK_PORT      GPIOA
#define ETH_CLK_PIN       GPIO_PIN_11
#define ETH_CLK_AF        GPIO_AF11_ETH
#define ETH_MDIO_PORT     GPIOG
#define ETH_MDIO_PIN      GPIO_PIN_3
#define ETH_MDIO_AF       GPIO_AF11_ETH
#define ETH_TX_EN_PORT    GPIOB
#define ETH_TX_EN_PIN     GPIO_PIN_11
#define ETH_TX_EN_AF      GPIO_AF11_ETH
#define ETH_CRS_DV_PORT   GPIOA
#define ETH_CRS_DV_PIN    GPIO_PIN_7
#define ETH_CRS_DV_AF     GPIO_AF11_ETH
#define ETH_RXD0_PORT     GPIOC
#define ETH_RXD0_PIN      GPIO_PIN_4
#define ETH_RXD0_AF       GPIO_AF11_ETH
#define ETH_RXD1_PORT     GPIOC
#define ETH_RXD1_PIN      GPIO_PIN_5
#define ETH_RXD1_AF       GPIO_AF11_ETH
#define ETH_MDC_PORT      GPIOG
#define ETH_MDC_PIN       GPIO_PIN_2
#define ETH_MDC_AF        GPIO_AF11_ETH
#define ETH_TXD0_PORT     GPIOG
#define ETH_TXD0_PIN      GPIO_PIN_13
#define ETH_TXD0_AF       GPIO_AF11_ETH
#define ETH_TXD1_PORT     GPIOG
#define ETH_TXD1_PIN      GPIO_PIN_14
#define ETH_TXD1_AF       GPIO_AF11_ETH
#define ETH_PHY_INTN_PORT GPIOG
#define ETH_PHY_INTN_PIN  GPIO_PIN_12
#define ETH_PHY_INTN_AF   GPIO_AF11_ETH
#define ETH_NRST_PORT     GPIOG
#define ETH_NRST_PIN      GPIO_PIN_11

#define FMC_MAKER             0xC2U
#define FMC_DEV               0xDCU
#define FMC_3RD               0x90U
#define FMC_4TH               0xA2U
#define FMC_PAGE_SIZE_BYTES   4096U
#define FMC_OOB_SIZE_BYTES    256U
#define FMC_BLOCK_SIZE_PAGES  64U
#define FMC_PLANE_SIZE_BLOCKS 1024U
#define FMC_PLANE_NBR         2U
#define FMC_SECTOR_SIZE       512U
#define FMC_SCRATCH_ADDR      DEF_DDR_BASE

#endif

#endif // BOARD_H

// end file board.h
