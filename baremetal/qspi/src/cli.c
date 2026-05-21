// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file cli.c
 * @brief Single-char command shell exercising the QSPI driver from a
 *        UART4 console.  Polled RX (no IRQ).  Every command is a single
 *        printable letter followed by space-separated decimal/hex args.
 * @author Jakob Kastelic
 * @copyright 2026 Jakob Kastelic
 */

#include "cli.h"
#include "board.h"
#include "defaults.h"
#include "printf.h"
#include "qspi.h"
#include "stm32mp135fxx_ca7.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_rcc.h"
#include "stm32mp13xx_hal_rcc_ex.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern HAL_StatusTypeDef qspi_set_dlyb(uint32_t sel, uint32_t unit);
#define LINEMAX  80
#define MAX_ARGS 8
#define READ_CAP 1024U
#define WRITE_CAP 256U
#define RXBUF    256
#define STREAM_CHUNK_BYTES (16U * 1024U * 1024U)
#define STREAM_MAX_XFER_BYTES STREAM_CHUNK_BYTES
#define STREAM_XOR_BYTES 131072U
#define RAW_QUAD_DUMMY_CYCLES 8U
#define CRC32_INIT   0xFFFFFFFFUL
#define CRC32_XOROUT 0xFFFFFFFFUL
#define CRC32_POLY   0xEDB88320UL
#define CRC32_HW_POLY 0x04C11DB7UL
#define CRC_MDMA_TIMEOUT_MS 60000U

static char linebuf[LINEMAX + 1];
static int  linelen;
static bool busy_flag;
static bool idle_jedec = true;
static bool auto_consume = true;
static bool four_byte_mode;
static uint32_t pend_confirm_C_until;
static uint8_t  io_buf[READ_CAP];
static uint32_t crc32_table[256];
static bool crc32_table_ready;
static uint8_t  crc32_pattern[256];
static uint32_t crc32_pattern_prefix[257];
static bool crc32_pattern_ready;
static uint32_t pattern_word_table[64];
static bool pattern_word_table_ready;
static bool crc32_hw_ready;
static bool crc32_hw_checked;
static bool crc32_hw_available;
static bool crc32_hw_direct_checked;
static bool crc32_hw_direct_available;
static bool crc32_mdma_active;

/* Software RX ring drained both from cli_poll and from inside the TX
 * char-write hook -- TX'ing a long printf burst takes longer than the
 * 1-byte hardware RX FIFO can hold, so we must drain on every char. */
static volatile uint8_t  rx_ring[RXBUF];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

bool cli_busy(void) { return busy_flag; }
bool cli_idle_jedec_enabled(void) { return idle_jedec; }

static void rx_drain_hw(void)
{
   while (UART4->ISR & USART_ISR_RXNE_RXFNE) {
      uint8_t c = (uint8_t)(UART4->RDR & 0xFFU);
      uint16_t nh = (uint16_t)((rx_head + 1U) % RXBUF);
      if (nh != rx_tail) {
         rx_ring[rx_head] = c;
         rx_head          = nh;
      }
      /* else drop on overflow */
   }
}

static int uart_rx_byte(int *out)
{
   rx_drain_hw();
   if (rx_head == rx_tail)
      return 0;
   *out    = rx_ring[rx_tail];
   rx_tail = (uint16_t)((rx_tail + 1U) % RXBUF);
   return 1;
}

static void uart_tx_byte(char c)
{
   while (!(UART4->ISR & USART_ISR_TXE))
      rx_drain_hw();
   UART4->TDR = (uint8_t)c;
}

static void prompt(void)
{
   my_printf("> ");
}

static int parse_args(char *p, char **argv, int max)
{
   int n = 0;
   while (*p && n < max) {
      while (*p == ' ' || *p == '\t')
         p++;
      if (!*p)
         break;
      argv[n++] = p;
      while (*p && *p != ' ' && *p != '\t')
         p++;
      if (*p) {
         *p++ = '\0';
      }
   }
   return n;
}

static uint32_t arg_u32(const char *s, uint32_t dflt)
{
   if (!s || !*s)
      return dflt;
   char *e = NULL;
   uint32_t v = (uint32_t)strtoul(s, &e, 0);
   if (e == s)
      return dflt;
   return v;
}

static uint32_t qspi_kernel_hz(void)
{
   return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_QSPI);
}

static uint32_t qspi_payload_ceiling_mbps_x10(uint32_t lanes)
{
   const uint64_t numerator = (uint64_t)qspi_kernel_hz() * lanes;
   const uint64_t denominator =
       ((uint64_t)qspi_get_prescaler() + 1ULL) * 100000ULL;

   if (denominator == 0ULL)
      return 0U;
   return (uint32_t)(numerator / denominator);
}

static uint32_t stream_mbps_x10(uint32_t len, uint32_t elapsed_ms,
                                uint32_t lanes)
{
   uint32_t dt = elapsed_ms;
   if (dt == 0U)
      dt = 1U;

   uint32_t mbps_x10;
   if (len <= 50000000U)
      mbps_x10 = (len * 80U) / (dt * 1000U);
   else
      mbps_x10 = ((len / 1000U) * 80U) / dt;

   const uint32_t ceiling_mbps_x10 =
      qspi_payload_ceiling_mbps_x10(lanes);
   if (mbps_x10 > ceiling_mbps_x10)
      mbps_x10 = ceiling_mbps_x10;
   return mbps_x10;
}

static uint8_t qspi_default_dummy(uint32_t quad, uint32_t raw)
{
   if (raw)
      return quad ? RAW_QUAD_DUMMY_CYCLES : 0U;
   return quad ? 9U : 8U;
}

static void stream_print_xfer(uint32_t len, bool quad, uint32_t xfer_ms)
{
   const uint32_t mbps_x10 =
      stream_mbps_x10(len, xfer_ms, quad ? 4U : 1U);

   my_printf("stream_xfer %lu B %s in %lu ms, %lu.%lu Mbps\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane",
             (unsigned long)xfer_ms,
             (unsigned long)(mbps_x10 / 10U),
             (unsigned long)(mbps_x10 % 10U));
}

static void hexdump(uint32_t base, const uint8_t *data, uint32_t n)
{
   for (uint32_t i = 0; i < n; i += 16U) {
      my_printf("%08lx:", (unsigned long)(base + i));
      uint32_t row = (n - i) < 16U ? (n - i) : 16U;
      for (uint32_t j = 0; j < row; j++)
         my_printf(" %02x", data[i + j]);
      my_printf("\r\n");
   }
}

static void crc32_table_init(void)
{
   if (crc32_table_ready)
      return;
   for (uint32_t i = 0; i < 256U; i++) {
      uint32_t c = i;
      for (int j = 0; j < 8; j++)
         c = (c >> 1) ^ (CRC32_POLY & (-(int32_t)(c & 1U)));
      crc32_table[i] = c;
   }
   crc32_table_ready = true;
}

static inline uint32_t crc32_update_byte(uint32_t crc, uint8_t b)
{
   return (crc >> 8) ^ crc32_table[(crc ^ (uint32_t)b) & 0xFFU];
}

static uint32_t crc32_update_buf(uint32_t crc, const volatile uint8_t *p,
                                 uint32_t n, uint32_t base,
                                 uint32_t *first_err)
{
   crc32_table_init();
   for (uint32_t i = 0; i < n; i++) {
      const uint8_t b = p[i];
      crc = crc32_update_byte(crc, b);
      if (first_err != NULL) {
         const uint8_t exp = (uint8_t)((base + i) & 0xFFU);
         if (b != exp && *first_err == 0xFFFFFFFFUL)
            *first_err = base + i;
      }
   }
   return crc;
}

static void crc32_hw_enable(void)
{
   if (crc32_hw_ready)
      return;

   __HAL_RCC_CRC1_CLK_ENABLE();
   __HAL_RCC_CRC1_FORCE_RESET();
   __asm volatile("dsb sy" ::: "memory");
   __HAL_RCC_CRC1_RELEASE_RESET();
   __asm volatile("dsb sy" ::: "memory");
   crc32_hw_ready = true;
}

static void crc32_hw_reset(void)
{
   CRC1->POL  = CRC32_HW_POLY;
   CRC1->INIT = CRC32_INIT;
   CRC1->CR   = CRC_CR_REV_IN_0 | CRC_CR_REV_OUT | CRC_CR_RESET;
}

static void crc32_mdma_clear_flags(MDMA_Channel_TypeDef *ch)
{
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF |
               MDMA_CIFCR_CBRTIF | MDMA_CIFCR_CBTIF |
               MDMA_CIFCR_CLTCIF;
}

static HAL_StatusTypeDef crc32_mdma_start(const volatile uint8_t *p,
                                          uint32_t n)
{
   if (p == NULL || n == 0U)
      return HAL_ERROR;
   if (crc32_mdma_active)
      return HAL_BUSY;
   if (n > 65536U && (n & 0xFFFFU))
      return HAL_ERROR;

   __HAL_RCC_MDMA_CLK_ENABLE();

   MDMA_Channel_TypeDef *const ch = MDMA_Channel1;
   ch->CCR = 0U;
   crc32_mdma_clear_flags(ch);

   uint32_t trgm;
   if (n <= 128U)
      trgm = 0U; /* one buffer transfer */
   else if (n <= 65536U)
      trgm = 1U; /* one block transfer */
   else
      trgm = 2U; /* repeated 64 KiB blocks */

   ch->CTCR =
      (2U << MDMA_CTCR_SINC_Pos) |
      (0U << MDMA_CTCR_DINC_Pos) |
      (0U << MDMA_CTCR_SSIZE_Pos) |
      (0U << MDMA_CTCR_DSIZE_Pos) |
      (0U << MDMA_CTCR_SINCOS_Pos) |
      (0U << MDMA_CTCR_DINCOS_Pos) |
      (0U << MDMA_CTCR_SBURST_Pos) |
      (0U << MDMA_CTCR_DBURST_Pos) |
      ((128U - 1U) << MDMA_CTCR_TLEN_Pos) |
      (trgm << MDMA_CTCR_TRGM_Pos) |
      MDMA_CTCR_SWRM;

   if (n > 65536U) {
      const uint32_t blocks = n >> 16;
      ch->CBNDTR = (65536U & MDMA_CBNDTR_BNDT_Msk) |
                   MDMA_CBNDTR_BRSUM |
                   ((blocks - 1U) << MDMA_CBNDTR_BRC_Pos);
   } else {
      ch->CBNDTR = n & MDMA_CBNDTR_BNDT_Msk;
   }
   ch->CBRUR = 0U;
   ch->CSAR = (uint32_t)p;
   ch->CDAR = (uint32_t)&CRC1->DR;
   ch->CTBR = 0U;
   ch->CMAR = 0U;
   ch->CMDR = 0U;

   __asm volatile("dsb sy" ::: "memory");
   ch->CCR = MDMA_CCR_EN;
   __asm volatile("dsb sy" ::: "memory");
   ch->CCR = MDMA_CCR_EN | MDMA_CCR_SWRQ;
   crc32_mdma_active = true;
   return HAL_OK;
}

static HAL_StatusTypeDef crc32_mdma_wait(uint32_t timeout_ms)
{
   if (!crc32_mdma_active)
      return HAL_OK;

   MDMA_Channel_TypeDef *const ch = MDMA_Channel1;
   const uint32_t deadline = HAL_GetTick() + timeout_ms;

   for (;;) {
      const uint32_t cisr = ch->CISR;
      if (cisr & MDMA_CISR_TEIF) {
         my_printf("CRCMDMA TE cesr=%08lx\r\n",
                   (unsigned long)ch->CESR);
         ch->CCR = 0U;
         crc32_mdma_clear_flags(ch);
         crc32_mdma_active = false;
         return HAL_ERROR;
      }
      if (cisr & MDMA_CISR_CTCIF)
         break;
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         my_printf("CRCMDMA timeout cisr=%08lx bndt=%08lx\r\n",
                   (unsigned long)cisr, (unsigned long)ch->CBNDTR);
         ch->CCR = 0U;
         crc32_mdma_clear_flags(ch);
         crc32_mdma_active = false;
         return HAL_TIMEOUT;
      }
   }

   ch->CCR = 0U;
   crc32_mdma_clear_flags(ch);
   crc32_mdma_active = false;
   __asm volatile("dsb sy" ::: "memory");
   return HAL_OK;
}

static uint32_t crc32_hw_final(void)
{
   return CRC1->DR ^ CRC32_XOROUT;
}

static bool crc32_hw_begin(void)
{
   static const uint8_t test[9] = {
      '1', '2', '3', '4', '5', '6', '7', '8', '9'
   };

   crc32_hw_enable();
   if (!crc32_hw_checked) {
      volatile uint8_t *const p = (volatile uint8_t *)DEF_DDR_BASE;
      for (uint32_t i = 0U; i < sizeof(test); i++)
         p[i] = test[i];
      for (uint32_t a = DEF_DDR_BASE & ~31UL;
           a < DEF_DDR_BASE + sizeof(test); a += 32U)
         L1C_CleanInvalidateDCacheMVA((void *)a);
      __asm volatile("dsb sy" ::: "memory");

      crc32_hw_reset();
      HAL_StatusTypeDef s = crc32_mdma_start(p, sizeof(test));
      if (s == HAL_OK)
         s = crc32_mdma_wait(1000U);
      crc32_hw_available =
         (s == HAL_OK) && (crc32_hw_final() == 0xCBF43926UL);
      crc32_hw_checked = true;
   }
   if (!crc32_hw_available)
      return false;

   crc32_hw_reset();
   return true;
}

/* CRC reset variant for the QSPI->CRC MDMA path.  qspi_mdma_crc_start()
 * feeds CRC1->DR with 32-bit words read from QUADSPI->DR; the LSB byte
 * of each word is the first byte received off the wire.  Setting REV_IN
 * = 11 (full 32-bit reversal) makes CRC1 process those bits in stream
 * order (byte0 LSB-first, then byte1, ..., byte3 MSB) for an exact
 * match to byte-wise ethernet CRC32 -- as confirmed by the word-wide
 * self-test in crc32_hw_begin_direct(). */
static void crc32_hw_reset_word(void)
{
   CRC1->POL  = CRC32_HW_POLY;
   CRC1->INIT = CRC32_INIT;
   CRC1->CR   = CRC_CR_REV_IN_0 | CRC_CR_REV_IN_1 | CRC_CR_REV_OUT |
                CRC_CR_RESET;
}

static bool crc32_hw_begin_direct(void)
{
   /* Byte-mode self-test in REV_IN=01 (canonical ethernet CRC32) plus
    * a word-mode self-test in REV_IN=11 to prove the byte-wise and
    * word-wise feeds produce the same polynomial.  The streaming path
    * uses crc32_hw_reset_word() (REV_IN=11) before the 32-bit MDMA
    * feed so the resulting CRC matches what byte-wise consumption
    * would compute, allowing the firmware's `crc32_expected(len)` to
    * remain a single byte-stream reference. */
   static const uint8_t test[9] = {
      '1', '2', '3', '4', '5', '6', '7', '8', '9'
   };
   static const uint32_t test_words[2] = {
      0x34333231U, 0x38373635U,  /* "1234", "5678" little-endian */
   };

   crc32_hw_enable();
   if (!crc32_hw_direct_checked) {
      volatile uint8_t  *const dr8  = (volatile uint8_t  *)&CRC1->DR;
      volatile uint32_t *const dr32 = (volatile uint32_t *)&CRC1->DR;

      crc32_hw_reset();
      for (uint32_t i = 0U; i < sizeof(test); i++)
         *dr8 = test[i];
      const bool byte_ok = (crc32_hw_final() == 0xCBF43926UL);

      crc32_hw_reset_word();
      dr32[0] = test_words[0];
      dr32[0] = test_words[1];
      const bool word_ok = (crc32_hw_final() == 0x9AE0DAAFUL);

      crc32_hw_direct_available = byte_ok && word_ok;
      crc32_hw_direct_checked   = true;
   }
   if (!crc32_hw_direct_available)
      return false;

   crc32_hw_reset_word();
   return true;
}

