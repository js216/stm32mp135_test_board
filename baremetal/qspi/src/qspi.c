// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file qspi.c
 * @brief QUADSPI bare-register driver for FPGA bring-up.  Indirect read /
 *        write, memory-mapped read, and auto-poll status modes.  Polled
 *        only; HAL is used for GPIO pin-mux and clock enables.
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#include "qspi.h"
#include "board.h"
#include "core_ca.h"
#include "printf.h"
#include "stm32mp135fxx_ca7.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_def.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_rcc.h"
#include <stddef.h>
#include <stdint.h>

#define CCR_INSTRUCTION_Pos 0U
#define CCR_IMODE_Pos       8U
#define CCR_ADMODE_Pos      10U
#define CCR_ADSIZE_Pos      12U
#define CCR_ABMODE_Pos      14U
#define CCR_ABSIZE_Pos      16U
#define CCR_DCYC_Pos        18U
#define CCR_DMODE_Pos       24U
#define CCR_FMODE_Pos       26U

static int initialised;
static uint32_t cur_prescaler;
static uint32_t cur_fsize;
static uint32_t cur_csht;
static uint32_t cur_dlyb_sel;
static uint32_t cur_dlyb_unit;
static uint32_t mdma_active_dst;
static uint32_t mdma_active_len;
static bool mdma_active;

uint32_t qspi_get_prescaler(void) { return cur_prescaler; }
uint32_t qspi_get_fsize(void)     { return cur_fsize; }
uint32_t qspi_get_csht(void)      { return cur_csht; }

HAL_StatusTypeDef qspi_set_dlyb(uint32_t sel, uint32_t unit);

HAL_StatusTypeDef qspi_set_dlyb(uint32_t sel, uint32_t unit)
{
   if (sel > 15U || unit > 127U)
      return HAL_ERROR;
   cur_dlyb_sel  = sel;
   cur_dlyb_unit = sel ? unit : 0U;
   return HAL_OK;
}

static uint32_t lines_to_mode(qspi_lines_t l)
{
   switch (l) {
      case QSPI_LINES_1: return 1U;
      case QSPI_LINES_2: return 2U;
      case QSPI_LINES_4: return 3U;
      default: return 0U;
   }
}

