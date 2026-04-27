// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file qspi.h
 * @brief QUADSPI bare-register driver for FPGA bring-up
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 *
 * Polled, indirect mode only.  Uses HAL only for GPIO/clock muxing; the
 * QUADSPI peripheral itself is poked directly.
 */

#ifndef QSPI_H
#define QSPI_H

#include "stm32mp13xx_hal.h"
#include <stdint.h>

typedef enum {
   QSPI_LINES_NONE = 0,
   QSPI_LINES_1    = 1,
   QSPI_LINES_2    = 2,
   QSPI_LINES_4    = 4,
} qspi_lines_t;

typedef enum {
   QSPI_FMODE_WRITE = 0,
   QSPI_FMODE_READ  = 1,
} qspi_fmode_t;

typedef struct {
   uint8_t instruction;
   uint32_t address;
   uint32_t alt_bytes;
   uint8_t addr_bytes;    /* 1..4 */
   uint8_t alt_bytes_len; /* 1..4 */
   uint8_t dummy_cycles;  /* 0..31 */
   qspi_lines_t inst_lines;
   qspi_lines_t addr_lines;
   qspi_lines_t alt_lines;
   qspi_lines_t data_lines;
   uint32_t data_len;
} qspi_cmd_t;

/* SCLK = quadspi_ker_ck / (prescaler + 1). */
HAL_StatusTypeDef qspi_init(uint32_t prescaler, uint32_t fsize_log2,
                            uint32_t cs_high_cyc, uint32_t sample_shift);

HAL_StatusTypeDef qspi_xfer(const qspi_cmd_t *cmd, qspi_fmode_t fmode,
                            void *buf, uint32_t timeout_ms);

void qspi_abort(void);
int qspi_busy(void);

#endif
