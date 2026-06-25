// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Jakob Kastelic
//
// Custom-board GPIO connectivity sweep. The SRS STM32MP135 PCB routes 22
// SPI/QUADSPI signals out of header J503; each is wired to a ball on
// the iCE40-HX8K-B-EVN. This firmware drives one of those MP135 pins at a
// time so the FPGA sampler (which holds every sampled ball at its weak
// internal pull-up) can report which ball changed: an input reads 1 unless
// actively driven, so driving a pin LOW makes exactly its wired ball read 0,
// and that single zero bit identifies the connection.
//
// Built only for BOARD_CUSTOM; under EVB this is an empty translation unit so
// the eval-board mission keeps the original gpio_replay_mpu_stub.c behaviour.

#ifdef BOARD_CUSTOM

#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "gpio_custom.h"
#include "printf.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_rcc.h"

typedef struct {
   const char   *name;
   GPIO_TypeDef *port;
   uint16_t      pin;
} custom_signal_t;

// Order is the discovery index sent over UART (two hex digits). Pin map
// extracted from kicad Rev_B mpu_dsp_fpga.kicad_sch (net label -> primary
// STM32MP135 pin name, e.g. "PE2/SPI5_MOSI").
static const custom_signal_t custom_signals[] = {
   {"QUADSPI_CLK", GPIOF, GPIO_PIN_10},
   {"QUADSPI_CS",  GPIOE, GPIO_PIN_14},
   {"QUADSPI_IO0", GPIOF, GPIO_PIN_8},
   {"QUADSPI_IO1", GPIOF, GPIO_PIN_9},
   {"QUADSPI_IO2", GPIOD, GPIO_PIN_7},
   {"QUADSPI_IO3", GPIOH, GPIO_PIN_7},
   {"SPI1_SCK",    GPIOB, GPIO_PIN_1},
   {"SPI1_MOSI",   GPIOA, GPIO_PIN_3},
   {"SPI1_MISO",   GPIOC, GPIO_PIN_3},
   {"SPI1_CS",     GPIOF, GPIO_PIN_12},
   {"SPI2_SCK",    GPIOH, GPIO_PIN_6},
   {"SPI2_MOSI",   GPIOH, GPIO_PIN_10},
   {"SPI2_MISO",   GPIOG, GPIO_PIN_1},
   {"SPI2_CS",     GPIOB, GPIO_PIN_13},
   {"SPI4_SCK",    GPIOB, GPIO_PIN_4},
   {"SPI4_MOSI",   GPIOE, GPIO_PIN_11},
   {"SPI4_MISO",   GPIOE, GPIO_PIN_13},
   {"SPI4_CS",     GPIOD, GPIO_PIN_10},
   {"SPI5_SCK",    GPIOG, GPIO_PIN_10},
   {"SPI5_MOSI",   GPIOE, GPIO_PIN_2},
   {"SPI5_MISO",   GPIOG, GPIO_PIN_8},
   {"SPI5_CS",     GPIOH, GPIO_PIN_11},
};
#define CUSTOM_SIGNAL_COUNT (sizeof(custom_signals) / sizeof(custom_signals[0]))

static void custom_enable_gpio_clocks(void)
{
   __HAL_RCC_GPIOA_CLK_ENABLE();
   __HAL_RCC_GPIOB_CLK_ENABLE();
   __HAL_RCC_GPIOC_CLK_ENABLE();
   __HAL_RCC_GPIOD_CLK_ENABLE();
   __HAL_RCC_GPIOE_CLK_ENABLE();
   __HAL_RCC_GPIOF_CLK_ENABLE();
   __HAL_RCC_GPIOG_CLK_ENABLE();
   __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void custom_config_input(const custom_signal_t *s)
{
   GPIO_InitTypeDef g = {
      .Pin   = s->pin,
      .Mode  = GPIO_MODE_INPUT,
      .Pull  = GPIO_NOPULL,
      .Speed = GPIO_SPEED_FREQ_LOW,
   };
   HAL_GPIO_Init(s->port, &g);
}

static void custom_config_output(const custom_signal_t *s)
{
   GPIO_InitTypeDef g = {
      .Pin   = s->pin,
      .Mode  = GPIO_MODE_OUTPUT_PP,
      .Pull  = GPIO_NOPULL,
      .Speed = GPIO_SPEED_FREQ_LOW,
   };
   HAL_GPIO_Init(s->port, &g);
}

static void custom_release_all(void)
{
   for (size_t i = 0; i < CUSTOM_SIGNAL_COUNT; i++)
      custom_config_input(&custom_signals[i]);
   my_printf("gpio_test release all ok\r\n");
}

static void custom_drive(size_t idx, int high)
{
   const custom_signal_t *s = &custom_signals[idx];
   custom_config_output(s);
   HAL_GPIO_WritePin(s->port, s->pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
   my_printf("gpio_test drive %s %s ok\r\n", s->name, high ? "high" : "low");
}

static int hexval(char c)
{
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
   if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
   return -1;
}

// Command protocol (one command per UART write, no terminator needed):
//   D<nn><L|H>  drive signal index nn (two hex digits) low/high, push-pull
//   R           release every signal back to high-impedance input
// A bad hex digit or out-of-range index aborts the in-flight command.
void gpio_custom_run(void)
{
   enum { IDLE, WANT_D1, WANT_D0, WANT_LEVEL } st = IDLE;
   int d1 = 0, d0 = 0;

   custom_enable_gpio_clocks();
   custom_release_all();
   my_printf("gpio_test custom ready\r\n");

   for (;;) {
      while (!console_rx_empty()) {
         char c = console_rx_get();

         switch (st) {
         case IDLE:
            if (c == 'D')
               st = WANT_D1;
            else if (c == 'R')
               custom_release_all();
            break;
         case WANT_D1:
            d1 = hexval(c);
            st = (d1 < 0) ? IDLE : WANT_D0;
            break;
         case WANT_D0:
            d0 = hexval(c);
            st = (d0 < 0) ? IDLE : WANT_LEVEL;
            break;
         case WANT_LEVEL: {
            size_t idx = (size_t)(d1 * 16 + d0);
            if (idx < CUSTOM_SIGNAL_COUNT) {
               if (c == 'H')
                  custom_drive(idx, 1);
               else if (c == 'L')
                  custom_drive(idx, 0);
            }
            st = IDLE;
            break;
         }
         }
      }
   }
}

#endif /* BOARD_CUSTOM */
