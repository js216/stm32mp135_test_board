// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file mmu_stm32mp13xx.h
 * @brief Driver and board low-level setup
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#ifndef MMU_STM32MP13XX_H
#define MMU_STM32MP13XX_H

/**
  \brief  Create Translation Table.

   Creates Memory Management Unit Translation Table.
 */
extern void MMU_CreateTranslationTable(void);

#endif // MMU_STM32MP13XX_H