static void bench_consume_byte(uint8_t b, uint32_t i, uint32_t *crc_io,
                               uint32_t *first_err_io, uint8_t got16[16])
{
   uint32_t crc = *crc_io;
   if (i < 16U)
      got16[i] = b;
   crc ^= (uint32_t)b;
   for (int j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
   if (b != (uint8_t)(i & 0xFFU) && *first_err_io == 0xFFFFFFFFUL)
      *first_err_io = i;
   *crc_io = crc;
}

static void setupgpio(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
   GPIO_InitTypeDef g = {
       .Pin       = pin,
       .Mode      = GPIO_MODE_AF_PP,
       .Pull      = GPIO_NOPULL,
       .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
       .Alternate = af,
   };
   HAL_GPIO_Init(port, &g);
}

static void setup_dlyb(void)
{
   DLYB_QUADSPI->CR = 0U;
   if (cur_dlyb_sel == 0U) {
      DLYB_QUADSPI->CFGR = 0U;
      return;
   }

   DLYB_QUADSPI->CR = DLYB_CR_DEN | DLYB_CR_SEN;
   DLYB_QUADSPI->CFGR =
      ((cur_dlyb_sel & 0x0FU) << DLYB_CFGR_SEL_Pos) |
      ((cur_dlyb_unit & 0x7FU) << DLYB_CFGR_UNIT_Pos);
   DLYB_QUADSPI->CR = DLYB_CR_DEN;
}

int qspi_busy(void)
{
   return (QUADSPI->SR & QUADSPI_SR_BUSY) ? 1 : 0;
}

void qspi_abort(void)
{
   /* Force ABORT unconditionally even when BUSY=0.  After an indirect
    * read whose DLR is not a multiple of (FTHRES+1) the receive FIFO
    * can hold up to FTHRES leftover bytes that MDMA never pulled (FTF
    * never re-asserts below threshold).  BUSY clears with the transfer
    * but those stale bytes survive into the next transaction, so the
    * next read returns shifted data starting with the residual.  ABORT
    * resets the FIFO per RM, so always run it. */
   QUADSPI->CR |= QUADSPI_CR_ABORT;
   for (uint32_t i = 0; i < 100000U; i++) {
      if (!(QUADSPI->CR & QUADSPI_CR_ABORT))
         break;
   }
   for (uint32_t i = 0; i < 100000U; i++) {
      if (!(QUADSPI->SR & QUADSPI_SR_BUSY))
         break;
   }
   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
}

HAL_StatusTypeDef qspi_init(uint32_t prescaler, uint32_t fsize_log2,
                            uint32_t cs_high_cyc, uint32_t sample_shift,
                            bool dual_flash)
{
   if (prescaler > 255U || fsize_log2 > 31U || cs_high_cyc > 7U ||
       sample_shift > 1U)
      return HAL_ERROR;

   __HAL_RCC_GPIOD_CLK_ENABLE();
   __HAL_RCC_GPIOE_CLK_ENABLE();
   __HAL_RCC_GPIOF_CLK_ENABLE();
   __HAL_RCC_GPIOH_CLK_ENABLE();

   __HAL_RCC_QSPI_CLK_ENABLE();
   __HAL_RCC_QSPI_FORCE_RESET();
   __HAL_RCC_QSPI_RELEASE_RESET();
   setup_dlyb();

   setupgpio(QSPI_CLK_PORT, QSPI_CLK_PIN, QSPI_CLK_AF);
   setupgpio(QSPI_NCS_PORT, QSPI_NCS_PIN, QSPI_NCS_AF);
   setupgpio(QSPI_IO0_PORT, QSPI_IO0_PIN, QSPI_IO0_AF);
   setupgpio(QSPI_IO1_PORT, QSPI_IO1_PIN, QSPI_IO1_AF);
   setupgpio(QSPI_IO2_PORT, QSPI_IO2_PIN, QSPI_IO2_AF);
   setupgpio(QSPI_IO3_PORT, QSPI_IO3_PIN, QSPI_IO3_AF);

   QUADSPI->CR &= ~QUADSPI_CR_EN;

   uint32_t cr = (prescaler & 0xFFU) << QUADSPI_CR_PRESCALER_Pos;
   if (sample_shift)
      cr |= QUADSPI_CR_SSHIFT;
   if (dual_flash)
      cr |= QUADSPI_CR_DFM;
#ifndef QSPI_FTHRES
#define QSPI_FTHRES 15U
#endif
   cr |= ((QSPI_FTHRES) << QUADSPI_CR_FTHRES_Pos);
   QUADSPI->CR = cr;

   QUADSPI->DCR = ((fsize_log2 & 0x1FU) << QUADSPI_DCR_FSIZE_Pos) |
                  ((cs_high_cyc & 0x07U) << QUADSPI_DCR_CSHT_Pos);

   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
   QUADSPI->CR |= QUADSPI_CR_EN;

   cur_prescaler = prescaler;
   cur_fsize     = fsize_log2;
   cur_csht      = cs_high_cyc;
   initialised   = 1;
   return HAL_OK;
}

static HAL_StatusTypeDef wait_not_busy(uint32_t deadline)
{
   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   return HAL_OK;
}

static uint32_t build_ccr(const qspi_cmd_t *cmd, qspi_fmode_t fmode)
{
   uint32_t ccr = 0;
   ccr |= ((uint32_t)cmd->instruction & 0xFFU) << CCR_INSTRUCTION_Pos;
   ccr |= lines_to_mode(cmd->inst_lines) << CCR_IMODE_Pos;
   ccr |= lines_to_mode(cmd->addr_lines) << CCR_ADMODE_Pos;
   ccr |= lines_to_mode(cmd->alt_lines) << CCR_ABMODE_Pos;
   ccr |= lines_to_mode(cmd->data_lines) << CCR_DMODE_Pos;
   if (cmd->addr_lines != QSPI_LINES_NONE && cmd->addr_bytes)
      ccr |= (uint32_t)(cmd->addr_bytes - 1U) << CCR_ADSIZE_Pos;
   if (cmd->alt_lines != QSPI_LINES_NONE && cmd->alt_bytes_len)
      ccr |= (uint32_t)(cmd->alt_bytes_len - 1U) << CCR_ABSIZE_Pos;
   ccr |= (uint32_t)(cmd->dummy_cycles & 0x1FU) << CCR_DCYC_Pos;
   ccr |= ((uint32_t)fmode & 0x3U) << CCR_FMODE_Pos;
   if (cmd->ddr)
      ccr |= QUADSPI_CCR_DDRM;
   return ccr;
}

static HAL_StatusTypeDef poll_flag(uint32_t mask, uint32_t deadline)
{
   for (;;) {
      const uint32_t sr = QUADSPI->SR;
      if (sr & mask)
         return HAL_OK;
      if (sr & QUADSPI_SR_TEF) {
         qspi_abort();
         return HAL_ERROR;
      }
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
}

static HAL_StatusTypeDef wait_read_data(uint32_t mask, uint32_t deadline)
{
   for (;;) {
      const uint32_t sr = QUADSPI->SR;
      if (sr & mask)
         return HAL_OK;
      if (sr & QUADSPI_SR_TEF) {
         qspi_abort();
         return HAL_ERROR;
      }
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
}

static HAL_StatusTypeDef read_dr_byte(uint32_t deadline, uint8_t *byte_out,
                                      uint32_t *wait_iters)
{
   for (;;) {
      const uint32_t sr = QUADSPI->SR;
      if (sr & QUADSPI_SR_FLEVEL) {
         *byte_out = *(volatile uint8_t *)&QUADSPI->DR;
         return HAL_OK;
      }
      if (wait_iters != NULL)
         (*wait_iters)++;
      if (sr & QUADSPI_SR_TEF) {
         qspi_abort();
         return HAL_ERROR;
      }
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
}

HAL_StatusTypeDef qspi_xfer(const qspi_cmd_t *cmd, qspi_fmode_t fmode,
                            void *buf, uint32_t timeout_ms)
{
   if (!initialised || cmd == NULL)
      return HAL_ERROR;
   if (cmd->data_len && buf == NULL && fmode != QSPI_FMODE_AUTOPOLL)
      return HAL_ERROR;
   if (cmd->addr_bytes > 4U || cmd->alt_bytes_len > 4U ||
       cmd->dummy_cycles > 31U)
      return HAL_ERROR;

   const uint32_t deadline = HAL_GetTick() + timeout_ms;

   HAL_StatusTypeDef s = wait_not_busy(deadline);
   if (s != HAL_OK)
      return s;

   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
   QUADSPI->DLR = cmd->data_len ? (cmd->data_len - 1U) : 0U;
   QUADSPI->ABR = cmd->alt_bytes;
   QUADSPI->CCR = build_ccr(cmd, fmode);
   if (cmd->addr_lines != QSPI_LINES_NONE)
      QUADSPI->AR = cmd->address;

   uint8_t qdr_bytes[16];
   uint32_t qdr_count = 0U;
   if (cmd->data_len) {
      volatile uint8_t  *const dr8  = (volatile uint8_t  *)&QUADSPI->DR;
      volatile uint32_t *const dr32 = (volatile uint32_t *)&QUADSPI->DR;
      uint8_t *bp                   = (uint8_t *)buf;
      uint32_t remaining            = cmd->data_len;
      const bool qdr_diag =
         (fmode == QSPI_FMODE_READ && cmd->data_lines == QSPI_LINES_4 &&
          (cmd->instruction == 0x6BU || cmd->instruction == 0x6DU));
      if (fmode == QSPI_FMODE_READ && cmd->data_lines == QSPI_LINES_4) {
         while (remaining) {
            uint8_t b;
            s = read_dr_byte(deadline, &b, NULL);
            if (s != HAL_OK)
               return s;
            *bp++ = b;
            if (qdr_diag && qdr_count < sizeof(qdr_bytes))
               qdr_bytes[qdr_count++] = b;
            remaining--;
         }
      } else if (fmode == QSPI_FMODE_READ) {
         while (remaining >= 4U) {
            s = wait_read_data(QUADSPI_SR_FTF | QUADSPI_SR_TCF, deadline);
            if (s != HAL_OK)
               return s;
            const uint32_t w = *dr32;
            bp[0] = (uint8_t)(w >> 0);
            bp[1] = (uint8_t)(w >> 8);
            bp[2] = (uint8_t)(w >> 16);
            bp[3] = (uint8_t)(w >> 24);
            if (qdr_diag) {
               for (uint32_t i = 0; i < 4U && qdr_count < sizeof(qdr_bytes);
                    i++)
                  qdr_bytes[qdr_count++] = bp[i];
            }
            bp += 4;
            remaining -= 4U;
         }
         if (remaining) {
            s = wait_read_data(QUADSPI_SR_TCF, deadline);
            if (s != HAL_OK)
               return s;
            const uint32_t w = *dr32;
            for (uint32_t i = 0; i < remaining; i++) {
               bp[i] = (uint8_t)(w >> (8U * i));
               if (qdr_diag && qdr_count < sizeof(qdr_bytes))
                  qdr_bytes[qdr_count++] = bp[i];
            }
            remaining = 0U;
         }
      } else {
         while (remaining) {
            s = poll_flag(QUADSPI_SR_FTF, deadline);
            if (s != HAL_OK)
               return s;
            *dr8 = *bp++;
            remaining--;
         }
      }
   }

   s = poll_flag(QUADSPI_SR_TCF, deadline);
   if (s != HAL_OK)
      return s;
   QUADSPI->FCR = QUADSPI_FCR_CTCF;

   s = wait_not_busy(deadline);
   if (s != HAL_OK)
      return s;

   if (qdr_count) {
      my_printf("qdr%02x", (unsigned)cmd->instruction);
      for (uint32_t i = 0; i < qdr_count; i++)
         my_printf(" %02x", (unsigned)qdr_bytes[i]);
      my_printf("\r\n");
   }

   return HAL_OK;
}

HAL_StatusTypeDef qspi_mm_enable(const qspi_cmd_t *cmd)
{
   if (!initialised || cmd == NULL)
      return HAL_ERROR;

   const uint32_t deadline = HAL_GetTick() + 100U;
   HAL_StatusTypeDef s     = wait_not_busy(deadline);
   if (s != HAL_OK)
      return s;

   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
   QUADSPI->ABR = cmd->alt_bytes;
   QUADSPI->CCR = build_ccr(cmd, QSPI_FMODE_MEMMAPPED);
   return HAL_OK;
}

void qspi_mm_disable(void)
{
   /* qspi_abort() is unconditional and resets FIFO + deasserts CS,
    * which is what mem-mapped tear-down also needs. */
   qspi_abort();
}

HAL_StatusTypeDef qspi_autopoll(const qspi_cmd_t *cmd, uint32_t mask,
                                uint32_t match, uint32_t interval_cycles,
                                uint32_t timeout_ms)
{
   if (!initialised || cmd == NULL)
      return HAL_ERROR;

   const uint32_t deadline = HAL_GetTick() + timeout_ms;
   HAL_StatusTypeDef s     = wait_not_busy(deadline);
   if (s != HAL_OK)
      return s;

   /* AND-match (PMM=0), automatic stop on match (APMS=1). */
   uint32_t cr = QUADSPI->CR;
   cr &= ~QUADSPI_CR_PMM;
   cr |= QUADSPI_CR_APMS;
   QUADSPI->CR = cr;

   QUADSPI->FCR   = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
   QUADSPI->PSMKR = mask;
   QUADSPI->PSMAR = match;
   QUADSPI->PIR   = interval_cycles & 0xFFFFU;
   QUADSPI->DLR   = cmd->data_len ? (cmd->data_len - 1U) : 0U;
   QUADSPI->ABR   = cmd->alt_bytes;
   QUADSPI->CCR   = build_ccr(cmd, QSPI_FMODE_AUTOPOLL);
   if (cmd->addr_lines != QSPI_LINES_NONE)
      QUADSPI->AR = cmd->address;

   while (!(QUADSPI->SR & QUADSPI_SR_SMF)) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
      if (QUADSPI->SR & QUADSPI_SR_TEF) {
         qspi_abort();
         return HAL_ERROR;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CSMF;
   /* APMS=1 auto-clears the transfer; just be safe. */
   qspi_abort();
   return HAL_OK;
}

HAL_StatusTypeDef qspi_bench_read(uint8_t opcode, qspi_lines_t data_lines,
                                  uint8_t dummy_cycles, uint32_t len,
                                  bool raw,
                                  uint32_t *crc_out, uint32_t *first_err_out,
                                  uint8_t  *got16_out,
                                  uint32_t *elapsed_ms_out)
{
   if (!initialised || len == 0)
      return HAL_ERROR;

   my_printf("BENCHDBG poll_enter t=%lu opcode=%02x lines=%u dummy=%u "
             "len=%lu\r\n",
             (unsigned long)HAL_GetTick(), (unsigned)opcode,
             (unsigned)data_lines, (unsigned)dummy_cycles,
             (unsigned long)len);

   const uint32_t deadline = HAL_GetTick() + 600000U;
   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
   QUADSPI->DLR = len - 1U;
   QUADSPI->ABR = 0;

   uint32_t ccr = 0;
   if (!raw) {
      ccr |= ((uint32_t)opcode) << CCR_INSTRUCTION_Pos;
      ccr |= 1U << CCR_IMODE_Pos;         /* opcode 1-lane */
      ccr |= 1U << CCR_ADMODE_Pos;        /* address 1-lane */
      ccr |= (3U - 1U) << CCR_ADSIZE_Pos; /* 3-byte address */
   }
   /* raw=true -> IMODE=0 + ADMODE=0: data phase only on the wire. */
   ccr |= lines_to_mode(data_lines) << CCR_DMODE_Pos;
   ccr |= (uint32_t)(dummy_cycles & 0x1FU) << CCR_DCYC_Pos;
   ccr |= ((uint32_t)QSPI_FMODE_READ) << CCR_FMODE_Pos;
   QUADSPI->CCR = ccr;
   if (!raw)
      QUADSPI->AR = 0U;

   uint32_t crc                  = 0xFFFFFFFFUL;
   uint32_t first_err            = 0xFFFFFFFFUL;
   uint8_t  got16[16]            = {0};
   const uint32_t t0             = HAL_GetTick();
   uint32_t wait_iters           = 0;

   uint32_t i = 0;

   if (data_lines == QSPI_LINES_4) {
      while (i < len) {
         uint8_t b;
         HAL_StatusTypeDef s = read_dr_byte(deadline, &b, &wait_iters);
         if (s != HAL_OK)
            return s;
         bench_consume_byte(b, i, &crc, &first_err, got16);
         i++;
      }
   } else {
      volatile uint32_t *const dr32 = (volatile uint32_t *)&QUADSPI->DR;

      while (i + 4U <= len) {
         while (!(QUADSPI->SR & (QUADSPI_SR_FTF | QUADSPI_SR_TCF))) {
            wait_iters++;
            if (QUADSPI->SR & QUADSPI_SR_TEF) {
               qspi_abort();
               return HAL_ERROR;
            }
            if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
               qspi_abort();
               return HAL_TIMEOUT;
            }
         }
         const uint32_t w = *dr32;
         for (uint32_t j = 0; j < 4U; j++) {
            bench_consume_byte((uint8_t)(w >> (8U * j)), i, &crc,
                               &first_err, got16);
            i++;
         }
      }

      if (i < len) {
         while (!(QUADSPI->SR & QUADSPI_SR_TCF)) {
            wait_iters++;
            if (QUADSPI->SR & QUADSPI_SR_TEF) {
               qspi_abort();
               return HAL_ERROR;
            }
            if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
               qspi_abort();
               return HAL_TIMEOUT;
            }
         }
         const uint32_t w = *dr32;
         for (uint32_t j = 0; i < len; j++, i++)
            bench_consume_byte((uint8_t)(w >> (8U * j)), i, &crc,
                               &first_err, got16);
      }
   }

   while (!(QUADSPI->SR & QUADSPI_SR_TCF)) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CTCF;
   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }

   *crc_out        = crc ^ 0xFFFFFFFFUL;
   *first_err_out  = first_err;
   for (int k = 0; k < 16; k++)
      got16_out[k] = got16[k];
   *elapsed_ms_out = HAL_GetTick() - t0;
   (void)wait_iters;
   my_printf("BENCHDBG poll_done t=%lu len=%lu firsterr=%ld\r\n",
             (unsigned long)HAL_GetTick(), (unsigned long)len,
             (first_err == 0xFFFFFFFFUL) ? -1L : (long)first_err);
   return HAL_OK;
}

/* MDMA channel 0 streaming read into DDR.  Direct register programming
 * (HAL not used) so we can pin TSEL = 0x1A (QUADSPI FIFO threshold) per
 * RM0475.  The destination must be 4-byte aligned and lie in DDR. */
HAL_StatusTypeDef qspi_mdma_start(uint8_t opcode, qspi_lines_t data_lines,
                                  uint8_t dummy_cycles, uint32_t len,
                                  bool raw, uint32_t dst_addr,
                                  uint32_t timeout_ms, bool clean_before)
{
   if (!initialised || len == 0U)
      return HAL_ERROR;
   if (dst_addr & 0x3U)
      return HAL_ERROR;
   if (mdma_active)
      return HAL_BUSY;

   /* Enable MDMA AHB6 clock.  Idempotent. */
   __HAL_RCC_MDMA_CLK_ENABLE();

   const uint32_t deadline = HAL_GetTick() + timeout_ms;

   /* Drain any prior QSPI state. */
   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;
   uint32_t cr = QUADSPI->CR;
   cr &= ~QUADSPI_CR_FTHRES_Msk;
   cr |= ((data_lines == QSPI_LINES_4 ? 3U : 15U)
          << QUADSPI_CR_FTHRES_Pos);
   QUADSPI->CR = cr;

   MDMA_Channel_TypeDef *const ch = MDMA_Channel0;

   /* Disable channel and clear any pending flags. */
   ch->CCR = 0U;
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF | MDMA_CIFCR_CBRTIF |
               MDMA_CIFCR_CBTIF | MDMA_CIFCR_CLTCIF;

   /* Clean+invalidate the destination cache range when requested so:
    *  (a) any dirty lines won't get written back over MDMA data, and
    *  (b) a subsequent CPU read picks up the fresh DDR contents.
    * The Cortex-A7 minimum L1 D-line is 32 B; loop is conservative. */
   const uint32_t line_sz = 32U;
   if (clean_before) {
      for (uint32_t a = dst_addr & ~(line_sz - 1U);
           a < dst_addr + len; a += line_sz)
         L1C_CleanInvalidateDCacheMVA((void *)a);
      __asm volatile("dsb sy" ::: "memory");
   }

   /* Number of bytes per buffer transfer. Choose 64 B = 16 beats of
    * 32-bit (matches AXI/AHB max burst length on this CPU bus).  TLEN
    * field is "bytes-1".  Override with -DMDMA_TLEN=<N> to probe the
    * per-buffer-vs-per-byte overhead model. */
#ifndef MDMA_TLEN
#define MDMA_TLEN 64U
#endif
#ifndef MDMA_SBURST
#define MDMA_SBURST 0U
#endif
   const uint32_t tlen = MDMA_TLEN;
   const uint32_t bndt = len; /* block size in bytes */

   /* CTCR:
    *   SINC = 0  (source address fixed, QUADSPI->DR)
    *   DINC = 2  (destination increments)
    *   SSIZE = 2 (32-bit reads from FIFO)
    *   DSIZE = 2 (32-bit writes to DDR)
    *   DINCOS = 2 (destination advances by 32-bit words)
    *   SBURST = 0 (single 32-bit read; QSPI FIFO is FTHRES=3 -> >=4 B)
    *   DBURST = 4 (16-beat burst write to DDR)
    *   TLEN  = tlen-1
    *   PKE = 0, PAM = 0 (matched sizes)
    *   TRGM = 0 (each request triggers ONE buffer transfer == TLEN bytes)
    *   SWRM = 0 (HW request)
    *   BWM  = 1 (bufferable writes -- DDR is normal cacheable)
    */
   const uint32_t ctcr =
       (0U  << MDMA_CTCR_SINC_Pos)   |
       (2U  << MDMA_CTCR_DINC_Pos)   |
       (2U  << MDMA_CTCR_SSIZE_Pos)  |
       (2U  << MDMA_CTCR_DSIZE_Pos)  |
       (2U  << MDMA_CTCR_DINCOS_Pos) |
       (MDMA_SBURST << MDMA_CTCR_SBURST_Pos) |
       (4U  << MDMA_CTCR_DBURST_Pos) |
       ((tlen - 1U) << MDMA_CTCR_TLEN_Pos) |
       MDMA_CTCR_BWM;

   /* CBNDTR.BNDT = block size in bytes (max 0x1FFFF = 128 KB - 1).
    * For multi-MB transfers, use block-repeat with 65536-byte blocks.
    * Decimal tails are handled by the caller as separate <=128 KB chunks. */
   if (len > 0x1FFFFU) {
      if (len & 0xFFFFU)
         return HAL_ERROR;
      const uint32_t blocks = len >> 16; /* len / 65536 */
      if (blocks == 0U || blocks > 0xFFFU + 1U)
         return HAL_ERROR;
      ch->CBNDTR = (65536U & MDMA_CBNDTR_BNDT_Msk) |
                   ((blocks - 1U) << MDMA_CBNDTR_BRC_Pos) |
                   MDMA_CBNDTR_BRDUM; /* dst increments across blocks */
      /* No source/destination block address offset (contiguous). */
      ch->CBRUR = 0U;
   } else {
      ch->CBNDTR = bndt & MDMA_CBNDTR_BNDT_Msk;
      ch->CBRUR  = 0U;
   }

   ch->CTCR = ctcr;
   ch->CSAR = (uint32_t)&QUADSPI->DR;
   ch->CDAR = dst_addr;
   /* CTBR.TSEL = 0x1A (QUADSPI FIFO threshold).  SBUS/DBUS = 0 (AXI). */
   ch->CTBR = 0x1AU & MDMA_CTBR_TSEL_Msk;
   ch->CMAR = 0U;
   ch->CMDR = 0U;

   /* Build QUADSPI CCR (FMODE=01 indirect read).  Match the bench's
    * raw / non-raw layout. */
   QUADSPI->DLR = len - 1U;
   QUADSPI->ABR = 0U;

   uint32_t ccr = 0;
   if (!raw) {
      ccr |= ((uint32_t)opcode) << CCR_INSTRUCTION_Pos;
      ccr |= 1U << CCR_IMODE_Pos;
      ccr |= 1U << CCR_ADMODE_Pos;
      ccr |= (3U - 1U) << CCR_ADSIZE_Pos;
   }
   ccr |= lines_to_mode(data_lines) << CCR_DMODE_Pos;
   ccr |= (uint32_t)(dummy_cycles & 0x1FU) << CCR_DCYC_Pos;
   ccr |= ((uint32_t)QSPI_FMODE_READ) << CCR_FMODE_Pos;

   /* Assert DMAEN before kicking the read. */
   QUADSPI->CR |= QUADSPI_CR_DMAEN;

   /* Enable MDMA channel BEFORE the QSPI request can fire. */
   ch->CCR = MDMA_CCR_EN | MDMA_CCR_PL;

   QUADSPI->CCR = ccr;
   if (!raw)
      QUADSPI->AR = 0U;

   mdma_active_dst = dst_addr;
   mdma_active_len = len;
   mdma_active = true;
   return HAL_OK;
}

HAL_StatusTypeDef qspi_mdma_crc_start(uint8_t opcode,
                                      qspi_lines_t data_lines,
                                      uint8_t dummy_cycles,
                                      uint32_t len,
                                      bool raw,
                                      uint32_t crc_dr_addr,
                                      uint32_t timeout_ms)
{
   if (!initialised || len == 0U)
      return HAL_ERROR;
   if (mdma_active)
      return HAL_BUSY;

   __HAL_RCC_MDMA_CLK_ENABLE();

   const uint32_t deadline = HAL_GetTick() + timeout_ms;
   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;

   uint32_t cr = QUADSPI->CR;
   cr &= ~QUADSPI_CR_FTHRES_Msk;
   cr |= ((data_lines == QSPI_LINES_4 ? 3U : 15U)
          << QUADSPI_CR_FTHRES_Pos);
   QUADSPI->CR = cr;

   MDMA_Channel_TypeDef *const ch = MDMA_Channel0;
   ch->CCR = 0U;
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF | MDMA_CIFCR_CBRTIF |
               MDMA_CIFCR_CBTIF | MDMA_CIFCR_CLTCIF;

   const uint32_t tlen = 64U;
   ch->CTCR =
       (0U << MDMA_CTCR_SINC_Pos) |
       (0U << MDMA_CTCR_DINC_Pos) |
       (0U << MDMA_CTCR_SSIZE_Pos) |
       (0U << MDMA_CTCR_DSIZE_Pos) |
       (0U << MDMA_CTCR_SINCOS_Pos) |
       (0U << MDMA_CTCR_DINCOS_Pos) |
       (0U << MDMA_CTCR_SBURST_Pos) |
       (0U << MDMA_CTCR_DBURST_Pos) |
       ((tlen - 1U) << MDMA_CTCR_TLEN_Pos);

   if (len > 0x1FFFFU) {
      if (len & 0xFFFFU)
         return HAL_ERROR;
      const uint32_t blocks = len >> 16;
      if (blocks == 0U || blocks > 0xFFFU + 1U)
         return HAL_ERROR;
      ch->CBNDTR = (65536U & MDMA_CBNDTR_BNDT_Msk) |
                   ((blocks - 1U) << MDMA_CBNDTR_BRC_Pos);
   } else {
      ch->CBNDTR = len & MDMA_CBNDTR_BNDT_Msk;
   }
   ch->CBRUR = 0U;
   ch->CSAR = (uint32_t)&QUADSPI->DR;
   ch->CDAR = crc_dr_addr;
   ch->CTBR = 0x1AU & MDMA_CTBR_TSEL_Msk;
   ch->CMAR = 0U;
   ch->CMDR = 0U;

   QUADSPI->DLR = len - 1U;
   QUADSPI->ABR = 0U;

   uint32_t ccr = 0;
   if (!raw) {
      ccr |= ((uint32_t)opcode) << CCR_INSTRUCTION_Pos;
      ccr |= 1U << CCR_IMODE_Pos;
      ccr |= 1U << CCR_ADMODE_Pos;
      ccr |= (3U - 1U) << CCR_ADSIZE_Pos;
   }
   ccr |= lines_to_mode(data_lines) << CCR_DMODE_Pos;
   ccr |= (uint32_t)(dummy_cycles & 0x1FU) << CCR_DCYC_Pos;
   ccr |= ((uint32_t)QSPI_FMODE_READ) << CCR_FMODE_Pos;

   QUADSPI->CR |= QUADSPI_CR_DMAEN;
   ch->CCR = MDMA_CCR_EN | MDMA_CCR_PL;

   QUADSPI->CCR = ccr;
   if (!raw)
      QUADSPI->AR = 0U;

   mdma_active_dst = 0U;
   mdma_active_len = 0U;
   mdma_active = true;
   return HAL_OK;
}

HAL_StatusTypeDef qspi_mdma_crc_read(uint8_t opcode,
                                     qspi_lines_t data_lines,
                                     uint8_t dummy_cycles,
                                     uint32_t len,
                                     bool raw,
                                     uint32_t crc_dr_addr,
                                     uint32_t timeout_ms)
{
   HAL_StatusTypeDef s = qspi_mdma_crc_start(opcode, data_lines,
                                             dummy_cycles, len, raw,
                                             crc_dr_addr, timeout_ms);
   if (s != HAL_OK)
      return s;
   return qspi_mdma_finish_no_inval(timeout_ms);
}

HAL_StatusTypeDef qspi_mdma_finish_no_inval(uint32_t timeout_ms)
{
   if (!mdma_active)
      return HAL_ERROR;

   MDMA_Channel_TypeDef *const ch = MDMA_Channel0;
   const uint32_t deadline = HAL_GetTick() + timeout_ms;

   /* Wait for MDMA channel transfer complete. Once CTCIF fires, all data
    * MDMA was programmed for is in DDR. We don't care about QSPI's TCF
    * because we'll abort QSPI right after. */
   while (!(ch->CISR & MDMA_CISR_CTCIF)) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         ch->CCR = 0U;
         QUADSPI->CR &= ~QUADSPI_CR_DMAEN;
         qspi_abort();
         mdma_active = false;
         return HAL_TIMEOUT;
      }
   }

   /* Tear down. Abort QSPI to stop any further clocking. */
   ch->CCR = 0U;
   QUADSPI->CR &= ~QUADSPI_CR_DMAEN;
   qspi_abort();
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF | MDMA_CIFCR_CBRTIF |
               MDMA_CIFCR_CBTIF | MDMA_CIFCR_CLTCIF;
   mdma_active = false;
   return HAL_OK;
}