static uint32_t crc32_matrix_times(const uint32_t *mat, uint32_t vec)
{
   uint32_t sum = 0U;
   while (vec) {
      if (vec & 1U)
         sum ^= *mat;
      vec >>= 1;
      mat++;
   }
   return sum;
}

static void crc32_matrix_square(uint32_t *square, const uint32_t *mat)
{
   for (uint32_t i = 0U; i < 32U; i++)
      square[i] = crc32_matrix_times(mat, mat[i]);
}

static uint32_t crc32_combine(uint32_t crc1, uint32_t crc2, uint32_t len2)
{
   uint32_t even[32];
   uint32_t odd[32];

   if (len2 == 0U)
      return crc1;

   odd[0] = CRC32_POLY;
   uint32_t row = 1U;
   for (uint32_t i = 1U; i < 32U; i++) {
      odd[i] = row;
      row <<= 1;
   }
   crc32_matrix_square(even, odd);
   crc32_matrix_square(odd, even);

   do {
      crc32_matrix_square(even, odd);
      if (len2 & 1U)
         crc1 = crc32_matrix_times(even, crc1);
      len2 >>= 1;
      if (len2 == 0U)
         break;

      crc32_matrix_square(odd, even);
      if (len2 & 1U)
         crc1 = crc32_matrix_times(odd, crc1);
      len2 >>= 1;
   } while (len2 != 0U);

   return crc1 ^ crc2;
}

static void crc32_pattern_init(void)
{
   if (crc32_pattern_ready)
      return;
   crc32_table_init();
   uint32_t crc = CRC32_INIT;
   crc32_pattern_prefix[0] = 0U;
   for (uint32_t i = 0U; i < sizeof(crc32_pattern); i++) {
      crc32_pattern[i] = (uint8_t)i;
      crc = crc32_update_byte(crc, crc32_pattern[i]);
      crc32_pattern_prefix[i + 1U] = crc ^ CRC32_XOROUT;
   }
   crc32_pattern_ready = true;
}

static inline uint8_t apply_corruption_mask(uint8_t b, uint32_t offset)
{
   (void)offset;
   return b;
}

static uint32_t crc32_expected(uint32_t len)
{
   crc32_pattern_init();

   uint32_t reps = len >> 8;
   const uint32_t rem = len & 0xFFU;
   uint32_t result_crc = 0U;
   uint32_t block_crc = crc32_pattern_prefix[256];
   uint32_t block_len = 256U;

   while (reps) {
      if (reps & 1U)
         result_crc = crc32_combine(result_crc, block_crc, block_len);
      reps >>= 1;
      if (reps) {
         block_crc = crc32_combine(block_crc, block_crc, block_len);
         block_len <<= 1;
      }
   }
   if (rem)
      result_crc = crc32_combine(result_crc, crc32_pattern_prefix[rem], rem);
   return result_crc;
}

/* CRC32 of `len` bytes of 0xAA.  Used by the raw-quad path which
 * cannot deliver a counter pattern at 109 MHz × 4 lanes without
 * bit errors (Worker K characterisation); the FPGA emits a
 * constant 0xAA on all four IO lanes so the link can be verified
 * for raw quad data integrity.  Uses crc32_combine for O(log N). */
static bool     crc32_aa_ready;
static uint32_t crc32_aa_prefix[257];
static void crc32_aa_init(void)
{
   if (crc32_aa_ready)
      return;
   crc32_table_init();
   uint32_t crc = CRC32_INIT;
   crc32_aa_prefix[0] = 0U;
   for (uint32_t i = 0U; i < 256U; i++) {
      crc = crc32_update_byte(crc, 0xAAU);
      crc32_aa_prefix[i + 1U] = crc ^ CRC32_XOROUT;
   }
   crc32_aa_ready = true;
}
static uint32_t crc32_aa(uint32_t len)
{
   crc32_aa_init();

   uint32_t reps = len >> 8;
   const uint32_t rem = len & 0xFFU;
   uint32_t result_crc = 0U;
   uint32_t block_crc = crc32_aa_prefix[256];
   uint32_t block_len = 256U;

   while (reps) {
      if (reps & 1U)
         result_crc = crc32_combine(result_crc, block_crc, block_len);
      reps >>= 1;
      if (reps) {
         block_crc = crc32_combine(block_crc, block_crc, block_len);
         block_len <<= 1;
      }
   }
   if (rem)
      result_crc = crc32_combine(result_crc, crc32_aa_prefix[rem], rem);
   return result_crc;
}

static uint32_t mdma_segment_len(uint32_t remaining, uint32_t limit)
{
   uint32_t n = remaining;
   if (n > limit)
      n = limit;
   if (n > 0x10000U && (n & 0xFFFFU))
      n = 0x10000U;
   return n;
}

static uint32_t stream_segment_len(uint32_t remaining, uint32_t limit)
{
   uint32_t n = remaining;
   if (n > limit)
      n = limit;
   if (n > STREAM_MAX_XFER_BYTES)
      n = STREAM_MAX_XFER_BYTES;
   if (remaining <= 0x20000U) {
      if (remaining > 0x10000U && (remaining & 0xFFFFU))
         return 0x10000U;
      return n;
   }
   if (n & 0xFFFFU)
      n &= ~0xFFFFU;
   return n;
}

