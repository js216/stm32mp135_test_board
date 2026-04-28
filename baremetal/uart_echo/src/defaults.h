// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file defaults.h
 * @brief Default values and DDR memory map
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#ifndef DEFAULTS_H
#define DEFAULTS_H

/* DDR base address (Cortex-A7 physical). */
#define DEF_DDR_BASE 0xC0000000U

#define DEF_PRINT_LEN 128

/* Linux kernel. */
#define DEF_LINUX_ADDR 0xC2000000U
#define DEF_LINUX_LEN  15000 /* sectors on SD */
#define DEF_LINUX_BLK  896   /* starting LBA on SD */

/* Device Tree Blob. */
#define DEF_DTB_ADDR 0xC4000000U
#define DEF_DTB_LEN  250 /* sectors on SD */
#define DEF_DTB_BLK  640 /* starting LBA on SD */

/* USB MSC DDR backing store.  Host writes land here; fmc_flush commits to
 * NAND.  Only active in NAND builds -- EVB uses the SD card directly. */
#define FMC_DDR_BUF_ADDR 0xC8000000U
#define FMC_DDR_BUF_SIZE 0x10000000U /* 256 MiB */

/* Recovery initrd destination (patched into /chosen by dtb_patch_initrd).
 * Placed above the USB MSC buffer; ddr.c enforces this at compile time. */
#define DEF_INITRD_ADDR 0xD8000000U
#define DEF_INITRD_SIZE 0x02000000U /* 32 MiB */
#define DEF_INITRD_END  (DEF_INITRD_ADDR + DEF_INITRD_SIZE)

#endif // DEFAULTS_H
