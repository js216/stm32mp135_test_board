// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file ddr.h
 * @brief DDR RAM management
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#ifndef DDR_H
#define DDR_H

#include <stdint.h>

void ddr_init(void);
void ddr_print_cmd(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);
void ddr_align_test(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);
void ddr_relocate_initrd(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif // DDR_H