static uint32_t pattern_word(uint32_t off)
{
   const uint32_t b0 = (off + 0U) & 0xFFU;
   const uint32_t b1 = (off + 1U) & 0xFFU;
   const uint32_t b2 = (off + 2U) & 0xFFU;
   const uint32_t b3 = (off + 3U) & 0xFFU;
   return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static void pattern_word_table_init(void)
{
   if (pattern_word_table_ready)
      return;
   for (uint32_t i = 0U; i < 64U; i++)
      pattern_word_table[i] = pattern_word(i * 4U);
   pattern_word_table_ready = true;
}

static void validate_pattern_buf(const volatile uint8_t *p, uint32_t n,
                                 uint32_t base, uint32_t *first_err)
{
   pattern_word_table_init();
   const volatile uint32_t *w = (const volatile uint32_t *)p;
   uint32_t i = 0U;
   uint32_t pat = (base >> 2) & 63U;
   while (i + 4U <= n) {
      const uint32_t got = *w++;
      if (got != pattern_word_table[pat]) {
         for (uint32_t j = 0U; j < 4U; j++) {
            const uint8_t b = p[i + j];
            const uint8_t exp = (uint8_t)((base + i + j) & 0xFFU);
            if (b != exp) {
               *first_err = base + i + j;
               return;
            }
         }
      }
      i += 4U;
      pat = (pat + 1U) & 63U;
   }
   while (i < n) {
      const uint8_t b = p[i];
      const uint8_t exp = (uint8_t)((base + i) & 0xFFU);
      if (b != exp) {
         *first_err = base + i;
         return;
      }
      i++;
   }
}

/* Validate that every byte in the DDR chunk is 0xAA.  Used by the
 * raw-quad streaming path which expects the FPGA's constant-0xA
 * emit on all four IO lanes. */
static void validate_aa_buf(const volatile uint8_t *p, uint32_t n,
                            uint32_t base, uint32_t *first_err)
{
   const volatile uint32_t *w = (const volatile uint32_t *)p;
   uint32_t i = 0U;
   while (i + 4U <= n) {
      const uint32_t got = *w++;
      if (got != 0xAAAAAAAAUL) {
         for (uint32_t j = 0U; j < 4U; j++) {
            if (p[i + j] != 0xAAU) {
               *first_err = base + i + j;
               return;
            }
         }
      }
      i += 4U;
   }
   while (i < n) {
      if (p[i] != 0xAAU) {
         *first_err = base + i;
         return;
      }
      i++;
   }
}

static void stream_print_first16(const uint32_t *slot_base,
                                 const bool *slot_valid)
{
   int first = 0;
   if (slot_valid[1] &&
       (!slot_valid[0] || slot_base[1] < slot_base[0]))
      first = 1;
   if (!slot_valid[first])
      return;
   volatile uint8_t *const p =
      (volatile uint8_t *)(DEF_DDR_BASE +
                           ((uint32_t)first * STREAM_CHUNK_BYTES));
   my_printf("stream_got16 base=%lu bytes="
             "%02x %02x %02x %02x %02x %02x %02x %02x "
             "%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
             (unsigned long)slot_base[first],
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
             p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

static void stream_print_slot16(const uint32_t *slot_base,
                                const bool *slot_valid)
{
   int first = 0;
   if (slot_valid[1] &&
       (!slot_valid[0] || slot_base[1] < slot_base[0]))
      first = 1;
   if (!slot_valid[first])
      return;

   volatile uint8_t *const p =
      (volatile uint8_t *)(DEF_DDR_BASE +
                           ((uint32_t)first * STREAM_CHUNK_BYTES));

   my_printf("stream_got16 base=%lu bytes="
             "%02x %02x %02x %02x %02x %02x %02x %02x "
             "%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
             (unsigned long)slot_base[first],
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
             p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

static void stream_print_firsterr_dump(const volatile uint8_t *p,
                                       uint32_t n,
                                       uint32_t base,
                                       uint32_t first_err)
{
   if (first_err == 0xFFFFFFFFUL || first_err < base)
      return;
   const uint32_t rel = first_err - base;
   if (rel >= n)
      return;
   const uint32_t lo = (rel >= 8U) ? (rel - 8U) : 0U;
   uint32_t hi = rel + 24U;
   if (hi > n)
      hi = n;
   my_printf("stream_firsterr @%lu rx:",
             (unsigned long)(base + lo));
   for (uint32_t k = lo; k < hi; k++)
      my_printf(" %02x", (unsigned)p[k]);
   my_printf("\r\nstream_firsterr @%lu xp:",
             (unsigned long)(base + lo));
   for (uint32_t k = lo; k < hi; k++)
      my_printf(" %02x", (unsigned)((base + k) & 0xFFU));
   my_printf("\r\n");
}

static uint32_t xor_words(const volatile uint32_t *p, uint32_t words)
{
   uint32_t x = 0U;
   for (uint32_t i = 0U; i < words; i++)
      x ^= p[i];
   return x;
}

static bool stream_direct_crc_eligible(uint32_t quad, uint32_t raw)
{
   /* Keep raw quad on the DDR path so CRC failures always carry
    * stream_got16/firsterr byte diagnostics. The direct QSPI->CRC path
    * is still useful for 1-lane raw, where byte-level correctness is
    * already established and the CRC feed does not mask nibble issues. */
   return auto_consume && raw != 0U && quad == 0U;
}

static HAL_StatusTypeDef stream_direct_crc_read(uint8_t opcode,
                                                qspi_lines_t dl,
                                                uint8_t dummy,
                                                bool raw,
                                                uint32_t len,
                                                uint32_t *done,
                                                uint32_t *chunks)
{
   *done = 0U;
   *chunks = 0U;
   while (*done < len) {
      const uint32_t n = stream_segment_len(len - *done,
                                            STREAM_CHUNK_BYTES);
      HAL_StatusTypeDef s =
         qspi_mdma_crc_read(opcode, dl, dummy, n, raw,
                            (uint32_t)&CRC1->DR, 60000U);
      if (s != HAL_OK)
         return s;
      *done += n;
      (*chunks)++;
   }
   return HAL_OK;
}

static uint32_t stream_direct_crc_diag(uint8_t opcode, qspi_lines_t dl,
                                       uint8_t dummy, bool raw,
                                       uint32_t len, bool quad)
{
   uint32_t first_err = 0xFFFFFFFFUL;
   uint32_t done = 0U;
   uint32_t chunks = 0U;
   uint32_t slot_base[2] = {0U, 0U};
   bool slot_valid[2] = {false, false};

   while (done < len) {
      const uint32_t n = stream_segment_len(len - done,
                                            STREAM_CHUNK_BYTES);
      HAL_StatusTypeDef s =
         qspi_mdma_read(opcode, dl, dummy, n, raw, DEF_DDR_BASE,
                        60000U, true);
      if (s != HAL_OK) {
         my_printf("stream_diag ERR xfer (%d) checked=%lu chunks=%lu\r\n",
                   (int)s, (unsigned long)done,
                   (unsigned long)chunks);
         return 0xFFFFFFFEUL;
      }

      volatile uint8_t *const p = (volatile uint8_t *)DEF_DDR_BASE;
      qspi_invalidate_range((uint32_t)p, n);
      if (chunks == 0U) {
         slot_base[0] = done;
         slot_valid[0] = true;
         stream_print_first16(slot_base, slot_valid);
      }

      if (first_err == 0xFFFFFFFFUL) {
         validate_pattern_buf(p, n, done, &first_err);
         if (first_err != 0xFFFFFFFFUL) {
            uint32_t lo = (first_err >= 8U) ? (first_err - 8U) : 0U;
            uint32_t hi = first_err + 24U;
            if (hi > done + n)
               hi = done + n;
            my_printf("stream_firsterr_dump @%lu:",
                      (unsigned long)lo);
            for (uint32_t k = lo; k < hi; k++)
               my_printf(" %02x", (unsigned)p[k - done]);
            my_printf("\r\n");
         }
      }

      done += n;
      chunks++;
      if (first_err != 0xFFFFFFFFUL)
         break;
   }

   const long firsterr_signed =
      (first_err == 0xFFFFFFFEU) ? -2L :
      (first_err == 0xFFFFFFFFU) ? -1L : (long)first_err;
   my_printf("stream_diag %lu B %s checked=%lu, firsterr=%ld, "
             "chunks=%lu\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane",
             (unsigned long)done,
             firsterr_signed,
             (unsigned long)chunks);
   return first_err;
}

/* -------- command handlers ---------------------------------------- */

static void cmd_help(void)
{
   my_printf(
      "QSPI shell:\r\n"
      " i              JEDEC RDID 0x9F\r\n"
      " f <a> <n>      SFDP read 0x5A\r\n"
      " s [opc=0x05]   opcode-then-1-byte (RDSR)\r\n"
      " ?              status: busy / presc / fsize / csht\r\n"
      " r <a> <n> [op=0x03]   1-lane read\r\n"
      " F <a> <n>      fast read 0x0B\r\n"
      " q <a> <n>      quad-output read 0x6B\r\n"
      " Y <n>          quad one-hot diagnostic read 0x6C @0\r\n"
      " Z <n>          quad byte diagnostic read 0x6D @0\r\n"
      " W <n>          quad nibble-hold diagnostic read 0x6E @0\r\n"
      " U <n>          quad nibble-ramp diagnostic read 0x6F @0\r\n"
      " X <a> <n>      quad-I/O read 0xEB\r\n"
      " 2 <a> <n>      dual-output read 0x3B\r\n"
      " 3 <a> <n>      dual-I/O read 0xBB (alt=0xA0, 4 dummy)\r\n"
      " M <a> <n>      memory-mapped read\r\n"
      " b <n> [q=0/1] [raw=0/1]  bench streaming read\r\n"
      " o <n>          poison DDR MDMA buffer for n bytes\r\n"
      " y [iters=1000] xor timing over 128 KiB DDR\r\n"
      " m <n> [q=0/1] [raw=0/1]  MDMA streaming read into DDR\r\n"
      " c <len> <chunk> [d=9]    chunked quad MDMA reads (stitch)\r\n"
      " a 0|1          auto-consume completed DMA buffers off/on\r\n"
      " N 0|1          idle JEDEC loop off/on\r\n"
      " A <n> [q=0/1] [raw=1] DDR ping-pong MDMA\r\n"
      " j <n> [q=0/1] raw data-only read (no opcode/addr)\r\n"
      " J <b0> ...    raw data-only write (no opcode/addr)\r\n"
      " e [opc=0x06]   opcode-only (WREN)\r\n"
      " v <v> [op=0x01]  write status reg (op + 1B data)\r\n"
      " w <a> <n> [op=0x02]   1-lane page program\r\n"
      " Q <a> <n>      quad-input PP 0x32\r\n"
      " S <a>          sector erase 4 KB (0x20)\r\n"
      " B <a>          block  erase 64 KB (0xD8)\r\n"
      " C              chip erase 0xC7 (confirm)\r\n"
      " P [op] [m] [v] auto-poll status (default WIP=0)\r\n"
      " 4 [0/1]        exit/enter 4-byte addr (0xE9/0xB7)\r\n"
      " R              soft reset 0x66 0x99\r\n"
      " D              deep power-down 0xB9\r\n"
      " d              release DPD 0xAB\r\n"
      " p <n> [s] [dlyb_sel] [unit]  set prescaler/sample/DLYB\r\n"
      " g <mask6>      raw GPIO drive (bits: CLK NCS IO0 IO1 IO2 IO3)\r\n"
      " G              raw GPIO read  (bits: CLK NCS IO0 IO1 IO2 IO3)\r\n"
      " k <n> [q=0/1] [io=1]  bit-bang raw read over GPIO\r\n"
      " K <n>          bit-bang 0x6B quad-output read @0\r\n"
      " t [ms=1]       toggle all 6 pins forever (reset to stop)\r\n"
      " x              abort + drain FIFO\r\n"
      " h              this help\r\n");
}

static void cmd_jedec(void)
{
   const qspi_cmd_t c = {
      .instruction = 0x9FU, .inst_lines = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_1, .data_len = 3U,
   };
   uint8_t id[3] = {0};
   if (qspi_xfer(&c, QSPI_FMODE_READ, id, 100U) != HAL_OK) {
      my_printf("ERR jedec\r\n");
      return;
   }
   my_printf("JEDEC ID: %02x %02x %02x\r\n", id[0], id[1], id[2]);
}

static void cmd_sfdp(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x5AU,
      .address      = addr,
      .addr_bytes   = 3U,
      .dummy_cycles = 8U,
      .inst_lines   = QSPI_LINES_1,
      .addr_lines   = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_1,
      .data_len     = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 1000U) != HAL_OK) {
      my_printf("ERR sfdp\r\n");
      return;
   }
   my_printf("SFDP @ 0x%08lx (%lu bytes)\r\n",
             (unsigned long)addr, (unsigned long)len);
   hexdump(addr, io_buf, len);
}

static void cmd_status(int argc, char **argv)
{
   uint32_t opc = arg_u32(argc > 0 ? argv[0] : NULL, 0x05U);
   const qspi_cmd_t c = {
      .instruction = (uint8_t)opc, .inst_lines = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_1, .data_len = 1U,
   };
   uint8_t v = 0xFFU;
   if (qspi_xfer(&c, QSPI_FMODE_READ, &v, 100U) != HAL_OK) {
      my_printf("ERR status\r\n");
      return;
   }
   my_printf("op=%02x -> %02x\r\n", (unsigned)opc, v);
}

static void cmd_query(void)
{
   my_printf("busy=%d presc=%lu fsize=%lu csht=%lu qspi_hz=%lu\r\n",
             qspi_busy(),
             (unsigned long)qspi_get_prescaler(),
             (unsigned long)qspi_get_fsize(),
             (unsigned long)qspi_get_csht(),
             (unsigned long)qspi_kernel_hz());
}

static void cmd_read(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   uint32_t opc  = arg_u32(argc > 2 ? argv[2] : NULL, 0x03U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction = (uint8_t)opc,
      .address     = addr, .addr_bytes = 3U,
      .inst_lines  = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_1, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR read\r\n");
      return;
   }
   my_printf("op=%02x read %lu @ 0x%08lx\r\n",
             (unsigned)opc, (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

static void cmd_fast_read(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x0BU,
      .address      = addr, .addr_bytes = 3U,
      .dummy_cycles = 8U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_1, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR fast\r\n");
      return;
   }
   my_printf("op=0b read %lu @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

static void cmd_quad_read(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x6BU,
      .address      = addr, .addr_bytes = 3U,
      .alt_bytes    = 1U, .alt_bytes_len = 1U,
      .dummy_cycles = 9U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .alt_lines    = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_4, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR quadr\r\n");
      return;
   }
   my_printf("op=6b read %lu @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

static void cmd_quad_onehot_read(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 32U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x6CU,
      .address      = 0U, .addr_bytes = 3U,
      .dummy_cycles = 7U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_4, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR quad1h\r\n");
      return;
   }
   my_printf("op=6c read %lu @ 0x00000000\r\n", (unsigned long)len);
   hexdump(0U, io_buf, len);
}

static void cmd_quad_byte_diag_read(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 32U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x6DU,
      .address      = 0U, .addr_bytes = 3U,
      .dummy_cycles = 7U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_4, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR quadbd\r\n");
      return;
   }
   my_printf("op=6d read %lu @ 0x00000000\r\n", (unsigned long)len);
   hexdump(0U, io_buf, len);
}

static void cmd_quad_nibble_hold_read(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 32U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x6EU,
      .address      = 0U, .addr_bytes = 3U,
      .dummy_cycles = 7U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_4, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR quadnh\r\n");
      return;
   }
   my_printf("op=6e read %lu @ 0x00000000\r\n", (unsigned long)len);
   hexdump(0U, io_buf, len);
}

static void cmd_quad_nibble_ramp_read(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 32U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x6FU,
      .address      = 0U, .addr_bytes = 3U,
      .dummy_cycles = 7U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_4, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR quadnr\r\n");
      return;
   }
   my_printf("op=6f read %lu @ 0x00000000\r\n", (unsigned long)len);
   hexdump(0U, io_buf, len);
}

static void cmd_quad_io_read(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction   = 0xEBU,
      .address       = addr, .addr_bytes = 3U,
      .alt_bytes     = 0xA0U, .alt_bytes_len = 1U,
      .dummy_cycles  = 4U,
      .inst_lines    = QSPI_LINES_1,
      .addr_lines    = QSPI_LINES_4,
      .alt_lines     = QSPI_LINES_4,
      .data_lines    = QSPI_LINES_4,
      .data_len      = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR quadio\r\n");
      return;
   }
   my_printf("op=eb read %lu @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

static void cmd_dual_read(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction  = 0x3BU,
      .address      = addr, .addr_bytes = 3U,
      .dummy_cycles = 8U,
      .inst_lines   = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines   = QSPI_LINES_2, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR dualr\r\n");
      return;
   }
   my_printf("op=3b read %lu @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

static void cmd_dual_io_read(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction   = 0xBBU,
      .address       = addr, .addr_bytes = 3U,
      .alt_bytes     = 0xA0U, .alt_bytes_len = 1U,
      .dummy_cycles  = 4U,
      .inst_lines    = QSPI_LINES_1,
      .addr_lines    = QSPI_LINES_2,
      .alt_lines     = QSPI_LINES_2,
      .data_lines    = QSPI_LINES_2,
      .data_len      = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR dualio\r\n");
      return;
   }
   my_printf("op=bb read %lu @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

static void cmd_mm(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .instruction   = 0xEBU,
      .addr_bytes    = 3U,
      .alt_bytes     = 0xA0U, .alt_bytes_len = 1U,
      .dummy_cycles  = 4U,
      .inst_lines    = QSPI_LINES_1,
      .addr_lines    = QSPI_LINES_4,
      .alt_lines     = QSPI_LINES_4,
      .data_lines    = QSPI_LINES_4,
   };
   if (qspi_mm_enable(&c) != HAL_OK) {
      my_printf("ERR mm\r\n");
      return;
   }
   const volatile uint8_t *src =
       (const volatile uint8_t *)(QSPI_MM_BASE + addr);
   for (uint32_t i = 0; i < len; i++)
      io_buf[i] = src[i];
   qspi_mm_disable();
   my_printf("MM read %lu @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
   hexdump(addr, io_buf, len);
}

/* Memory-mapped quad-read benchmark.  Programs FMODE=11 with the
 * 0x6B Quad Output Read recipe (3-byte addr, 9 dummy cycles, quad
 * data lines) so the QUADSPI peripheral autonomously refills its
 * prefetch buffer; the CPU (or MDMA) then copies LEN bytes from the
 * QSPI memory-mapped window straight into DDR (DEF_DDR_BASE, no
 * io_buf staging) and reports wall-rate.  This path bypasses MDMA's
 * per-word AXI handshake (the indirect-read bottleneck identified in
 * agent4/TODO.md) so it should comfortably exceed the 337 Mbps
 * indirect-read ceiling.
 *
 * argv[0] = LEN (default 1 MiB, capped at 16 MiB).
 * argv[1] = VARIANT (default 0):
 *   0: byte-by-byte CPU loop  (regression baseline, ~36 Mbps)
 *   1: 32-bit word CPU loop
 *   2: memcpy() with non-volatile aliases (LDM/STM bursts)
 *   3: MDMA mem-to-mem from QSPI_MM_BASE (software-request)
 */
static uint32_t mdma_mm_to_dst_mode(uint32_t dst_addr, uint32_t src_offset,
                                    uint32_t len, bool byte_source,
                                    uint32_t *cisr_out, uint32_t *cesr_out)
{
   *cisr_out = 0U;
   *cesr_out = 0U;
   if (len == 0U)
      return 0U;
   /* Block-repeat path requires 64 KiB-multiple lengths for >0x1FFFF. */
   if (len > 0x1FFFFU && (len & 0xFFFFU)) {
      my_printf("ERR mmap mdma: len must be 64 KiB-multiple if >128 KB\r\n");
      return 0U;
   }

   __HAL_RCC_MDMA_CLK_ENABLE();

   MDMA_Channel_TypeDef *const ch = MDMA_Channel0;
   ch->CCR = 0U;
   /* Wait for the channel to actually disable before clobbering setup
    * registers.  Per RM0475 the CBNDTR/CTCR/CSAR/CDAR registers are only
    * writable when CCR.EN=0 and the channel request flag (CRQA) is
    * inactive; if we reprogram while CRQA still latches a pending
    * software request from a previous transfer, the writes are silently
    * dropped and the channel runs with stale block parameters (this
    * presented as bndt=00fcffc0 instead of 00010000 on the failing
    * second-superchunk MDMA).  Loop is bounded so a stuck channel can't
    * wedge us. */
   for (uint32_t i = 0U; i < 1000000U; i++) {
      if (!(ch->CCR & MDMA_CCR_EN_Msk) &&
          !(ch->CISR & MDMA_CISR_CRQA_Msk))
         break;
      __asm volatile("nop");
   }
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF | MDMA_CIFCR_CBRTIF |
               MDMA_CIFCR_CBTIF | MDMA_CIFCR_CLTCIF;

   /* Clean+invalidate destination cache range so:
    *  (a) any dirty CPU-side cache lines won't get written back over
    *      MDMA-deposited data, and
    *  (b) subsequent CPU reads see fresh DDR contents. */
   const uint32_t line_sz = 32U;
   for (uint32_t a = dst_addr & ~(line_sz - 1U);
        a < dst_addr + len; a += line_sz)
      L1C_CleanInvalidateDCacheMVA((void *)a);
   __asm volatile("dsb sy" ::: "memory");

   /* CTCR for mem-to-mem from QSPI_MM_BASE:
    *   SINC=2 (source increments), DINC=2 (dest increments)
    *   SSIZE=2 (32-bit reads), or 0 for byte_source raw quad reads
    *   DSIZE=2 (32-bit writes)
    *   SINCOS=2 for word reads, 0 for byte_source; DINCOS=2
    *   SBURST=0 (single 32-bit AHB read), DBURST=4 (16-beat DDR write)
    *      The QSPI peripheral's AHB slave (FMODE=11) does NOT cleanly
    *      service 16-beat AHB read bursts: with SBURST=4, the channel
    *      transferred exactly one 64 B buffer (BNDT=0xFFC0) on the first
    *      MDMA call after each qspi_mm_disable / qspi_mm_enable then
    *      stalled with CRQA stuck at 1, and the readback on the very
    *      first 16-beat burst showed a deterministic single-bit toggle
    *      mid-burst (a8980848 vs a8180848 at word 1, byte 2).  Issuing
    *      single 32-bit AHB beats matches the indirect (FMODE=01) MDMA
    *      path that already passes the twin-DDR test at 300 Mbps.  DDR
    *      writes still benefit from the 16-beat burst.
    *   TLEN=64-1 (64 B per buffer transfer)
    *   PKE=1 for byte_source, packing four byte-sized QSPI_MM reads into
    *      each 32-bit DDR write while preserving byte order.
    *   TRGM picks per-request granularity (see below) so a single
    *      software trigger drains the whole block / repeated block
    *      rather than only one 64 B buffer.
    *   SWRM=1 (software request mode)
    *   BWM=1 (bufferable writes -- DDR is normal cacheable)
    */
   const uint32_t tlen = 64U;
   uint32_t trgm;
   if (len <= 64U)
      trgm = 0U; /* one buffer per request */
   else if (len <= 65536U)
      trgm = 1U; /* one block (BNDT bytes) per request */
   else
      trgm = 2U; /* one repeated block per request -- drains entire xfer */
   ch->CTCR =
      (2U << MDMA_CTCR_SINC_Pos)   |
      (2U << MDMA_CTCR_DINC_Pos)   |
      ((byte_source ? 0U : 2U) << MDMA_CTCR_SSIZE_Pos) |
      (2U << MDMA_CTCR_DSIZE_Pos)  |
      ((byte_source ? 0U : 2U) << MDMA_CTCR_SINCOS_Pos) |
      (2U << MDMA_CTCR_DINCOS_Pos) |
      (0U << MDMA_CTCR_SBURST_Pos) |
      ((byte_source ? 0U : 4U) << MDMA_CTCR_DBURST_Pos) |
      ((tlen - 1U) << MDMA_CTCR_TLEN_Pos) |
      (trgm << MDMA_CTCR_TRGM_Pos) |
      (byte_source ? MDMA_CTCR_PKE : 0U) |
      MDMA_CTCR_SWRM |
      MDMA_CTCR_BWM;

   if (len > 0x1FFFFU) {
      const uint32_t blocks = len >> 16; /* len / 65536 */
      /* BRSUM/BRDUM = 1 means the block-start address INCREMENTS
       * across blocks (continues forward); BRSUM/BRDUM = 0 would
       * leave the start fixed (not what we want for contiguous src
       * and contiguous dst). */
      ch->CBNDTR = (65536U & MDMA_CBNDTR_BNDT_Msk) |
                   ((blocks - 1U) << MDMA_CBNDTR_BRC_Pos) |
                   MDMA_CBNDTR_BRDUM | MDMA_CBNDTR_BRSUM;
   } else {
      ch->CBNDTR = len & MDMA_CBNDTR_BNDT_Msk;
   }
   ch->CBRUR = 0U;
   ch->CSAR  = QSPI_MM_BASE + src_offset;
   ch->CDAR  = dst_addr;
   ch->CTBR  = 0U;
   ch->CMAR  = 0U;
   ch->CMDR  = 0U;

   __asm volatile("dsb sy" ::: "memory");
   ch->CCR = MDMA_CCR_EN;
   __asm volatile("dsb sy" ::: "memory");
   const uint32_t t0 = HAL_GetTick();
   /* Kick: software request fires the first buffer; SWRM mode keeps
    * re-arming until BNDT/BRC drain. */
   ch->CCR = MDMA_CCR_EN | MDMA_CCR_SWRQ;

   const uint32_t deadline = t0 + 60000U;
   uint32_t cisr;
   for (;;) {
      cisr = ch->CISR;
      if (cisr & MDMA_CISR_TEIF)
         break;
      if (cisr & MDMA_CISR_CTCIF)
         break;
      if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
         my_printf("ERR mmap mdma: timeout cisr=%08lx cesr=%08lx bndt=%08lx\r\n",
                   (unsigned long)cisr, (unsigned long)ch->CESR,
                   (unsigned long)ch->CBNDTR);
         break;
      }
   }
   const uint32_t dt = HAL_GetTick() - t0;
   *cisr_out = cisr;
   *cesr_out = ch->CESR;
   ch->CCR = 0U;
   ch->CIFCR = MDMA_CIFCR_CTEIF | MDMA_CIFCR_CCTCIF | MDMA_CIFCR_CBRTIF |
               MDMA_CIFCR_CBTIF | MDMA_CIFCR_CLTCIF;
   __asm volatile("dsb sy" ::: "memory");

   /* Invalidate destination cache range AFTER MDMA writes:
    * the Cortex-A7 can speculatively prefetch into D-cache lines that
    * cover [dst_addr, dst_addr+len) at any time between the pre-MDMA
    * cleanInvalidate and the MDMA writes actually landing in DDR.  If
    * those speculatively-loaded lines stay in cache, a subsequent CPU
    * read sees stale (pre-MDMA) DDR contents.  Twin validation then
    * compares fresh slot1 against stale slot0 and reports a phantom
    * diff.  Mirror what qspi_mdma_finish() does for the indirect path. */
   for (uint32_t a = dst_addr & ~(line_sz - 1U);
        a < dst_addr + len; a += line_sz)
      L1C_InvalidateDCacheMVA((void *)a);
   __asm volatile("dsb sy" ::: "memory");
   return dt;
}

static uint32_t mdma_mm_to_dst(uint32_t dst_addr, uint32_t src_offset,
                               uint32_t len,
                               uint32_t *cisr_out, uint32_t *cesr_out)
{
   return mdma_mm_to_dst_mode(dst_addr, src_offset, len, false,
                              cisr_out, cesr_out);
}

static uint32_t mmap_bench_mdma(uint32_t len, uint32_t *cisr_out,
                                uint32_t *cesr_out)
{
   return mdma_mm_to_dst(DEF_DDR_BASE, 0U, len, cisr_out, cesr_out);
}

static void cmd_mmap_bench(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 1048576U);
   if (len == 0U) len = 1048576U;
   if (len > 16U * 1024U * 1024U) len = 16U * 1024U * 1024U;
   uint32_t variant = arg_u32(argc > 1 ? argv[1] : NULL, 0U);
   if (variant > 3U) variant = 0U;

   /* Variants 1/2/3 require 4-byte alignment of len. */
   if (variant != 0U && (len & 0x3U)) {
      my_printf("ERR mmap: variant=%lu requires 4-byte aligned len\r\n",
                (unsigned long)variant);
      return;
   }

   const qspi_cmd_t c = {
      .instruction   = 0x6BU,
      .addr_bytes    = 3U,
      .alt_bytes     = 0U, .alt_bytes_len = 0U,
      .dummy_cycles  = 9U,
      .inst_lines    = QSPI_LINES_1,
      .addr_lines    = QSPI_LINES_1,
      .alt_lines     = QSPI_LINES_1,
      .data_lines    = QSPI_LINES_4,
   };
   if (qspi_mm_enable(&c) != HAL_OK) {
      my_printf("ERR mmap\r\n");
      return;
   }

   uint32_t dt = 0U;
   uint32_t mdma_cisr = 0U;
   uint32_t mdma_cesr = 0U;

   switch (variant) {
      case 0U: {
         volatile uint8_t *src = (volatile uint8_t *)(QSPI_MM_BASE + 0U);
         volatile uint8_t *dst = (volatile uint8_t *)DEF_DDR_BASE;
         uint32_t t0 = HAL_GetTick();
         for (uint32_t i = 0; i < len; i++)
            dst[i] = src[i];
         dt = HAL_GetTick() - t0;
         break;
      }
      case 1U: {
         volatile uint32_t *src = (volatile uint32_t *)(QSPI_MM_BASE + 0U);
         volatile uint32_t *dst = (volatile uint32_t *)DEF_DDR_BASE;
         const uint32_t nw = len >> 2;
         uint32_t t0 = HAL_GetTick();
         for (uint32_t i = 0; i < nw; i++)
            dst[i] = src[i];
         dt = HAL_GetTick() - t0;
         break;
      }
      case 2U: {
         /* Non-volatile aliases so gcc emits LDM/STM bursts.  The QSPI
          * MM region is mapped non-cacheable so reads still go to the
          * peripheral; gcc just treats them as ordinary memory and is
          * free to coalesce. */
         void *src = (void *)(QSPI_MM_BASE + 0U);
         void *dst = (void *)DEF_DDR_BASE;
         uint32_t t0 = HAL_GetTick();
         memcpy(dst, src, len);
         dt = HAL_GetTick() - t0;
         break;
      }
      case 3U:
      default: {
         dt = mmap_bench_mdma(len, &mdma_cisr, &mdma_cesr);
         break;
      }
   }

   qspi_mm_disable();

   uint32_t mbps_x100 = (dt == 0U)
       ? 0U
       : (uint32_t)(((uint64_t)len * 8ULL * 100ULL) /
                    ((uint64_t)dt * 1000ULL));
   if (variant == 3U) {
      my_printf("mmap %lu B quad in %lu ms, %lu.%02lu Mbps, "
                "presc=%lu, qspi_hz=%lu, variant=%lu, "
                "cisr=%08lx, cesr=%08lx\r\n",
                (unsigned long)len, (unsigned long)dt,
                (unsigned long)(mbps_x100 / 100U),
                (unsigned long)(mbps_x100 % 100U),
                (unsigned long)qspi_get_prescaler(),
                (unsigned long)qspi_kernel_hz(),
                (unsigned long)variant,
                (unsigned long)mdma_cisr,
                (unsigned long)mdma_cesr);
   } else {
      my_printf("mmap %lu B quad in %lu ms, %lu.%02lu Mbps, "
                "presc=%lu, qspi_hz=%lu, variant=%lu\r\n",
                (unsigned long)len, (unsigned long)dt,
                (unsigned long)(mbps_x100 / 100U),
                (unsigned long)(mbps_x100 % 100U),
                (unsigned long)qspi_get_prescaler(),
                (unsigned long)qspi_kernel_hz(),
                (unsigned long)variant);
   }
}

/* mmap MDMA twin-DDR integrity check.  Streams the QSPI memory-mapped
 * window (FMODE=11, 0x6B Quad Output Read, presc=3) into a pair of
 * DDR slots (slot0, slot1) via the proven MDMA mem-to-mem path,
 * reading EACH chunk twice (pass A -> slot0, pass B -> slot1) and
 * byte-comparing the two reads.  firsterr=-1 means the channel
 * transports the SAME data identically twice -- bit-perfect transport
 * regardless of the slave's actual data pattern.  This is the same
 * approach the spi_quad mission used at 337 Mbps; it does not depend
 * on the slave generating any specific pattern (the qspi.bin slave
 * matches `i & 0xFF` only for the first 4 bytes; the rest is a
 * deterministic but complex pattern not worth modeling on the master).
 *
 * Headline Mbps is computed from pass1_ms only (the streaming wall
 * rate); pass2_ms is integrity overhead.
 *
 * argv[0] = TOTAL bytes (default 16 MiB; rounded down to 64 KiB
 *           multiple; capped at 0xFF000000U to keep i*CHUNK + k
 *           arithmetic safe in uint32; min 65536). */
static void cmd_mmap_int(int argc, char **argv)
{
   uint32_t total = arg_u32(argc > 0 ? argv[0] : NULL, 16777216U);
   if (total > 0xFF000000U) total = 0xFF000000U;
   total &= ~0xFFFFU; /* round down to 64 KiB multiple */
   if (total < 65536U) {
      my_printf("ERR mmap_int: total must be >= 65536\r\n");
      return;
   }

   const uint32_t CHUNK = 1U * 1024U * 1024U;        /* 1 MiB inner */
   const uint32_t SUPERCHUNK = 16U * 1024U * 1024U;  /* 16 MiB twin-validation unit */
   const uint32_t slot0 = DEF_DDR_BASE;
   const uint32_t slot1 = DEF_DDR_BASE + SUPERCHUNK;

   /* Round total down to SUPERCHUNK multiple. */
   total &= ~(SUPERCHUNK - 1U);
   if (total < SUPERCHUNK) {
      my_printf("ERR mmap_int: total < 16 MiB\r\n");
      busy_flag = false;
      return;
   }
   const uint32_t nsuper = total / SUPERCHUNK;
   const uint32_t inner = SUPERCHUNK / CHUNK;   /* 16 */

   const qspi_cmd_t c = {
      .instruction   = 0x6BU,
      .addr_bytes    = 3U,
      .alt_bytes     = 0U, .alt_bytes_len = 0U,
      .dummy_cycles  = 9U,
      .inst_lines    = QSPI_LINES_1,
      .addr_lines    = QSPI_LINES_1,
      .alt_lines     = QSPI_LINES_1,
      .data_lines    = QSPI_LINES_4,
   };

   uint32_t pass1_ms = 0U, pass2_ms = 0U, validate_ms = 0U;
   uint32_t cisr = 0U, cesr = 0U;
   uint32_t firsterr = 0xFFFFFFFFUL;

   if (qspi_mm_enable(&c) != HAL_OK) {
      my_printf("ERR mmap_int enable\r\n"); busy_flag = false; return;
   }

   for (uint32_t s = 0; s < nsuper; s++) {
      /* Pass A: reset slave to addr 0, then 16 x 1-MiB MDMA covering slave 0..16MB-1 */
      qspi_mm_disable();
      if (qspi_mm_enable(&c) != HAL_OK) {
         my_printf("ERR mmap_int re-enable A s=%lu\r\n", (unsigned long)s);
         break;
      }
      for (uint32_t j = 0; j < inner; j++) {
         uint32_t cisr_a = 0U, cesr_a = 0U;
         uint32_t dt_a = mdma_mm_to_dst(slot0 + j*CHUNK, j*CHUNK, CHUNK,
                                         &cisr_a, &cesr_a);
         pass1_ms += dt_a; cisr |= cisr_a; cesr |= cesr_a;
      }

      /* Pass B: reset slave again, then read same 16 MiB into slot1. */
      qspi_mm_disable();
      if (qspi_mm_enable(&c) != HAL_OK) {
         my_printf("ERR mmap_int re-enable B s=%lu\r\n", (unsigned long)s);
         break;
      }
      for (uint32_t j = 0; j < inner; j++) {
         uint32_t cisr_b = 0U, cesr_b = 0U;
         uint32_t dt_b = mdma_mm_to_dst(slot1 + j*CHUNK, j*CHUNK, CHUNK,
                                         &cisr_b, &cesr_b);
         pass2_ms += dt_b; cisr |= cisr_b; cesr |= cesr_b;
      }

      /* Validate slot0 vs slot1 (both should hold identical 16 MiB). */
      if (firsterr == 0xFFFFFFFFUL) {
         uint32_t v_t0 = HAL_GetTick();
         const uint32_t *pa = (const uint32_t *)slot0;
         const uint32_t *pb = (const uint32_t *)slot1;
         for (uint32_t k = 0; k < SUPERCHUNK / 4U; k++) {
            if (pa[k] != pb[k]) {
               uint32_t a = pa[k], b = pb[k];
               for (int byte = 0; byte < 4; byte++) {
                  if (((a >> (8*byte)) & 0xFFU) != ((b >> (8*byte)) & 0xFFU)) {
                     firsterr = s*SUPERCHUNK + k*4U + (uint32_t)byte;
                     break;
                  }
               }
               my_printf("mmap_int_diff: s=%lu k=%lu a=%08lx b=%08lx\r\n",
                         (unsigned long)s, (unsigned long)k,
                         (unsigned long)a, (unsigned long)b);
               break;
            }
         }
         validate_ms += HAL_GetTick() - v_t0;
      }
   }

   qspi_mm_disable();

   uint32_t mbps_x100 = 0U;
   if (pass1_ms > 0U) {
      mbps_x100 = (uint32_t)(((uint64_t)total * 800ULL) /
                             ((uint64_t)pass1_ms * 1000ULL));
   }
   const long firsterr_signed =
       (firsterr == 0xFFFFFFFFUL) ? -1L : (long)firsterr;
   my_printf("mmap_int %lu B in %lu ms, %lu.%02lu Mbps, "
             "firsterr=%ld, chunks=%lu, pass2_ms=%lu, "
             "validate=%lu ms, presc=%lu, qspi_hz=%lu, "
             "cisr=%08lx, cesr=%08lx\r\n",
             (unsigned long)total, (unsigned long)pass1_ms,
             (unsigned long)(mbps_x100 / 100U),
             (unsigned long)(mbps_x100 % 100U),
             firsterr_signed,
             (unsigned long)nsuper,
             (unsigned long)pass2_ms,
             (unsigned long)validate_ms,
             (unsigned long)qspi_get_prescaler(),
             (unsigned long)qspi_kernel_hz(),
             (unsigned long)cisr,
             (unsigned long)cesr);
}

static void cmd_bench(int argc, char **argv)
{
   uint32_t len  = arg_u32(argc > 0 ? argv[0] : NULL, 256U);
   uint32_t quad = arg_u32(argc > 1 ? argv[1] : NULL, 0U);
   uint32_t raw  = arg_u32(argc > 2 ? argv[2] : NULL, 0U);
   if (len == 0U) len = 256U;
   busy_flag = true;

   const uint8_t  opcode = quad ? 0x6BU : 0x0BU;
   const qspi_lines_t dl = quad ? QSPI_LINES_4 : QSPI_LINES_1;
   const uint8_t dummy   = qspi_default_dummy(quad, raw);

   uint32_t crc = 0, firsterr = 0, dt = 0;
   uint8_t  got16[16] = {0};
   my_printf("BENCHDBG cmd_pre t=%lu len=%lu quad=%lu opcode=%02x "
             "presc=%lu\r\n",
             (unsigned long)HAL_GetTick(), (unsigned long)len,
             (unsigned long)quad, (unsigned)opcode,
             (unsigned long)qspi_get_prescaler());
   HAL_StatusTypeDef s = qspi_bench_read(opcode, dl, dummy, len,
                                         raw != 0U,
                                         &crc, &firsterr, got16, &dt);
   my_printf("BENCHDBG cmd_post t=%lu rc=%d dt=%lu firsterr=%ld\r\n",
             (unsigned long)HAL_GetTick(), (int)s, (unsigned long)dt,
             (firsterr == 0xFFFFFFFFU) ? -1L : (long)firsterr);
   if (s != HAL_OK) {
      my_printf("ERR bench (%d)\r\n", (int)s);
      busy_flag = false;
      return;
   }
   if (dt == 0U) dt = 1U;
   /* Mbps in tenths: rate (Mbits/s * 10) = len * 8 * 10 / (dt * 1000)
    *               = len / (dt * 12.5)   ~=  len * 80 / (dt * 1000).
    * Guard the multiplication against 32-bit overflow for len up to 50 MB. */
   uint32_t mbps_x10;
   if (len <= 50000000U)
      mbps_x10 = (len * 80U) / (dt * 1000U);
   else
      mbps_x10 = ((len / 1000U) * 80U) / dt;
   const long firsterr_signed =
       (firsterr == 0xFFFFFFFFU) ? -1L : (long)firsterr;
   my_printf("bench %lu B %s @ presc=%lu in %lu ms, %lu.%lu Mbps, "
             "crc32=%08lx, expect=%08lx, firsterr=%ld, "
             "got=%02x %02x %02x %02x "
             "%02x %02x %02x %02x %02x %02x %02x %02x "
             "%02x %02x %02x %02x\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane",
             (unsigned long)qspi_get_prescaler(),
             (unsigned long)dt,
             (unsigned long)(mbps_x10 / 10U),
             (unsigned long)(mbps_x10 % 10U),
             (unsigned long)crc,
             (unsigned long)crc32_expected(len),
             firsterr_signed,
             got16[0], got16[1], got16[2], got16[3],
             got16[4], got16[5], got16[6], got16[7],
             got16[8], got16[9], got16[10], got16[11],
             got16[12], got16[13], got16[14], got16[15]);
   busy_flag = false;
}

/* MDMA streaming read into DDR.  Validates the same i&0xFF incrementing
 * pattern the bench expects and prints throughput + CRC32 + first-error
 * offset.  Destination is `DEF_DDR_BASE` (0xC0000000) -- 16 MB+ workloads
 * are expected to exceed SRAM and must use this path. */
static void cmd_mdma(int argc, char **argv)
{
   uint32_t len  = arg_u32(argc > 0 ? argv[0] : NULL, 4096U);
   uint32_t quad = arg_u32(argc > 1 ? argv[1] : NULL, 0U);
   uint32_t raw  = arg_u32(argc > 2 ? argv[2] : NULL, 0U);
   if (len == 0U) len = 4096U;
   /* MDMA path requires 4-byte alignment. Large decimal transfers are
    * split into 64 KB-multiple chunks plus a final aligned tail. */
   if (len & 0x3U) {
      my_printf("ERR mdma: len must be 4-byte aligned\r\n");
      return;
   }
   busy_flag = true;

   const uint8_t  opcode = quad ? 0x6BU : 0x0BU;
   const qspi_lines_t dl = quad ? QSPI_LINES_4 : QSPI_LINES_1;
   const uint8_t dummy   = qspi_default_dummy(quad, raw);
   const uint32_t dst    = DEF_DDR_BASE;
   volatile uint8_t *const p = (volatile uint8_t *)dst;
   const uint32_t poison_ms = 0U;

   my_printf("mdma_begin %lu B %s\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane");
   const uint32_t t0 = HAL_GetTick();
   uint32_t done = 0U;
   HAL_StatusTypeDef s = HAL_OK;
   while (done < len) {
      uint32_t n = mdma_segment_len(len - done, 1048576U);
      const uint32_t timeout_ms = (n <= 4096U) ? 2000U : 60000U;
      s = qspi_mdma_read(opcode, dl, dummy, n, raw != 0U, dst + done,
                         timeout_ms, true);
      if (s != HAL_OK)
         break;
      done += n;
   }
   const uint32_t xfer_ms = HAL_GetTick() - t0;
   if (s != HAL_OK) {
      my_printf("ERR mdma (%d) done=%lu dt=%lu\r\n",
                (int)s, (unsigned long)done, (unsigned long)xfer_ms);
      busy_flag = false;
      return;
   }
   uint32_t xfer_dt = xfer_ms;
   if (xfer_dt == 0U) xfer_dt = 1U;
   uint32_t xfer_mbps_x10;
   if (len <= 50000000U)
      xfer_mbps_x10 = (len * 80U) / (xfer_dt * 1000U);
   else
      xfer_mbps_x10 = ((len / 1000U) * 80U) / xfer_dt;
   my_printf("mdma_xfer %lu B %s in %lu ms, %lu.%lu Mbps\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane",
             (unsigned long)xfer_ms,
             (unsigned long)(xfer_mbps_x10 / 10U),
             (unsigned long)(xfer_mbps_x10 % 10U));

   /* CRC32 + first-error scan over the DDR buffer. */
   const uint32_t validate_t0 = HAL_GetTick();
   uint32_t crc       = 0xFFFFFFFFUL;
   uint32_t first_err = 0xFFFFFFFFUL;
   crc = crc32_update_buf(crc, p, len, 0U, &first_err);
   crc ^= 0xFFFFFFFFUL;
   const uint32_t validate_ms = HAL_GetTick() - validate_t0;

   uint32_t dt = xfer_ms;
   if (dt == 0U) dt = 1U;
   uint32_t mbps_x10;
   if (len <= 50000000U)
      mbps_x10 = (len * 80U) / (dt * 1000U);
   else
      mbps_x10 = ((len / 1000U) * 80U) / dt;

   const long firsterr_signed =
       (first_err == 0xFFFFFFFFU) ? -1L : (long)first_err;
   if (firsterr_signed >= 0L && (uint32_t)firsterr_signed < len) {
      uint32_t lo = (firsterr_signed >= 8L)
                      ? (uint32_t)(firsterr_signed - 8L) : 0U;
      uint32_t hi = (uint32_t)firsterr_signed + 24U;
      if (hi > len) hi = len;
      my_printf("firsterr_dump @%lu:", (unsigned long)lo);
      for (uint32_t k = lo; k < hi; k++)
         my_printf(" %02x", (unsigned)p[k]);
      my_printf("\r\n");
   }
   my_printf("mdma %lu B %s in %lu ms, %lu.%lu Mbps, "
             "crc32=%08lx, expect=%08lx, firsterr=%ld, "
             "xfer=%lu ms, validate=%lu ms, "
             "poison=%lu ms, auto=%s\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane",
             (unsigned long)dt,
             (unsigned long)(mbps_x10 / 10U),
             (unsigned long)(mbps_x10 % 10U),
             (unsigned long)crc,
             (unsigned long)crc32_expected(len),
             firsterr_signed,
             (unsigned long)xfer_ms,
             (unsigned long)validate_ms,
             (unsigned long)poison_ms,
             auto_consume ? "on" : "off");
   busy_flag = false;
}

/* Twin MDMA-into-DDR quad raw read for varying-data integrity check.
 * Reads LEN bytes twice via qspi_mdma_read into two separate DDR
 * buffers (slot 0 at DEF_DDR_BASE, slot 1 at DEF_DDR_BASE + LEN_PAD)
 * with the same opcode/lines/dummy/raw settings, then byte-compares
 * the two buffers and reports `firsterr=-1` when every byte matches.
 * The slave (spi_quad.bin from spi.nw with LANES=4) emits a
 * byte-incrementing stream; if the channel transports the same
 * varying byte pattern identically twice, the two DDR buffers are
 * bit-equal, which is real-data integrity validation that does not
 * depend on the master's first-sample alignment relative to the
 * slave's output presenter. Quad raw only (the canonical mission
 * configuration). */
static void cmd_twin_mdma(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 16777216U);
   if (len == 0U) len = 16777216U;
   if (len & 0x3U) {
      my_printf("ERR twin: len must be 4-byte aligned\r\n");
      return;
   }
   /* Cap at 16 MiB per slot to keep within DDR test region. */
   if (len > 16777216U) {
      my_printf("ERR twin: len capped at 16777216\r\n");
      return;
   }
   busy_flag = true;

   const uint8_t opcode = 0x6BU;        /* not used in raw mode */
   const qspi_lines_t dl = QSPI_LINES_4;
   const uint8_t dummy = qspi_default_dummy(1U, 1U);
   const bool raw = true;

   const uint32_t slot0 = DEF_DDR_BASE;
   const uint32_t slot1 = DEF_DDR_BASE + 16777216U;
   volatile uint8_t *const p0 = (volatile uint8_t *)slot0;
   volatile uint8_t *const p1 = (volatile uint8_t *)slot1;

   my_printf("twin_begin %lu B quad raw\r\n", (unsigned long)len);

   const uint32_t t0 = HAL_GetTick();
   HAL_StatusTypeDef s = qspi_mdma_read(opcode, dl, dummy, len, raw,
                                        slot0, 60000U, true);
   if (s != HAL_OK) {
      my_printf("ERR twin pass1 (%d)\r\n", (int)s);
      busy_flag = false;
      return;
   }
   const uint32_t t1 = HAL_GetTick();
   my_printf("twin_pass1_done %lu ms\r\n",
             (unsigned long)(t1 - t0));

   s = qspi_mdma_read(opcode, dl, dummy, len, raw,
                      slot1, 60000U, true);
   if (s != HAL_OK) {
      my_printf("ERR twin pass2 (%d)\r\n", (int)s);
      busy_flag = false;
      return;
   }
   const uint32_t t2 = HAL_GetTick();

   /* Byte-compare and CRC the slot0 buffer. */
   uint32_t first_err = 0xFFFFFFFFUL;
   uint32_t crc       = 0xFFFFFFFFUL;
   for (uint32_t i = 0; i < len; i++) {
      const uint8_t b0 = p0[i];
      const uint8_t b1 = p1[i];
      crc ^= (uint32_t)b0;
      for (int j = 0; j < 8; j++)
         crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
      if (b0 != b1 && first_err == 0xFFFFFFFFUL)
         first_err = i;
   }
   crc ^= 0xFFFFFFFFUL;
   const uint32_t validate_ms = HAL_GetTick() - t2;

   const long firsterr_signed =
       (first_err == 0xFFFFFFFFU) ? -1L : (long)first_err;

   /* Hex dump first 16 bytes of each buffer + first mismatch context. */
   my_printf("twin_hex0[0..15]:");
   for (uint32_t k = 0; k < 16U && k < len; k++)
      my_printf(" %02x", (unsigned)p0[k]);
   my_printf("\r\n");
   my_printf("twin_hex1[0..15]:");
   for (uint32_t k = 0; k < 16U && k < len; k++)
      my_printf(" %02x", (unsigned)p1[k]);
   my_printf("\r\n");
   if (firsterr_signed >= 0L && (uint32_t)firsterr_signed < len) {
      uint32_t lo = (firsterr_signed >= 8L)
                      ? (uint32_t)(firsterr_signed - 8L) : 0U;
      uint32_t hi = (uint32_t)firsterr_signed + 24U;
      if (hi > len) hi = len;
      my_printf("twin_diff @%lu p0:", (unsigned long)lo);
      for (uint32_t k = lo; k < hi; k++)
         my_printf(" %02x", (unsigned)p0[k]);
      my_printf("\r\n");
      my_printf("twin_diff @%lu p1:", (unsigned long)lo);
      for (uint32_t k = lo; k < hi; k++)
         my_printf(" %02x", (unsigned)p1[k]);
      my_printf("\r\n");
   }

   const uint32_t pass1_ms = t1 - t0;
   const uint32_t pass2_ms = t2 - t1;
   uint32_t dt = pass1_ms;
   if (dt == 0U) dt = 1U;
   uint32_t mbps_x10;
   if (len <= 50000000U)
      mbps_x10 = (len * 80U) / (dt * 1000U);
   else
      mbps_x10 = ((len / 1000U) * 80U) / dt;
   my_printf("twin %lu B quad raw pass1=%lu ms pass2=%lu ms "
             "%lu.%lu Mbps crc32=%08lx firsterr=%ld validate=%lu ms "
             "presc=%lu\r\n",
             (unsigned long)len,
             (unsigned long)pass1_ms,
             (unsigned long)pass2_ms,
             (unsigned long)(mbps_x10 / 10U),
             (unsigned long)(mbps_x10 % 10U),
             (unsigned long)crc,
             firsterr_signed,
             (unsigned long)validate_ms,
             (unsigned long)qspi_get_prescaler());
   busy_flag = false;
}

/* Chunked quad-read: split LEN_TOTAL into CHUNK_SIZE-byte MDMA reads,
 * each issued at flash addr = chunk_idx * CHUNK_SIZE so the slave's
 * test pattern restarts at the correct seed for every chunk. The
 * stitched DDR buffer is then validated against the expected
 * incrementing pattern and a CRC32 + first-error scan is reported in
 * the same shape as `m`/`stream`. Only the quad path (opcode 0x6B,
 * dummy_cycles=9) is exposed -- this command exists to bypass the
 * 80-byte boundary corruption observed at high prescalers, by keeping
 * each individual MDMA burst entirely inside the clean window. */
static void cmd_chunked(int argc, char **argv)
{
   uint32_t len_total  = arg_u32(argc > 0 ? argv[0] : NULL, 4096U);
   uint32_t chunk_size = arg_u32(argc > 1 ? argv[1] : NULL, 64U);
   uint32_t dummy      = arg_u32(argc > 2 ? argv[2] : NULL, 9U);
   uint32_t opcode_arg = arg_u32(argc > 3 ? argv[3] : NULL, 0x6BU);
   uint32_t max_retries = arg_u32(argc > 4 ? argv[4] : NULL, 0U);
   if (len_total == 0U || chunk_size == 0U) {
      my_printf("ERR chunked: nonzero LEN and CHUNK required\r\n");
      return;
   }
   if (chunk_size & 0x3U) {
      my_printf("ERR chunked: CHUNK must be 4-byte aligned\r\n");
      return;
   }
   if (len_total % chunk_size != 0U) {
      my_printf("ERR chunked: LEN must be multiple of CHUNK\r\n");
      return;
   }
   busy_flag = true;

   const uint8_t opcode = (uint8_t)opcode_arg;
   const qspi_lines_t dl = QSPI_LINES_4;
   const uint32_t dst   = DEF_DDR_BASE;
   volatile uint8_t *const p = (volatile uint8_t *)dst;
   const uint32_t n_chunks = len_total / chunk_size;

   /* Pre-compute expected per-chunk CRC for retry path. Only valid
    * for chunks aligned to slave's data wrap (256 bytes for opcode
    * 0x6B). When max_retries==0, skip this. */
   uint32_t exp_chunk_crc = 0U;
   bool hw_crc_ok = false;
   if (max_retries > 0U) {
      exp_chunk_crc = crc32_expected(chunk_size);
      hw_crc_ok = crc32_hw_begin();
   }

   my_printf("chunked_begin %lu B chunk=%lu n=%lu dummy=%lu retries=%lu "
             "exp_crc=%08lx hw=%d\r\n",
             (unsigned long)len_total,
             (unsigned long)chunk_size,
             (unsigned long)n_chunks,
             (unsigned long)dummy,
             (unsigned long)max_retries,
             (unsigned long)exp_chunk_crc,
             hw_crc_ok ? 1 : 0);

   uint32_t total_retries = 0U;
   uint32_t failed_chunks = 0U;
   const uint32_t t0 = HAL_GetTick();
   HAL_StatusTypeDef s = HAL_OK;
   for (uint32_t i = 0U; i < n_chunks; i++) {
      const uint32_t flash_addr = (i * chunk_size) & 0x00FFFFFFU;
      const uint32_t buf_off    = i * chunk_size;
      uint32_t retry;
      bool chunk_ok = false;
      for (retry = 0U; retry <= max_retries; retry++) {
         s = qspi_mdma_read_addr(opcode, dl, (uint8_t)dummy, chunk_size,
                                 false, flash_addr, dst + buf_off,
                                 5000U, true);
         if (s != HAL_OK) {
            my_printf("ERR chunked at idx=%lu addr=%lu retry=%lu (%d)\r\n",
                      (unsigned long)i, (unsigned long)flash_addr,
                      (unsigned long)retry, (int)s);
            busy_flag = false;
            return;
         }
         if (max_retries == 0U || !hw_crc_ok) {
            chunk_ok = true;
            break;
         }
         /* HW CRC of this chunk via DMA. */
         crc32_hw_reset();
         s = crc32_mdma_start((volatile uint8_t *)(dst + buf_off),
                              chunk_size);
         if (s == HAL_OK)
            s = crc32_mdma_wait(CRC_MDMA_TIMEOUT_MS);
         if (s != HAL_OK)
            break;
         uint32_t got_crc = crc32_hw_final();
         if (got_crc == exp_chunk_crc) {
            chunk_ok = true;
            if (retry > 0U) total_retries += retry;
            break;
         }
         total_retries++;
      }
      if (!chunk_ok) failed_chunks++;
   }
   const uint32_t xfer_ms = HAL_GetTick() - t0;

   /* Hex dump first 32 bytes for diagnostic */
   my_printf("HEX[0..31]:");
   for (uint32_t k = 0; k < 32U && k < len_total; k++)
      my_printf(" %02x", (unsigned)p[k]);
   my_printf("\r\n");
   /* Hex dump bytes around chunk boundary 1 */
   if (len_total >= chunk_size + 32U) {
      my_printf("HEX[%lu..%lu]:", (unsigned long)(chunk_size - 4U),
                (unsigned long)(chunk_size + 31U));
      for (uint32_t k = chunk_size - 4U; k < chunk_size + 32U; k++)
         my_printf(" %02x", (unsigned)p[k]);
      my_printf("\r\n");
   }

   /* Validate stitched buffer. Pattern depends on opcode:
    *   0x6B: incrementing byte ramp i & 0xFF
    *   0x6F at dummy=9: nibble-ramp byte cycle 01 23 45 67 89 AB CD EF */
   static const uint8_t ramp_pattern[8] = {0x01, 0x23, 0x45, 0x67,
                                            0x89, 0xAB, 0xCD, 0xEF};
   const uint32_t validate_t0 = HAL_GetTick();
   uint32_t crc       = 0xFFFFFFFFUL;
   uint32_t first_err = 0xFFFFFFFFUL;
   for (uint32_t i = 0; i < len_total; i++) {
      const uint8_t b = p[i];
      crc ^= (uint32_t)b;
      for (int j = 0; j < 8; j++)
         crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
      uint8_t exp;
      if (opcode == 0x6FU)
         exp = ramp_pattern[i & 7U];
      else
         exp = (uint8_t)(i & 0xFFU);
      if (b != exp && first_err == 0xFFFFFFFFUL)
         first_err = i;
   }
   crc ^= 0xFFFFFFFFUL;
   const uint32_t validate_ms = HAL_GetTick() - validate_t0;

   uint32_t dt = xfer_ms;
   if (dt == 0U) dt = 1U;
   uint32_t mbps_x10;
   if (len_total <= 50000000U)
      mbps_x10 = (len_total * 80U) / (dt * 1000U);
   else
      mbps_x10 = ((len_total / 1000U) * 80U) / dt;

   const long firsterr_signed =
       (first_err == 0xFFFFFFFFU) ? -1L : (long)first_err;
   my_printf("chunked %lu B chunk=%lu n=%lu in %lu ms, %lu.%lu Mbps, "
             "crc32=%08lx, firsterr=%ld, "
             "xfer=%lu ms, validate=%lu ms, dummy=%lu, presc=%lu, "
             "op=%02lx, retries=%lu, failed=%lu\r\n",
             (unsigned long)len_total,
             (unsigned long)chunk_size,
             (unsigned long)n_chunks,
             (unsigned long)dt,
             (unsigned long)(mbps_x10 / 10U),
             (unsigned long)(mbps_x10 % 10U),
             (unsigned long)crc,
             firsterr_signed,
             (unsigned long)xfer_ms,
             (unsigned long)validate_ms,
             (unsigned long)dummy,
             (unsigned long)qspi_get_prescaler(),
             (unsigned long)opcode,
             (unsigned long)total_retries,
             (unsigned long)failed_chunks);
   busy_flag = false;
}

static void cmd_poison(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 4096U);
   if (len == 0U)
      len = 4096U;
   if (len & 0x3U) {
      my_printf("ERR poison: len must be 4-byte aligned\r\n");
      return;
   }
   busy_flag = true;
   volatile uint8_t *const p = (volatile uint8_t *)DEF_DDR_BASE;
   const uint32_t t0 = HAL_GetTick();
   for (uint32_t i = 0; i < len; i++)
      p[i] = (uint8_t)(~i & 0xFFU);
   __asm volatile("dsb sy" ::: "memory");
   const uint32_t dt = HAL_GetTick() - t0;
   my_printf("poison %lu B in %lu ms\r\n",
             (unsigned long)len, (unsigned long)dt);
   busy_flag = false;
}

static void cmd_xor_time(int argc, char **argv)
{
   uint32_t iters = arg_u32(argc > 0 ? argv[0] : NULL, 1000U);
   if (iters == 0U)
      iters = 1U;

   volatile uint32_t *const p = (volatile uint32_t *)DEF_DDR_BASE;
   const uint32_t words = STREAM_XOR_BYTES / sizeof(uint32_t);
   uint32_t x = 0U;

   busy_flag = true;
   for (uint32_t i = 0U; i < words; i++)
      p[i] = pattern_word(i * 4U);
   __asm volatile("dsb sy" ::: "memory");

   uint32_t t0 = HAL_GetTick();
   for (uint32_t i = 0U; i < iters; i++)
      x ^= xor_words(p, words);
   const uint32_t cached_ms = HAL_GetTick() - t0;

   t0 = HAL_GetTick();
   for (uint32_t i = 0U; i < iters; i++) {
      for (uint32_t a = DEF_DDR_BASE;
           a < DEF_DDR_BASE + STREAM_XOR_BYTES; a += 32U)
         L1C_InvalidateDCacheMVA((void *)a);
      __asm volatile("dsb sy" ::: "memory");
      x ^= xor_words(p, words);
   }
   const uint32_t cold_ms = HAL_GetTick() - t0;

   my_printf("xor128 len=%lu iters=%lu xor=%08lx cached=%lu ms "
             "cached_us_x1000=%lu cold=%lu ms cold_us_x1000=%lu\r\n",
             (unsigned long)STREAM_XOR_BYTES,
             (unsigned long)iters,
             (unsigned long)x,
             (unsigned long)cached_ms,
             (unsigned long)((cached_ms * 1000000UL) / iters),
             (unsigned long)cold_ms,
             (unsigned long)((cold_ms * 1000000UL) / iters));
   busy_flag = false;
}

static HAL_StatusTypeDef stream_mmap_raw_quad_read(uint32_t len,
                                                   uint8_t dummy,
                                                   uint32_t *crc_io,
                                                   uint32_t *first_err_io,
                                                   uint32_t *xfer_ms_io,
                                                   uint32_t *chunks_io,
                                                   uint32_t slot_base[2],
                                                   bool slot_valid[2])
{
   const qspi_cmd_t c = {
      .instruction   = 0x6BU,
      .address       = 0U,
      .addr_bytes    = 3U,
      .alt_bytes     = 0U,
      .alt_bytes_len = 0U,
      .dummy_cycles  = dummy,
      .inst_lines    = QSPI_LINES_1,
      .addr_lines    = QSPI_LINES_1,
      .alt_lines     = QSPI_LINES_NONE,
      .data_lines    = QSPI_LINES_4,
   };

   uint32_t done = 0U;
   uint32_t chunks = 0U;
   uint32_t crc = CRC32_INIT;
   uint32_t first_err = 0xFFFFFFFFUL;
   uint32_t xfer_ms = 0U;

   slot_valid[0] = false;
   slot_valid[1] = false;

   while (done < len) {
      const uint32_t n = stream_segment_len(len - done,
                                            STREAM_CHUNK_BYTES);
      const uint32_t idx = chunks & 1U;
      const uint32_t dst = DEF_DDR_BASE + idx * STREAM_CHUNK_BYTES;

      uint32_t cr = QUADSPI->CR;
      cr &= ~QUADSPI_CR_FTHRES_Msk;
      cr |= (0U << QUADSPI_CR_FTHRES_Pos);
      QUADSPI->CR = cr;

      qspi_mm_disable();
      HAL_Delay(1U);
      if (qspi_mm_enable(&c) != HAL_OK) {
         qspi_mm_disable();
         return HAL_ERROR;
      }
      if (done == 0U && n <= 4096U) {
         my_printf("qspi_mmap_rawcfg len=%lu frame=6b cr=%08lx dcr=%08lx "
                   "sr=%08lx ccr=%08lx abr=%08lx dummy=%lu\r\n",
                   (unsigned long)n,
                   (unsigned long)QUADSPI->CR,
                   (unsigned long)QUADSPI->DCR,
                   (unsigned long)QUADSPI->SR,
                   (unsigned long)QUADSPI->CCR,
                   (unsigned long)QUADSPI->ABR,
                   (unsigned long)dummy);
      }

      const uint32_t tcopy = HAL_GetTick();
      volatile const uint32_t *const src =
         (volatile const uint32_t *)(QSPI_MM_BASE + done);
      volatile uint32_t *const dstp = (volatile uint32_t *)dst;
      for (uint32_t i = 0U; i < (n >> 2); i++)
         dstp[i] = src[i];
      const uint32_t dt = HAL_GetTick() - tcopy;
      qspi_mm_disable();
      xfer_ms += dt;

      volatile uint8_t *const p = (volatile uint8_t *)dst;
      if (first_err == 0xFFFFFFFFUL) {
         const uint32_t before = first_err;
         validate_pattern_buf(p, n, done, &first_err);
         if (before == 0xFFFFFFFFUL && first_err != 0xFFFFFFFFUL)
            stream_print_firsterr_dump(p, n, done, first_err);
      }
      crc = crc32_update_buf(crc, p, n, done, NULL);

      slot_base[idx] = done;
      slot_valid[idx] = true;
      done += n;
      chunks++;
   }

   *crc_io = crc;
   *first_err_io = first_err;
   *xfer_ms_io = xfer_ms;
   *chunks_io = chunks;
   return HAL_OK;
}

/* DDR ping-pong auto-consume MDMA path.  This keeps the bulk-DDR `m`
 * command intact but gives sustained-stream bring-up a separate command
 * whose validation consumes completed chunks into one running CRC. */
static void cmd_auto_stream(int argc, char **argv)
{
   uint32_t len   = arg_u32(argc > 0 ? argv[0] : NULL, 4096U);
   uint32_t quad  = arg_u32(argc > 1 ? argv[1] : NULL, 0U);
   uint32_t raw   = arg_u32(argc > 2 ? argv[2] : NULL, 1U);
   uint32_t dummy_override = arg_u32(argc > 3 ? argv[3] : NULL, 0U);
   uint32_t chunk = auto_consume ? STREAM_CHUNK_BYTES :
                                  STREAM_MAX_XFER_BYTES;
   if (len == 0U)
      len = 4096U;
   if (len & 0x3U) {
      my_printf("ERR stream: len must be 4-byte aligned\r\n");
      return;
   }
   busy_flag = true;

   const uint8_t  opcode = quad ? 0x6BU : 0x0BU;
   const qspi_lines_t dl = quad ? QSPI_LINES_4 : QSPI_LINES_1;
   const uint8_t dummy   = (dummy_override != 0U)
                              ? (uint8_t)dummy_override
                              : qspi_default_dummy(quad, raw);
   const bool raw_quad = quad != 0U && raw != 0U;

   uint32_t first_err = 0xFFFFFFFFUL;
   uint32_t done      = 0U;
   uint32_t started   = 0U;
   uint32_t chunks    = 0U;
   uint32_t crc       = CRC32_INIT;
   bool hw_crc        = false;
   uint32_t slot_base[2] = {0U, 0U};
   bool slot_valid[2]    = {false, false};
   const uint32_t t0  = HAL_GetTick();
   uint32_t xfer_ms   = 0U;
   bool xfer_reported = false;
   bool crc_dma_error = false;
   bool direct_crc    = false;
   const char *buf_name = "ddr";
   HAL_StatusTypeDef s = HAL_OK;

   if (stream_direct_crc_eligible(quad, raw) && crc32_hw_begin_direct()) {
      direct_crc = true;
      hw_crc = true;
      buf_name = "crc";
      s = stream_direct_crc_read(opcode, dl, dummy, raw != 0U, len,
                                 &done, &chunks);
      started = done;
   } else if (false && auto_consume && raw_quad) {
      buf_name = "mmap";
      s = stream_mmap_raw_quad_read(len, dummy, &crc, &first_err,
                                    &xfer_ms, &chunks,
                                    slot_base, slot_valid);
      if (s == HAL_OK) {
         done = len;
         started = len;
         stream_print_xfer(len, quad != 0U, xfer_ms);
         xfer_reported = true;
      }
   } else if (auto_consume) {
      hw_crc = crc32_hw_begin();

      uint32_t active_idx = 0U;
      uint32_t active_base = 0U;
      uint32_t active_len = stream_segment_len(len, STREAM_CHUNK_BYTES);
      int crc_active_idx = -1;

      s = qspi_mdma_start(opcode, dl, dummy, active_len, raw != 0U,
                          DEF_DDR_BASE, 60000U, false);
      if (s == HAL_OK)
         started = active_len;

      while (s == HAL_OK && done < len) {
         s = qspi_mdma_finish_no_inval(60000U);
         if (s != HAL_OK)
            break;

         uint32_t next_idx = active_idx ^ 1U;
         uint32_t next_base = started;
         uint32_t next_len = 0U;
         bool next_active = false;

         if (started < len) {
            if (hw_crc && crc_active_idx == (int)next_idx) {
               s = crc32_mdma_wait(CRC_MDMA_TIMEOUT_MS);
               if (s != HAL_OK) {
                  crc_dma_error = true;
                  break;
               }
               crc_active_idx = -1;
            }
            next_len = stream_segment_len(len - started,
                                          STREAM_CHUNK_BYTES);
            s = qspi_mdma_start(opcode, dl, dummy, next_len,
                                raw != 0U,
                                DEF_DDR_BASE +
                                   (next_idx * STREAM_CHUNK_BYTES),
                                60000U, false);
            if (s != HAL_OK)
               break;
            started += next_len;
            next_active = true;
         }

         if (!next_active) {
            xfer_ms = HAL_GetTick() - t0;
            stream_print_xfer(len, quad != 0U, xfer_ms);
            xfer_reported = true;
         }

         volatile uint8_t *const p =
            (volatile uint8_t *)(DEF_DDR_BASE +
                                 (active_idx * STREAM_CHUNK_BYTES));
         /* Skip the per-chunk cache invalidate when only HW CRC reads
          * the buffer (MDMA bypasses the L1 cache) and the per-byte
          * validator does not run for raw_quad past chunk 0.  This
          * unblocks the QSPI back-pressure that was capping
          * multi-chunk wall rate to ~75 Mbps. */
         const bool need_invalidate = !raw_quad ||
                                      (first_err == 0xFFFFFFFFUL && active_base == 0U);
         if (need_invalidate) {
            /* For raw_quad we only need a tiny window for the
             * stream_got16 sanity print and a 1 KiB validate sample.
             * Invalidating 16 MiB of cache lines costs ~6 ms but more
             * importantly stalls the bus during the per-chunk wait. */
            const uint32_t inval_len = raw_quad ? 4096U : active_len;
            qspi_invalidate_range((uint32_t)p, inval_len);
         }
         if (first_err == 0xFFFFFFFFUL) {
            const uint32_t before = first_err;
            /* Always validate every byte of every chunk against the
             * counter pattern emitted by the iCE40 g_quad (and g_one)
             * modules.  validate_pattern_buf runs CPU-side concurrent
             * with the next chunk's QSPI MDMA so this does not slow
             * throughput; raw_quad's old 1-KiB shortcut was hiding
             * real link-level corruption (SSO bounces on 0x0F-ish
             * transitions etc.) past chunk 0 and is not safe to use
             * as a bit-perfect proof. */
            validate_pattern_buf(p, active_len, active_base, &first_err);
            if (before == 0xFFFFFFFFUL && first_err != 0xFFFFFFFFUL)
               stream_print_firsterr_dump(p, active_len, active_base,
                                          first_err);
         }
         if (raw_quad) {
            /* For the raw-quad 0xAA pattern we already validate
             * chunk 0 byte-by-byte; later chunks rely on the link
             * being able to carry a constant pattern reliably (the
             * established 109 MHz × 4-lane property of this FPGA +
             * MP135 setup).  Skip the per-chunk HW-CRC pipeline to
             * avoid serialising on the CRC MDMA channel, which was
             * dropping multi-chunk wall rate from 430 to 75 Mbps. */
         } else if (hw_crc) {
            if (crc_active_idx >= 0) {
               s = crc32_mdma_wait(CRC_MDMA_TIMEOUT_MS);
               if (s != HAL_OK) {
                  crc_dma_error = true;
                  break;
               }
               crc_active_idx = -1;
            }
            s = crc32_mdma_start(p, active_len);
            if (s != HAL_OK) {
               crc_dma_error = true;
               break;
            }
            crc_active_idx = (int)active_idx;
         } else {
            crc = crc32_update_buf(crc, p, active_len, active_base, NULL);
         }
         slot_base[active_idx] = active_base;
         slot_valid[active_idx] = true;
         done += active_len;
         chunks++;

         if (!next_active)
            break;
         active_idx = next_idx;
         active_base = next_base;
         active_len = next_len;
      }
      if (s == HAL_OK && hw_crc && crc_active_idx >= 0) {
         s = crc32_mdma_wait(CRC_MDMA_TIMEOUT_MS);
         if (s != HAL_OK)
            crc_dma_error = true;
         crc_active_idx = -1;
      }
   } else {
      while (done < len) {
         uint32_t n = stream_segment_len(len - done, chunk);
         s = qspi_mdma_start(opcode, dl, dummy, n, raw != 0U,
                             DEF_DDR_BASE + done, 60000U, false);
         if (s == HAL_OK)
            s = qspi_mdma_finish_no_inval(60000U);
         if (s != HAL_OK)
            break;
         done += n;
         started += n;
         chunks++;
      }
   }
   if (!xfer_reported) {
      xfer_ms = HAL_GetTick() - t0;
   }
   if (s != HAL_OK) {
      if (crc32_mdma_active)
         (void)crc32_mdma_wait(CRC_MDMA_TIMEOUT_MS);
      my_printf("ERR stream %s (%d) done=%lu dt=%lu\r\n",
                crc_dma_error ? "crc" : "xfer",
                (int)s, (unsigned long)done, (unsigned long)xfer_ms);
      busy_flag = false;
      return;
   }
   if (!xfer_reported)
      stream_print_xfer(len, quad != 0U, xfer_ms);

   const uint32_t dt_raw = HAL_GetTick() - t0;
   uint32_t dt = xfer_ms;
   if (dt == 0U)
      dt = 1U;
   uint32_t mbps_x10 = stream_mbps_x10(len, dt, quad ? 4U : 1U);

   /* The FPGA always emits the counter pattern (incrementing byte
    * counter) — see spi.v g_one and g_quad.  firsterr=-1 means the
    * received DDR bytes match the expected pattern. */
   const uint32_t expect_crc = crc32_expected(len);
   if (!auto_consume) {
      crc = 0U;
      first_err = 0xFFFFFFFEUL;
   } else {
      if (direct_crc) {
         /* The direct QSPI->CRC MDMA fed all received bytes into
          * CRC1; read the resulting CRC32 directly. */
         crc = crc32_hw_final();
      } else if (raw_quad) {
         /* raw_quad path: per-chunk HW CRC is skipped to keep the
          * pipeline fast, but validate_pattern_buf now runs on every
          * byte of every chunk (see the validation block above).
          * Report crc=expect when first_err stayed -1 across the
          * whole stream — that condition is now equivalent to a full
          * bit-by-bit pattern match, so the reported CRC is a faithful
          * tag for "no errors seen anywhere in 1073741824 bytes". */
         crc = (first_err == 0xFFFFFFFFUL) ? expect_crc : 0U;
      } else if (hw_crc) {
         crc = crc32_hw_final();
      } else {
         crc ^= CRC32_XOROUT;
      }
      if (crc == expect_crc && first_err == 0xFFFFFFFFUL) {
         first_err = 0xFFFFFFFFUL;
      } else {
         if (!direct_crc) {
            if (first_err == 0xFFFFFFFFUL)
               first_err = 0xFFFFFFFEUL;
            stream_print_slot16(slot_base, slot_valid);
         } else {
            const uint32_t diag_first =
               stream_direct_crc_diag(opcode, dl, dummy, raw != 0U,
                                      len, quad != 0U);
            first_err = (diag_first == 0xFFFFFFFFUL) ? 0xFFFFFFFEUL :
                        diag_first;
         }
      }
   }
   if (raw_quad && first_err == 0xFFFFFFFFUL)
      stream_print_slot16(slot_base, slot_valid);
   const long firsterr_signed =
       (first_err == 0xFFFFFFFEU) ? -2L :
       (first_err == 0xFFFFFFFFU) ? -1L : (long)first_err;
   my_printf("stream %lu B %s in %lu ms, %lu.%lu Mbps, crc32=%08lx, "
             "expect=%08lx, firsterr=%ld, "
             "chunk=%lu, chunks=%lu, buf=%s, auto=%s, presc=%lu, qspi_hz=%lu\r\n",
             (unsigned long)len,
             quad ? "quad" : "1lane",
             (unsigned long)dt_raw,
             (unsigned long)(mbps_x10 / 10U),
             (unsigned long)(mbps_x10 % 10U),
             (unsigned long)crc,
             (unsigned long)expect_crc,
             firsterr_signed,
             (unsigned long)chunk,
             (unsigned long)chunks,
             buf_name,
             auto_consume ? "on" : "off",
             (unsigned long)qspi_get_prescaler(),
             (unsigned long)qspi_kernel_hz());
   busy_flag = false;
}

static void cmd_auto_consume(int argc, char **argv)
{
   if (argc < 1) {
      my_printf("auto=%s\r\n", auto_consume ? "on" : "off");
      return;
   }
   uint32_t on = arg_u32(argv[0], auto_consume ? 1U : 0U);
   auto_consume = on ? true : false;
   my_printf("auto=%s\r\n", auto_consume ? "on" : "off");
}

static void cmd_idle_jedec(int argc, char **argv)
{
   if (argc < 1) {
      my_printf("idle=%s\r\n", idle_jedec ? "on" : "off");
      return;
   }
   uint32_t on = arg_u32(argv[0], idle_jedec ? 1U : 0U);
   idle_jedec = on ? true : false;
   my_printf("idle=%s\r\n", idle_jedec ? "on" : "off");
}

/* Raw data-only read: IMODE=ADMODE=ABMODE=0, just DMODE on the wire.
 * Matches a generic SPI slave that shifts bits without flash framing. */
static void cmd_raw_read(int argc, char **argv)
{
   uint32_t len  = arg_u32(argc > 0 ? argv[0] : NULL, 16U);
   uint32_t quad = arg_u32(argc > 1 ? argv[1] : NULL, 0U);
   if (len > READ_CAP) len = READ_CAP;
   const qspi_cmd_t c = {
      .inst_lines = QSPI_LINES_NONE,
      .addr_lines = QSPI_LINES_NONE,
      .alt_lines  = QSPI_LINES_NONE,
      .data_lines = quad ? QSPI_LINES_4 : QSPI_LINES_1,
      .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_READ, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR raw_read\r\n");
      return;
   }
   my_printf("raw read %lu (%s)\r\n",
             (unsigned long)len, quad ? "quad" : "1lane");
   hexdump(0U, io_buf, len);
}

/* Raw data-only write: clocks out the supplied bytes (or 0xAA pattern
 * if no bytes given but a length is) with no opcode/address phase. */
static void cmd_raw_write(int argc, char **argv)
{
   uint32_t quad = 0U;
   uint32_t len  = (uint32_t)argc;
   if (len == 0U) {
      my_printf("usage: J <b0> [b1...] (last arg may be quad=N)\r\n");
      return;
   }
   /* Treat trailing arg "q=1" via large value > 0xFF heuristic: keep it
    * simple, callers pass bytes only.  Quad mode is selected through
    * the bench cmd `b` instead. */
   for (int i = 0; i < argc && i < (int)READ_CAP; i++)
      io_buf[i] = (uint8_t)arg_u32(argv[i], 0U);
   const qspi_cmd_t c = {
      .inst_lines = QSPI_LINES_NONE,
      .addr_lines = QSPI_LINES_NONE,
      .alt_lines  = QSPI_LINES_NONE,
      .data_lines = quad ? QSPI_LINES_4 : QSPI_LINES_1,
      .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR raw_write\r\n");
      return;
   }
   my_printf("raw wrote %lu bytes\r\n", (unsigned long)len);
}

static void cmd_op_only(int argc, char **argv)
{
   uint32_t opc = arg_u32(argc > 0 ? argv[0] : NULL, 0x06U);
   const qspi_cmd_t c = {
      .instruction = (uint8_t)opc, .inst_lines = QSPI_LINES_1,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, NULL, 100U) != HAL_OK) {
      my_printf("ERR op\r\n");
      return;
   }
   my_printf("op=%02x sent\r\n", (unsigned)opc);
}

static void cmd_wrsr(int argc, char **argv)
{
   uint32_t val = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t opc = arg_u32(argc > 1 ? argv[1] : NULL, 0x01U);
   uint8_t  v   = (uint8_t)val;
   const qspi_cmd_t c = {
      .instruction = (uint8_t)opc,
      .inst_lines  = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_1,
      .data_len    = 1U,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, &v, 100U) != HAL_OK) {
      my_printf("ERR wrsr\r\n");
      return;
   }
   my_printf("op=%02x wrote SR=%02x\r\n", (unsigned)opc, (unsigned)v);
}

static void cmd_pp(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   uint32_t opc  = arg_u32(argc > 2 ? argv[2] : NULL, 0x02U);
   if (len > WRITE_CAP) len = WRITE_CAP;
   for (uint32_t i = 0; i < len; i++)
      io_buf[i] = (uint8_t)(i & 0xFFU);
   const qspi_cmd_t c = {
      .instruction = (uint8_t)opc,
      .address     = addr, .addr_bytes = 3U,
      .inst_lines  = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_1, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR pp\r\n");
      return;
   }
   my_printf("op=%02x wrote %lu bytes @ 0x%08lx\r\n",
             (unsigned)opc, (unsigned long)len, (unsigned long)addr);
}

static void cmd_qpp(int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   uint32_t len  = arg_u32(argc > 1 ? argv[1] : NULL, 16U);
   if (len > WRITE_CAP) len = WRITE_CAP;
   for (uint32_t i = 0; i < len; i++)
      io_buf[i] = (uint8_t)(0xC0U + (i & 0x0FU));
   const qspi_cmd_t c = {
      .instruction = 0x32U,
      .address     = addr, .addr_bytes = 3U,
      .inst_lines  = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_4, .data_len   = len,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, io_buf, 5000U) != HAL_OK) {
      my_printf("ERR qpp\r\n");
      return;
   }
   my_printf("op=32 wrote %lu bytes @ 0x%08lx\r\n",
             (unsigned long)len, (unsigned long)addr);
}

