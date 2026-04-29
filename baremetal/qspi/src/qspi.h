// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file qspi.h
 * @brief QUADSPI bare-register driver for FPGA bring-up.
 *        Polled, indirect-mode primary path; memory-mapped and auto-poll
 *        modes are also exposed.  DMA is NOT implemented.
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#ifndef QSPI_H
#define QSPI_H

#include "stm32mp13xx_hal_def.h"
#include <stdbool.h>
#include <stdint.h>

/* Memory-mapped Bank 1 base (FMODE=11). */
#define QSPI_MM_BASE 0x70000000UL

typedef enum {
   QSPI_LINES_NONE = 0,
   QSPI_LINES_1    = 1,
   QSPI_LINES_2    = 2,
   QSPI_LINES_4    = 4,
} qspi_lines_t;

typedef enum {
   QSPI_FMODE_WRITE     = 0,
   QSPI_FMODE_READ      = 1,
   QSPI_FMODE_AUTOPOLL  = 2,
   QSPI_FMODE_MEMMAPPED = 3,
} qspi_fmode_t;

typedef struct {
   uint8_t instruction;
   uint32_t address;
   uint32_t alt_bytes;
   uint8_t addr_bytes;    /* 1..4 */
   uint8_t alt_bytes_len; /* 1..4 */
   uint8_t dummy_cycles;  /* 0..31 */
   bool ddr;              /* DDR mode (CCR.DDRM) */
   qspi_lines_t inst_lines;
   qspi_lines_t addr_lines;
   qspi_lines_t alt_lines;
   qspi_lines_t data_lines;
   uint32_t data_len;
} qspi_cmd_t;

/* SCLK = quadspi_ker_ck / (prescaler + 1). */
HAL_StatusTypeDef qspi_init(uint32_t prescaler, uint32_t fsize_log2,
                            uint32_t cs_high_cyc, uint32_t sample_shift,
                            bool dual_flash);

HAL_StatusTypeDef qspi_xfer(const qspi_cmd_t *cmd, qspi_fmode_t fmode,
                            void *buf, uint32_t timeout_ms);

/* Memory-mapped mode: program CCR with FMODE=11, peripheral auto-issues
 * flash reads when CPU dereferences QSPI_MM_BASE + offset.  No data_len
 * needed in the cmd. */
HAL_StatusTypeDef qspi_mm_enable(const qspi_cmd_t *cmd);
void qspi_mm_disable(void);

/* Auto-poll status: program PSMKR/PSMAR/PIR + FMODE=10, AND-match,
 * automatic stop on match (APMS=1). */
HAL_StatusTypeDef qspi_autopoll(const qspi_cmd_t *cmd, uint32_t mask,
                                uint32_t match, uint32_t interval_cycles,
                                uint32_t timeout_ms);

void qspi_abort(void);
int qspi_busy(void);

/* Streaming bench read: programs CCR/DLR/AR once, drains DR in 32-bit
 * words for `len` bytes (no buffer allocation), validates against
 * expected pattern `i & 0xFF` (incrementing).  Returns CRC32, first-
 * error offset (0xFFFFFFFF if all match), the first 16 bytes actually
 * received (got16, useful even when no error -- always populated), and
 * elapsed milliseconds. */
/* `raw=true` -> IMODE=0 + ADMODE=0: nothing on the wire except the
 * data-phase bytes themselves (matches a generic SPI slave that just
 * shifts bits on SCK).  In that case `opcode` is ignored. */
HAL_StatusTypeDef qspi_bench_read(uint8_t opcode, qspi_lines_t data_lines,
                                  uint8_t dummy_cycles, uint32_t len,
                                  bool raw,
                                  uint32_t *crc_out, uint32_t *first_err_out,
                                  uint8_t  *got16_out,
                                  uint32_t *elapsed_ms_out);

/* MDMA-driven streaming read: programs MDMA channel 0 with TSEL=0x1A
 * (QUADSPI FIFO threshold) so the QSPI FIFO drains directly into DDR
 * without any CPU loop.  Source = &QUADSPI->DR (address-fixed); dest =
 * `dst_addr` in DDR (incrementing).  CCR is programmed exactly like the
 * bench (FMODE=01, IMODE/ADMODE = 0 if `raw`, else IMODE=1 ADMODE=1
 * ADSIZE=2).  Polls MDMA CTC + QUADSPI TCF; aborts on timeout.  After
 * success the destination cache lines are invalidated so a subsequent
 * CPU read sees the freshly-DMA'd bytes. */
HAL_StatusTypeDef qspi_mdma_read(uint8_t opcode, qspi_lines_t data_lines,
                                 uint8_t dummy_cycles, uint32_t len,
                                 bool raw, uint32_t dst_addr,
                                 uint32_t timeout_ms, bool clean_before);

/* Accessors used by the CLI '?' command. */
uint32_t qspi_get_prescaler(void);
uint32_t qspi_get_fsize(void);
uint32_t qspi_get_csht(void);

#endif