void qspi_invalidate_range(uint32_t addr, uint32_t len)
{
   const uint32_t line_sz = 32U;
   for (uint32_t a = addr & ~(line_sz - 1U); a < addr + len; a += line_sz)
      L1C_InvalidateDCacheMVA((void *)a);
   __asm volatile("dsb sy" ::: "memory");
}

HAL_StatusTypeDef qspi_mdma_finish(uint32_t timeout_ms)
{
   const uint32_t dst = mdma_active_dst;
   const uint32_t len = mdma_active_len;
   HAL_StatusTypeDef s = qspi_mdma_finish_no_inval(timeout_ms);
   if (s == HAL_OK)
      qspi_invalidate_range(dst, len);
   return s;
}

HAL_StatusTypeDef qspi_mdma_read(uint8_t opcode, qspi_lines_t data_lines,
                                 uint8_t dummy_cycles, uint32_t len,
                                 bool raw, uint32_t dst_addr,
                                 uint32_t timeout_ms, bool clean_before)
{
   HAL_StatusTypeDef s = qspi_mdma_start(opcode, data_lines, dummy_cycles,
                                         len, raw, dst_addr, timeout_ms,
                                         clean_before);
   if (s != HAL_OK)
      return s;
   return qspi_mdma_finish(timeout_ms);
}