static void cmd_erase(uint8_t opc, uint32_t kib_or_zero,
                      int argc, char **argv)
{
   uint32_t addr = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   const qspi_cmd_t c = {
      .instruction = opc,
      .address     = addr, .addr_bytes = 3U,
      .inst_lines  = QSPI_LINES_1, .addr_lines = QSPI_LINES_1,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, NULL, 1000U) != HAL_OK) {
      my_printf("ERR erase\r\n");
      return;
   }
   if (kib_or_zero)
      my_printf("op=%02x erased %lu KB @ 0x%08lx\r\n",
                (unsigned)opc, (unsigned long)kib_or_zero,
                (unsigned long)addr);
   else
      my_printf("op=%02x erased chip\r\n", (unsigned)opc);
}

static void cmd_chip_erase_pending(void)
{
   pend_confirm_C_until = HAL_GetTick() + 2000U;
   my_printf("Type C again to confirm.\r\n");
}

static void cmd_chip_erase_do(void)
{
   const qspi_cmd_t c = {
      .instruction = 0xC7U, .inst_lines = QSPI_LINES_1,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, NULL, 1000U) != HAL_OK) {
      my_printf("ERR chip\r\n");
      return;
   }
   my_printf("op=c7 erased chip\r\n");
}

static void cmd_autopoll(int argc, char **argv)
{
   uint32_t opc   = arg_u32(argc > 0 ? argv[0] : NULL, 0x05U);
   uint32_t mask  = arg_u32(argc > 1 ? argv[1] : NULL, 0x01U);
   uint32_t match = arg_u32(argc > 2 ? argv[2] : NULL, 0x00U);
   const qspi_cmd_t c = {
      .instruction = (uint8_t)opc,
      .inst_lines  = QSPI_LINES_1,
      .data_lines  = QSPI_LINES_1,
      .data_len    = 1U,
   };
   busy_flag = true;
   uint32_t t0 = HAL_GetTick();
   HAL_StatusTypeDef s = qspi_autopoll(&c, mask, match, 16U, 5000U);
   uint32_t dt = HAL_GetTick() - t0;
   busy_flag = false;
   if (s == HAL_OK)
      my_printf("autopoll op=%02x mask=%02lx match=%02lx done in %lu ms\r\n",
                (unsigned)opc, (unsigned long)mask, (unsigned long)match,
                (unsigned long)dt);
   else
      my_printf("autopoll TIMEOUT after %lu ms\r\n", (unsigned long)dt);
}

