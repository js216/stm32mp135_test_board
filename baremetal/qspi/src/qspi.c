// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file qspi.c
 * @brief QUADSPI bare-register driver for FPGA bring-up.  Indirect read /
 *        write, memory-mapped read, and auto-poll status modes.  Polled
 *        only; HAL is used for GPIO pin-mux and clock enables.
 *        DMA is intentionally not implemented.
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 */

#include "qspi.h"
#include "board.h"
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

uint32_t qspi_get_prescaler(void) { return cur_prescaler; }
uint32_t qspi_get_fsize(void)     { return cur_fsize; }
uint32_t qspi_get_csht(void)      { return cur_csht; }

static uint32_t lines_to_mode(qspi_lines_t l)
{
   switch (l) {
      case QSPI_LINES_1: return 1U;
      case QSPI_LINES_2: return 2U;
      case QSPI_LINES_4: return 3U;
      default: return 0U;
   }
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

int qspi_busy(void)
{
   return (QUADSPI->SR & QUADSPI_SR_BUSY) ? 1 : 0;
}

void qspi_abort(void)
{
   if (QUADSPI->SR & QUADSPI_SR_BUSY) {
      QUADSPI->CR |= QUADSPI_CR_ABORT;
      for (uint32_t i = 0; i < 100000U; i++) {
         if (!(QUADSPI->CR & QUADSPI_CR_ABORT))
            break;
      }
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
   cr |= (3U << QUADSPI_CR_FTHRES_Pos);
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

   if (cmd->data_len) {
      volatile uint8_t *const dr8 = (volatile uint8_t *)&QUADSPI->DR;
      uint8_t *bp                 = (uint8_t *)buf;
      uint32_t remaining          = cmd->data_len;
      while (remaining) {
         s = poll_flag(QUADSPI_SR_FTF, deadline);
         if (s != HAL_OK)
            return s;
         if (fmode == QSPI_FMODE_WRITE)
            *dr8 = *bp++;
         else
            *bp++ = *dr8;
         remaining--;
      }
   }

   s = poll_flag(QUADSPI_SR_TCF, deadline);
   if (s != HAL_OK)
      return s;
   QUADSPI->FCR = QUADSPI_FCR_CTCF;

   return wait_not_busy(deadline);
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
   ccr |= ((uint32_t)opcode) << CCR_INSTRUCTION_Pos;
   ccr |= 1U << CCR_IMODE_Pos;            /* opcode 1-lane */
   ccr |= 1U << CCR_ADMODE_Pos;           /* address 1-lane */
   ccr |= (3U - 1U) << CCR_ADSIZE_Pos;    /* 3-byte address */
   ccr |= lines_to_mode(data_lines) << CCR_DMODE_Pos;
   ccr |= (uint32_t)(dummy_cycles & 0x1FU) << CCR_DCYC_Pos;
   ccr |= ((uint32_t)QSPI_FMODE_READ) << CCR_FMODE_Pos;
   QUADSPI->CCR = ccr;
   QUADSPI->AR  = 0U;

   my_printf("BENCHDBG poll_kicked t=%lu sr=%08lx\r\n",
             (unsigned long)HAL_GetTick(),
             (unsigned long)QUADSPI->SR);

   volatile uint32_t *const dr32 = (volatile uint32_t *)&QUADSPI->DR;
   volatile uint8_t  *const dr8  = (volatile uint8_t  *)&QUADSPI->DR;
   uint32_t crc                  = 0xFFFFFFFFUL;
   uint32_t first_err            = 0xFFFFFFFFUL;
   uint8_t  got16[16]            = {0};
   const uint32_t t0             = HAL_GetTick();
   uint32_t wait_prints          = 0;
   uint32_t last_wait_t          = t0;
   uint32_t wait_iters           = 0;

   /* Drain in 32-bit words while at least 4 bytes are in the FIFO.
    * FTHRES=3 (set in qspi_init) -> FTF asserts at >= 4 bytes available,
    * so a single FTF poll covers a full word read.  Tail (< 4 bytes)
    * falls back to byte-serial. */
   uint32_t i = 0;
   while (i + 4U <= len) {
      while (!(QUADSPI->SR & QUADSPI_SR_FTF)) {
         wait_iters++;
         const uint32_t now = HAL_GetTick();
         if (wait_prints < 5U &&
             (int32_t)(now - last_wait_t) >= 200) {
            my_printf("BENCHDBG poll_wait t=%lu i=%lu sr=%08lx\r\n",
                      (unsigned long)now, (unsigned long)i,
                      (unsigned long)QUADSPI->SR);
            wait_prints++;
            last_wait_t = now;
         }
         if (QUADSPI->SR & QUADSPI_SR_TEF) {
            my_printf("BENCHDBG poll_tef sr=%08lx\r\n",
                      (unsigned long)QUADSPI->SR);
            qspi_abort();
            return HAL_ERROR;
         }
         if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
            my_printf("BENCHDBG poll_timeout t=%lu sr=%08lx i=%lu\r\n",
                      (unsigned long)HAL_GetTick(),
                      (unsigned long)QUADSPI->SR,
                      (unsigned long)i);
            qspi_abort();
            return HAL_TIMEOUT;
         }
      }
      const uint32_t w = *dr32;
      for (int k = 0; k < 4; k++) {
         const uint8_t b = (uint8_t)(w >> (k * 8));
         const uint32_t off = i + (uint32_t)k;
         if (off < 16U)
            got16[off] = b;
         crc ^= (uint32_t)b;
         for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
         const uint8_t exp = (uint8_t)(off & 0xFFU);
         if (b != exp && first_err == 0xFFFFFFFFUL)
            first_err = off;
      }
      i += 4U;
   }
   /* Drain the trailing 1..3 bytes byte-serial. */
   while (i < len) {
      while (!(QUADSPI->SR & QUADSPI_SR_FTF)) {
         if (QUADSPI->SR & QUADSPI_SR_TCF) break;  /* last bytes */
         if (QUADSPI->SR & QUADSPI_SR_TEF) {
            my_printf("BENCHDBG poll_tef sr=%08lx\r\n",
                      (unsigned long)QUADSPI->SR);
            qspi_abort();
            return HAL_ERROR;
         }
         if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
            my_printf("BENCHDBG poll_timeout t=%lu sr=%08lx i=%lu\r\n",
                      (unsigned long)HAL_GetTick(),
                      (unsigned long)QUADSPI->SR,
                      (unsigned long)i);
            qspi_abort();
            return HAL_TIMEOUT;
         }
      }
      const uint8_t b = *dr8;
      if (i < 16U)
         got16[i] = b;
      crc ^= (uint32_t)b;
      for (int j = 0; j < 8; j++)
         crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
      const uint8_t exp = (uint8_t)(i & 0xFFU);
      if (b != exp && first_err == 0xFFFFFFFFUL)
         first_err = i;
      i++;
   }

   while (!(QUADSPI->SR & QUADSPI_SR_TCF)) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         my_printf("BENCHDBG poll_timeout t=%lu sr=%08lx i=%lu\r\n",
                   (unsigned long)HAL_GetTick(),
                   (unsigned long)QUADSPI->SR,
                   (unsigned long)i);
         qspi_abort();
         return HAL_TIMEOUT;
      }
   }
   QUADSPI->FCR = QUADSPI_FCR_CTCF;
   while (QUADSPI->SR & QUADSPI_SR_BUSY) {
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         my_printf("BENCHDBG poll_timeout t=%lu sr=%08lx i=%lu\r\n",
                   (unsigned long)HAL_GetTick(),
                   (unsigned long)QUADSPI->SR,
                   (unsigned long)i);
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