/* Variant of qspi_mdma_start that programs QUADSPI->AR with `flash_addr`
 * so chunked-read stitching can issue each chunk at its own slave
 * address. This duplicates qspi_mdma_start (instead of refactoring) so
 * the working baseline path stays bit-identical. */
static HAL_StatusTypeDef qspi_mdma_start_addr(uint8_t opcode,
                                              qspi_lines_t data_lines,
                                              uint8_t dummy_cycles,
                                              uint32_t len, bool raw,
                                              uint32_t flash_addr,
                                              uint32_t dst_addr,
                                              uint32_t timeout_ms,
                                              bool clean_before)
{
   if (!initialised || len == 0U)
      return HAL_ERROR;
   if (dst_addr & 0x3U)
      return HAL_ERROR;
   if (mdma_active)
      return HAL_BUSY;

   __HAL_RCC_MDMA_CLK_ENABLE();

   const uint32_t deadline = HAL_GetTick() + timeout_ms;

   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CSMF | QUADSPI_FCR_CTCF |
                  QUADSPI_FCR_CTEF;

   MDMA_Channel_TypeDef *const ch = MDMA_Channel0;
   ch->CCR = 0U;
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF | MDMA_CIFCR_CBRTIF |
               MDMA_CIFCR_CBTIF | MDMA_CIFCR_CLTCIF;

   const uint32_t line_sz = 32U;
   if (clean_before) {
      for (uint32_t a = dst_addr & ~(line_sz - 1U);
           a < dst_addr + len; a += line_sz)
         L1C_CleanInvalidateDCacheMVA((void *)a);
      __asm volatile("dsb sy" ::: "memory");
   }

   const uint32_t tlen = (len < 64U) ? len : 64U;
   const uint32_t bndt = len;

   const uint32_t ctcr =
       (0U  << MDMA_CTCR_SINC_Pos)   |
       (2U  << MDMA_CTCR_DINC_Pos)   |
       (2U  << MDMA_CTCR_SSIZE_Pos)  |
       (2U  << MDMA_CTCR_DSIZE_Pos)  |
       (2U  << MDMA_CTCR_DINCOS_Pos) |
       (0U  << MDMA_CTCR_SBURST_Pos) |
       (4U  << MDMA_CTCR_DBURST_Pos) |
       ((tlen - 1U) << MDMA_CTCR_TLEN_Pos) |
       MDMA_CTCR_BWM;

   if (len > 0x1FFFFU) {
      if (len & 0xFFFFU)
         return HAL_ERROR;
      const uint32_t blocks = len >> 16;
      if (blocks == 0U || blocks > 0xFFFU + 1U)
         return HAL_ERROR;
      ch->CBNDTR = (65536U & MDMA_CBNDTR_BNDT_Msk) |
                   ((blocks - 1U) << MDMA_CBNDTR_BRC_Pos) |
                   MDMA_CBNDTR_BRDUM;
      ch->CBRUR = 0U;
   } else {
      ch->CBNDTR = bndt & MDMA_CBNDTR_BNDT_Msk;
      ch->CBRUR  = 0U;
   }

   ch->CTCR = ctcr;
   ch->CSAR = (uint32_t)&QUADSPI->DR;
   ch->CDAR = dst_addr;
   ch->CTBR = 0x1AU & MDMA_CTBR_TSEL_Msk;
   ch->CMAR = 0U;
   ch->CMDR = 0U;

   QUADSPI->DLR = len - 1U;
   QUADSPI->ABR = 0U;

   uint32_t ccr = 0;
   if (!raw) {
      ccr |= ((uint32_t)opcode) << CCR_INSTRUCTION_Pos;
      ccr |= 1U << CCR_IMODE_Pos;
      ccr |= 1U << CCR_ADMODE_Pos;
      ccr |= (3U - 1U) << CCR_ADSIZE_Pos;
   }
   ccr |= lines_to_mode(data_lines) << CCR_DMODE_Pos;
   ccr |= (uint32_t)(dummy_cycles & 0x1FU) << CCR_DCYC_Pos;
   ccr |= ((uint32_t)QSPI_FMODE_READ) << CCR_FMODE_Pos;

   QUADSPI->CR |= QUADSPI_CR_DMAEN;
   ch->CCR = MDMA_CCR_EN | MDMA_CCR_PL;

   QUADSPI->CCR = ccr;
   if (!raw)
      QUADSPI->AR = flash_addr;

   mdma_active_dst = dst_addr;
   mdma_active_len = len;
   mdma_active = true;
   return HAL_OK;
}

HAL_StatusTypeDef qspi_mdma_read_addr(uint8_t opcode, qspi_lines_t data_lines,
                                      uint8_t dummy_cycles, uint32_t len,
                                      bool raw, uint32_t flash_addr,
                                      uint32_t dst_addr,
                                      uint32_t timeout_ms, bool clean_before)
{
   HAL_StatusTypeDef s = qspi_mdma_start_addr(opcode, data_lines,
                                              dummy_cycles, len, raw,
                                              flash_addr, dst_addr,
                                              timeout_ms, clean_before);
   if (s != HAL_OK)
      return s;
   return qspi_mdma_finish(timeout_ms);
}