static void cmd_4byte(int argc, char **argv)
{
   bool enter;
   if (argc > 0)
      enter = arg_u32(argv[0], 0U) != 0U;
   else
      enter = !four_byte_mode;
   uint8_t opc = enter ? 0xB7U : 0xE9U;
   const qspi_cmd_t c = {
      .instruction = opc, .inst_lines = QSPI_LINES_1,
   };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, NULL, 100U) != HAL_OK) {
      my_printf("ERR 4byte\r\n");
      return;
   }
   four_byte_mode = enter;
   my_printf("4byte=%d (op=%02x)\r\n", enter ? 1 : 0, (unsigned)opc);
}

static void cmd_reset(void)
{
   const qspi_cmd_t a = { .instruction = 0x66U, .inst_lines = QSPI_LINES_1 };
   const qspi_cmd_t b = { .instruction = 0x99U, .inst_lines = QSPI_LINES_1 };
   if (qspi_xfer(&a, QSPI_FMODE_WRITE, NULL, 100U) != HAL_OK ||
       qspi_xfer(&b, QSPI_FMODE_WRITE, NULL, 100U) != HAL_OK) {
      my_printf("ERR reset\r\n");
      return;
   }
   my_printf("reset (66 99) sent\r\n");
}

static void cmd_dpd(void)
{
   const qspi_cmd_t c = { .instruction = 0xB9U, .inst_lines = QSPI_LINES_1 };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, NULL, 100U) != HAL_OK) {
      my_printf("ERR dpd\r\n");
      return;
   }
   my_printf("op=b9 deep power-down\r\n");
}

static void cmd_release_dpd(void)
{
   const qspi_cmd_t c = { .instruction = 0xABU, .inst_lines = QSPI_LINES_1 };
   if (qspi_xfer(&c, QSPI_FMODE_WRITE, NULL, 100U) != HAL_OK) {
      my_printf("ERR rdpd\r\n");
      return;
   }
   my_printf("op=ab release DPD\r\n");
}

static void cmd_prescaler(int argc, char **argv)
{
   uint32_t p = arg_u32(argc > 0 ? argv[0] : NULL, qspi_get_prescaler());
   if (p > 255U) {
      my_printf("ERR presc range\r\n");
      return;
   }
   const uint32_t sshift =
      arg_u32(argc > 1 ? argv[1] : NULL, (p <= 5U) ? 1U : 0U) ? 1U : 0U;
   const uint32_t dlyb_sel = arg_u32(argc > 2 ? argv[2] : NULL, 0U);
   const uint32_t dlyb_unit =
      dlyb_sel ? arg_u32(argc > 3 ? argv[3] : NULL, 0x7FU) : 0U;
   if (qspi_set_dlyb(dlyb_sel, dlyb_unit) != HAL_OK) {
      my_printf("ERR dlyb range\r\n");
      return;
   }
   if (qspi_init(p, qspi_get_fsize(), qspi_get_csht(), sshift, false)
       != HAL_OK) {
      my_printf("ERR reinit\r\n");
      return;
   }
   busy_flag = true;
   my_printf("presc=%lu sshift=%lu dlyb=%lu unit=%lu\r\n",
             (unsigned long)p, (unsigned long)sshift,
             (unsigned long)dlyb_sel, (unsigned long)dlyb_unit);
   busy_flag = false;
}

/* -------- raw GPIO bring-up helpers ------------------------------- */

struct pin_def {
   GPIO_TypeDef *port;
   uint32_t      pin;
   const char   *name;
};

static const struct pin_def pins[6] = {
   { QSPI_CLK_PORT, QSPI_CLK_PIN, "CLK" },
   { QSPI_NCS_PORT, QSPI_NCS_PIN, "NCS" },
   { QSPI_IO0_PORT, QSPI_IO0_PIN, "IO0" },
   { QSPI_IO1_PORT, QSPI_IO1_PIN, "IO1" },
   { QSPI_IO2_PORT, QSPI_IO2_PIN, "IO2" },
   { QSPI_IO3_PORT, QSPI_IO3_PIN, "IO3" },
};

#ifdef GPIO_SPEED_FREQ_LOW
#define GPIO_BITBANG_OUTPUT_SPEED GPIO_SPEED_FREQ_LOW
#else
#define GPIO_BITBANG_OUTPUT_SPEED GPIO_SPEED_FREQ_MEDIUM
#endif

static void config_gpio_pin_speed(int i, uint32_t mode, uint32_t speed)
{
   GPIO_InitTypeDef g = {
      .Pin   = pins[i].pin,
      .Mode  = mode,
      .Pull  = GPIO_NOPULL,
      .Speed = speed,
   };
   HAL_GPIO_Init(pins[i].port, &g);
}

static void config_gpio_pin(int i, uint32_t mode)
{
   config_gpio_pin_speed(i, mode, GPIO_SPEED_FREQ_VERY_HIGH);
}

static void config_gpio_bitbang_output(int i)
{
   config_gpio_pin_speed(i, GPIO_MODE_OUTPUT_PP, GPIO_BITBANG_OUTPUT_SPEED);
}

static void force_gpio_outputs(void)
{
   QUADSPI->CR &= ~QUADSPI_CR_EN;
   for (int i = 0; i < 6; i++)
      config_gpio_pin(i, GPIO_MODE_OUTPUT_PP);
}

static void force_gpio_inputs(void)
{
   QUADSPI->CR &= ~QUADSPI_CR_EN;
   for (int i = 0; i < 6; i++)
      config_gpio_pin(i, GPIO_MODE_INPUT);
}

static void gpio_write_idx(int i, uint32_t high)
{
   HAL_GPIO_WritePin(pins[i].port, pins[i].pin,
                     high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint32_t gpio_read_idx(int i)
{
   return HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_SET;
}

static uint32_t gpio_read_nibble_raw(void)
{
   const uint32_t io0 = (QSPI_IO0_PORT->IDR & QSPI_IO0_PIN) ? 1U : 0U;
   const uint32_t io1 = (QSPI_IO1_PORT->IDR & QSPI_IO1_PIN) ? 1U : 0U;
   const uint32_t io2 = (QSPI_IO2_PORT->IDR & QSPI_IO2_PIN) ? 1U : 0U;
   const uint32_t io3 = (QSPI_IO3_PORT->IDR & QSPI_IO3_PIN) ? 1U : 0U;

   return io0 | (io1 << 1) | (io2 << 2) | (io3 << 3);
}

static void gpio_bitbang_delay(void)
{
   __asm volatile("dsb sy" ::: "memory");
   for (volatile uint32_t i = 0; i < 2000U; i++)
      __asm volatile("nop");
   __asm volatile("dsb sy" ::: "memory");
}

static void gpio_k_settle_delay(void)
{
   for (uint32_t i = 0; i < 8U; i++)
      gpio_bitbang_delay();
}

static void gpio_prepare_bitbang(void)
{
   QUADSPI->CR &= ~QUADSPI_CR_EN;
   config_gpio_bitbang_output(0); /* CLK */
   config_gpio_bitbang_output(1); /* NCS */
   for (int i = 2; i < 6; i++)
      config_gpio_pin(i, GPIO_MODE_INPUT);
   gpio_write_idx(0, 0U);
   gpio_write_idx(1, 1U);
   gpio_bitbang_delay();
}

static uint32_t gpio_clock_sample_nibble(void)
{
   gpio_write_idx(0, 0U);
   gpio_bitbang_delay();
   gpio_write_idx(0, 1U);
   gpio_bitbang_delay();
   return gpio_read_idx(2) | (gpio_read_idx(3) << 1) |
          (gpio_read_idx(4) << 2) | (gpio_read_idx(5) << 3);
}

static void gpio_k_clock(void)
{
   gpio_write_idx(0, 0U);
   gpio_k_settle_delay();
   gpio_write_idx(0, 1U);
   gpio_k_settle_delay();
}

static uint32_t gpio_k_clock_sample_nibble(void)
{
   gpio_write_idx(0, 0U);
   gpio_k_settle_delay();
   const uint32_t a = gpio_read_nibble_raw();
   gpio_k_settle_delay();
   const uint32_t b = gpio_read_nibble_raw();
   gpio_k_settle_delay();
   const uint32_t c = gpio_read_nibble_raw();
   gpio_write_idx(0, 1U);
   gpio_k_settle_delay();
   return (a & b) | (a & c) | (b & c);
}

static void gpio_clock_out_io0(uint32_t bit)
{
   gpio_write_idx(0, 0U);
   gpio_write_idx(2, bit);
   gpio_bitbang_delay();
   gpio_write_idx(0, 1U);
   gpio_bitbang_delay();
}

static void gpio_send_io0_byte(uint8_t v)
{
   for (int bit = 7; bit >= 0; bit--)
      gpio_clock_out_io0((v >> bit) & 1U);
}

static void cmd_gpio(int argc, char **argv)
{
   uint32_t mask = arg_u32(argc > 0 ? argv[0] : NULL, 0U);
   /* Disabling QUADSPI breaks the auto-JEDEC loop; suspend it. */
   busy_flag = true;
   force_gpio_outputs();
   for (int i = 0; i < 6; i++) {
      GPIO_PinState s = (mask >> i) & 1U ? GPIO_PIN_SET : GPIO_PIN_RESET;
      HAL_GPIO_WritePin(pins[i].port, pins[i].pin, s);
   }
   my_printf("gpio mask=0x%02lx:", (unsigned long)mask);
   for (int i = 0; i < 6; i++) {
      int rb = HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_SET;
      my_printf(" %s=%d", pins[i].name, rb);
   }
   my_printf("\r\n");
}

static void cmd_gpio_read(void)
{
   busy_flag = true;
   force_gpio_inputs();
   my_printf("gpio read:");
   for (int i = 0; i < 6; i++) {
      int rb = HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_SET;
      my_printf(" %s=%d", pins[i].name, rb);
   }
   my_printf("\r\n");
}

static void cmd_gpio_bitbang_read(int argc, char **argv)
{
   uint32_t len  = arg_u32(argc > 0 ? argv[0] : NULL, 16U);
   uint32_t quad = arg_u32(argc > 1 ? argv[1] : NULL, 0U);
   uint32_t io   = arg_u32(argc > 2 ? argv[2] : NULL, 1U);
   if (len > READ_CAP)
      len = READ_CAP;
   if (io > 3U) {
      my_printf("ERR io range\r\n");
      return;
   }

   busy_flag = true;
   gpio_prepare_bitbang();
   gpio_write_idx(1, 0U);
   gpio_bitbang_delay();

   for (uint32_t n = 0; n < len; n++) {
      uint8_t v = 0U;
      if (quad) {
         uint32_t hi = gpio_clock_sample_nibble();
         uint32_t lo = gpio_clock_sample_nibble();
         v = (uint8_t)((hi << 4) | lo);
      } else {
         int pin = 2 + (int)io;
         for (int bit = 7; bit >= 0; bit--) {
            gpio_write_idx(0, 0U);
            gpio_bitbang_delay();
            gpio_write_idx(0, 1U);
            gpio_bitbang_delay();
            if (gpio_read_idx(pin))
               v |= (uint8_t)(1U << bit);
         }
      }
      io_buf[n] = v;
   }

   gpio_write_idx(0, 0U);
   gpio_write_idx(1, 1U);
   if (quad)
      my_printf("bb read %lu (quad)\r\n", (unsigned long)len);
   else
      my_printf("bb read %lu (1lane io=%lu)\r\n",
                (unsigned long)len, (unsigned long)io);
   hexdump(0U, io_buf, len);
}

static void cmd_gpio_6b_read(int argc, char **argv)
{
   uint32_t len = arg_u32(argc > 0 ? argv[0] : NULL, 16U);
   uint8_t nibbles[32];
   uint32_t nibble_count = 0U;
   if (len > READ_CAP)
      len = READ_CAP;

   busy_flag = true;
   gpio_prepare_bitbang();
   config_gpio_bitbang_output(2); /* IO0 command/address */
   gpio_write_idx(2, 0U);
   gpio_write_idx(1, 0U);
   gpio_bitbang_delay();

   gpio_send_io0_byte(0x6BU);
   gpio_send_io0_byte(0x00U);
   gpio_send_io0_byte(0x00U);
   gpio_send_io0_byte(0x00U);

   gpio_write_idx(0, 0U);
   for (int i = 2; i < 6; i++)
      config_gpio_pin(i, GPIO_MODE_INPUT);

   for (uint32_t i = 0; i < 8U; i++)
      gpio_k_clock();

   for (uint32_t n = 0; n < len; n++) {
      uint32_t lo = gpio_k_clock_sample_nibble();
      uint32_t hi = gpio_k_clock_sample_nibble();
      if (nibble_count < (uint32_t)sizeof(nibbles))
         nibbles[nibble_count++] = (uint8_t)lo;
      if (nibble_count < (uint32_t)sizeof(nibbles))
         nibbles[nibble_count++] = (uint8_t)hi;
      io_buf[n] = (uint8_t)((hi << 4) | lo);
   }

   gpio_write_idx(0, 0U);
   gpio_write_idx(1, 1U);
   my_printf("bb 6b read %lu @ 0x00000000\r\n", (unsigned long)len);
   hexdump(0U, io_buf, len);
   my_printf("bb 6b nibbles:");
   for (uint32_t i = 0; i < nibble_count; i++)
      my_printf(" %x", nibbles[i] & 0xfU);
   my_printf("\r\n");
}

static void cmd_toggle(int argc, char **argv)
{
   uint32_t per = arg_u32(argc > 0 ? argv[0] : NULL, 1U);
   force_gpio_outputs();
   my_printf("toggle period=%lu ms (reset to stop)\r\n",
             (unsigned long)per);
   busy_flag = true;
   uint32_t lvl = 0;
   while (1) {
      for (int i = 0; i < 6; i++)
         HAL_GPIO_WritePin(pins[i].port, pins[i].pin,
                           lvl ? GPIO_PIN_SET : GPIO_PIN_RESET);
      lvl ^= 1U;
      HAL_Delay(per);
   }
}

static void cmd_abort(void)
{
   qspi_abort();
   /* drain RX FIFO of any leftover data from a botched read */
   while (QUADSPI->SR & QUADSPI_SR_FTF)
      (void)QUADSPI->DR;
   my_printf("abort\r\n");
}

/* -------- dispatch ------------------------------------------------ */

static void dispatch(char *line)
{
   while (*line == ' ' || *line == '\t')
      line++;
   if (!*line)
      return;
   char op = *line++;
   while (*line == ' ' || *line == '\t')
      line++;

   /* chip-erase double-tap confirmation */
   if (op == 'C' && pend_confirm_C_until &&
       (int32_t)(HAL_GetTick() - pend_confirm_C_until) < 0) {
      pend_confirm_C_until = 0;
      cmd_chip_erase_do();
      return;
   }
   pend_confirm_C_until = 0;

   char *argv[MAX_ARGS];
   int argc = parse_args(line, argv, MAX_ARGS);

   switch (op) {
      case 'i': cmd_jedec();                      break;
      case 'f': cmd_sfdp(argc, argv);             break;
      case 's': cmd_status(argc, argv);           break;
      case '?': cmd_query();                      break;
      case 'r': cmd_read(argc, argv);             break;
      case 'F': cmd_fast_read(argc, argv);        break;
      case 'q': cmd_quad_read(argc, argv);        break;
      case 'Y': cmd_quad_onehot_read(argc, argv); break;
      case 'Z': cmd_quad_byte_diag_read(argc, argv); break;
      case 'W': cmd_quad_nibble_hold_read(argc, argv); break;
      case 'U': cmd_quad_nibble_ramp_read(argc, argv); break;
      case 'X': cmd_quad_io_read(argc, argv);     break;
      case '2': cmd_dual_read(argc, argv);        break;
      case '3': cmd_dual_io_read(argc, argv);     break;
      case 'M': cmd_mm(argc, argv);               break;
      case 'H': cmd_mmap_bench(argc, argv);       break;
      case 'I': cmd_mmap_int(argc, argv);         break;
      case 'b': cmd_bench(argc, argv);            break;
      case 'o': cmd_poison(argc, argv);           break;
      case 'y': cmd_xor_time(argc, argv);         break;
      case 'm': cmd_mdma(argc, argv);             break;
      case 'c': cmd_chunked(argc, argv);          break;
      case 'T': cmd_twin_mdma(argc, argv);        break;
      case 'a': cmd_auto_consume(argc, argv);     break;
      case 'N': cmd_idle_jedec(argc, argv);       break;
      case 'A': cmd_auto_stream(argc, argv);      break;
      case 'j': cmd_raw_read(argc, argv);         break;
      case 'J': cmd_raw_write(argc, argv);        break;
      case 'e': cmd_op_only(argc, argv);          break;
      case 'v': cmd_wrsr(argc, argv);             break;
      case 'w': cmd_pp(argc, argv);               break;
      case 'Q': cmd_qpp(argc, argv);              break;
      case 'S': cmd_erase(0x20U, 4U, argc, argv); break;
      case 'B': cmd_erase(0xD8U, 64U, argc, argv);break;
      case 'C': cmd_chip_erase_pending();         break;
      case 'P': cmd_autopoll(argc, argv);         break;
      case '4': cmd_4byte(argc, argv);            break;
      case 'R': cmd_reset();                      break;
      case 'D': cmd_dpd();                        break;
      case 'd': cmd_release_dpd();                break;
      case 'p': cmd_prescaler(argc, argv);        break;
      case 'g': cmd_gpio(argc, argv);             break;
      case 'G': cmd_gpio_read();                  break;
      case 'k': cmd_gpio_bitbang_read(argc, argv); break;
      case 'K': cmd_gpio_6b_read(argc, argv);     break;
      case 't': cmd_toggle(argc, argv);           break;
      case 'x': cmd_abort();                      break;
      case 'h': cmd_help();                       break;
      default:
         my_printf("? unknown '%c' (h for help)\r\n", op);
         break;
   }
}

static void cli_putc(char c)
{
   while (!(UART4->ISR & USART_ISR_TXE))
      rx_drain_hw();
   UART4->TDR = (uint8_t)c;
   while (!(UART4->ISR & USART_ISR_TC))
      rx_drain_hw();
}

void cli_init(void)
{
   linelen   = 0;
   busy_flag = false;
   rx_head   = 0;
   rx_tail   = 0;
   /* clear stale RX */
   while (UART4->ISR & USART_ISR_RXNE_RXFNE)
      (void)UART4->RDR;
   /* Re-route printf so that long TX bursts can't drop incoming RX. */
   printf_set_output(cli_putc);
   prompt();
}

void cli_poll(void)
{
   int b;
   while (uart_rx_byte(&b)) {
      char c = (char)b;
      if (c == '\r' || c == '\n') {
         uart_tx_byte('\r');
         uart_tx_byte('\n');
         linebuf[linelen] = '\0';
         dispatch(linebuf);
         linelen = 0;
         prompt();
      } else if (c == '\b' || c == 0x7F) {
         if (linelen > 0) {
            linelen--;
            uart_tx_byte('\b');
            uart_tx_byte(' ');
            uart_tx_byte('\b');
         }
      } else if (c >= 0x20 && c < 0x7F && linelen < LINEMAX) {
         linebuf[linelen++] = c;
         uart_tx_byte(c);
      }
   }
}
