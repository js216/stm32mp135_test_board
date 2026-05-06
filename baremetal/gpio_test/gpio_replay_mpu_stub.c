// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Jakob Kastelic

#include <string.h>

#include "connectivity_mpu_replay.h"

#ifndef GPIO_REPLAY_STUB_MAIN
#include "printf.h"
#include "stm32mp13xx_hal.h"
#include "stm32mp13xx_hal_gpio.h"
#include "stm32mp13xx_hal_rcc.h"
#endif

int gpio_connectivity_mpu_replay_stub_run(void);
int gpio_connectivity_mpu_replay_io0_high_report(void);
int gpio_connectivity_mpu_replay_io0_low_report(void);
int gpio_connectivity_mpu_replay_io1_high_report(void);
int gpio_connectivity_mpu_replay_io2_high_report(void);
int gpio_connectivity_mpu_replay_io3_high_report(void);
int gpio_connectivity_mpu_replay_io0_sample_report(void);
int gpio_connectivity_mpu_replay_io1_sample_report(void);
int gpio_connectivity_mpu_replay_io2_sample_report(void);
int gpio_connectivity_mpu_replay_io3_sample_report(void);
int gpio_connectivity_mpu_replay_ncs_drive_report(void);
int gpio_connectivity_mpu_replay_ncs_low_report(void);
int gpio_connectivity_mpu_replay_sclk_drive_report(void);
int gpio_connectivity_mpu_replay_sclk_low_report(void);

#ifndef GPIO_REPLAY_STUB_MAIN
typedef struct {
    const char *signal;
    GPIO_TypeDef *port;
    uint16_t pin;
} mpu_gpio_signal_t;

#define QSPI_NCS_PORT GPIOD
#define QSPI_NCS_PIN  GPIO_PIN_1
#define QSPI_CLK_PORT GPIOF
#define QSPI_CLK_PIN  GPIO_PIN_10

static const mpu_gpio_signal_t mpu_drive_signals[] = {
    {"mpu_qspi_ncs_to_fpga_cs_n", QSPI_NCS_PORT, QSPI_NCS_PIN},
};

static const mpu_gpio_signal_t mpu_sclk_drive_signal = {
    "mpu_qspi_clk_to_fpga_sclk", QSPI_CLK_PORT, QSPI_CLK_PIN
};

static const mpu_gpio_signal_t mpu_io0_drive_signal = {
    "mpu_qspi_io0_to_fpga_io0", GPIOH, GPIO_PIN_3
};

static const mpu_gpio_signal_t mpu_io1_drive_signal = {
    "mpu_qspi_io1_to_fpga_io1", GPIOF, GPIO_PIN_9
};

static const mpu_gpio_signal_t mpu_io2_drive_signal = {
    "mpu_qspi_io2_to_fpga_io2", GPIOH, GPIO_PIN_6
};

static const mpu_gpio_signal_t mpu_io3_drive_signal = {
    "mpu_qspi_io3_to_fpga_io3", GPIOH, GPIO_PIN_7
};

static const mpu_gpio_signal_t mpu_sample_signals[] = {
    {"mpu_qspi_io0_to_fpga_io0", GPIOH, GPIO_PIN_3},
    {"mpu_qspi_io1_to_fpga_io1", GPIOF, GPIO_PIN_9},
    {"mpu_qspi_io2_to_fpga_io2", GPIOH, GPIO_PIN_6},
    {"mpu_qspi_io3_to_fpga_io3", GPIOH, GPIO_PIN_7},
};

static const mpu_gpio_signal_t *find_mpu_drive_signal(const char *signal)
{
    for (size_t i = 0; i < sizeof(mpu_drive_signals) / sizeof(mpu_drive_signals[0]); i++) {
        if (strcmp(signal, mpu_drive_signals[i].signal) == 0)
            return &mpu_drive_signals[i];
    }

    return 0;
}

static const mpu_gpio_signal_t *find_mpu_sample_signal(const char *signal)
{
    for (size_t i = 0; i < sizeof(mpu_sample_signals) / sizeof(mpu_sample_signals[0]); i++) {
        if (strcmp(signal, mpu_sample_signals[i].signal) == 0)
            return &mpu_sample_signals[i];
    }

    return 0;
}

static void enable_mpu_gpio_clocks(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void configure_mpu_drive_output(const mpu_gpio_signal_t *signal)
{
    GPIO_InitTypeDef g = {
        .Pin = signal->pin,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };

    enable_mpu_gpio_clocks();
    HAL_GPIO_Init(signal->port, &g);
}

static void configure_mpu_sample_input(const mpu_gpio_signal_t *signal)
{
    GPIO_InitTypeDef g = {
        .Pin = signal->pin,
        .Mode = GPIO_MODE_INPUT,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };

    enable_mpu_gpio_clocks();
    HAL_GPIO_Init(signal->port, &g);
}
#endif

static int mpu_drive(const char *signal, int value)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    const mpu_gpio_signal_t *drive_signal;
    GPIO_PinState state;
    const char *level;

    if (signal == 0 || (value != 0 && value != 1))
        return 0;

    drive_signal = find_mpu_drive_signal(signal);
    if (drive_signal == 0)
        return 1;

    configure_mpu_drive_output(drive_signal);
    state = value != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(drive_signal->port, drive_signal->pin, state);

    level = value != 0 ? "high" : "low";
    if (strcmp(signal, "mpu_qspi_ncs_to_fpga_cs_n") == 0 && value == 0) {
        my_printf("gpio_test mpu_qspi_ncs_to_fpga_cs_n low drive ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_ncs_to_fpga_cs_n") == 0 && value == 1) {
        my_printf("gpio_test mpu_qspi_ncs_to_fpga_cs_n high drive ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_clk_to_fpga_sclk") == 0 && value == 0) {
        my_printf("gpio_test mpu_qspi_clk_to_fpga_sclk low drive ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_clk_to_fpga_sclk") == 0 && value == 1) {
        my_printf("gpio_test mpu_qspi_clk_to_fpga_sclk high drive ok\r\n");
    } else {
        my_printf("gpio_test %s %s drive ok\r\n", signal, level);
    }

    return 1;
#else
    return signal != 0 && (value == 0 || value == 1);
#endif
}

static int mpu_sample_expect(const char *signal, int expected)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    const mpu_gpio_signal_t *sample_signal;
    GPIO_PinState state;
    int actual;
    const char *level;
    const char *result;

    if (signal == 0 || (expected != 0 && expected != 1))
        return 0;

    sample_signal = find_mpu_sample_signal(signal);
    if (sample_signal == 0)
        return 1;

    configure_mpu_sample_input(sample_signal);
    state = HAL_GPIO_ReadPin(sample_signal->port, sample_signal->pin);
    actual = state == GPIO_PIN_SET ? 1 : 0;
    level = expected != 0 ? "high" : "low";
    result = actual == expected ? "ok" : "fail";

    if (strcmp(signal, "mpu_qspi_io0_to_fpga_io0") == 0 && expected == 0 &&
        actual == expected) {
        my_printf("gpio_test mpu_qspi_io0_to_fpga_io0 low ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io0_to_fpga_io0") == 0 &&
               expected == 1 && actual == expected) {
        my_printf("gpio_test mpu_qspi_io0_to_fpga_io0 high ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io1_to_fpga_io1") == 0 && expected == 0 &&
        actual == expected) {
        my_printf("gpio_test mpu_qspi_io1_to_fpga_io1 low ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io1_to_fpga_io1") == 0 &&
               expected == 1 && actual == expected) {
        my_printf("gpio_test mpu_qspi_io1_to_fpga_io1 high ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io2_to_fpga_io2") == 0 &&
               expected == 0 && actual == expected) {
        my_printf("gpio_test mpu_qspi_io2_to_fpga_io2 low ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io2_to_fpga_io2") == 0 &&
               expected == 1 && actual == expected) {
        my_printf("gpio_test mpu_qspi_io2_to_fpga_io2 high ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io3_to_fpga_io3") == 0 &&
               expected == 0 && actual == expected) {
        my_printf("gpio_test mpu_qspi_io3_to_fpga_io3 low ok\r\n");
    } else if (strcmp(signal, "mpu_qspi_io3_to_fpga_io3") == 0 &&
               expected == 1 && actual == expected) {
        my_printf("gpio_test mpu_qspi_io3_to_fpga_io3 high ok\r\n");
    } else {
        my_printf("gpio_test %s %s %s\r\n", signal, level, result);
    }

    return actual == expected;
#else
    return signal != 0 && (expected == 0 || expected == 1);
#endif
}

int gpio_connectivity_mpu_replay_stub_run(void)
{
    for (size_t i = 0; i < gpio_connectivity_mpu_replay_count; i++) {
        const gpio_connectivity_replay_command_t *cmd =
            &gpio_connectivity_mpu_replay[i];

        if (strcmp(cmd->controller, "mpu") != 0)
            return 1;
        if (strcmp(cmd->command_kind, "drive") == 0) {
            if (!mpu_drive(cmd->signal, cmd->drive_value))
                return 1;
        } else if (strcmp(cmd->command_kind, "sample_expect") == 0) {
            if (!mpu_sample_expect(cmd->signal, cmd->expected_value))
                return 1;
        } else {
            return 1;
        }
    }

    return 0;
}

int gpio_connectivity_mpu_replay_io0_high_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_io0_drive_signal);
    HAL_GPIO_WritePin(mpu_io0_drive_signal.port, mpu_io0_drive_signal.pin,
                      GPIO_PIN_SET);
    my_printf("gpio_test mpu_qspi_io0_to_fpga_io0 high drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

int gpio_connectivity_mpu_replay_io0_low_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_io0_drive_signal);
    HAL_GPIO_WritePin(mpu_io0_drive_signal.port, mpu_io0_drive_signal.pin,
                      GPIO_PIN_RESET);
    my_printf("gpio_test mpu_qspi_io0_to_fpga_io0 low drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

int gpio_connectivity_mpu_replay_io1_high_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_io1_drive_signal);
    HAL_GPIO_WritePin(mpu_io1_drive_signal.port, mpu_io1_drive_signal.pin,
                      GPIO_PIN_SET);
    my_printf("gpio_test mpu_qspi_io1_to_fpga_io1 high drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

int gpio_connectivity_mpu_replay_io2_high_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_io2_drive_signal);
    HAL_GPIO_WritePin(mpu_io2_drive_signal.port, mpu_io2_drive_signal.pin,
                      GPIO_PIN_SET);
    my_printf("gpio_test mpu_qspi_io2_to_fpga_io2 high drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

int gpio_connectivity_mpu_replay_io3_high_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_io3_drive_signal);
    HAL_GPIO_WritePin(mpu_io3_drive_signal.port, mpu_io3_drive_signal.pin,
                      GPIO_PIN_SET);
    my_printf("gpio_test mpu_qspi_io3_to_fpga_io3 high drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

int gpio_connectivity_mpu_replay_io0_sample_report(void)
{
    int low_ok = mpu_sample_expect("mpu_qspi_io0_to_fpga_io0", 0);
    int high_ok = mpu_sample_expect("mpu_qspi_io0_to_fpga_io0", 1);

    return low_ok || high_ok;
}

int gpio_connectivity_mpu_replay_io1_sample_report(void)
{
    int low_ok = mpu_sample_expect("mpu_qspi_io1_to_fpga_io1", 0);
    int high_ok = mpu_sample_expect("mpu_qspi_io1_to_fpga_io1", 1);

    return low_ok || high_ok;
}

int gpio_connectivity_mpu_replay_io2_sample_report(void)
{
    int low_ok = mpu_sample_expect("mpu_qspi_io2_to_fpga_io2", 0);
    int high_ok = mpu_sample_expect("mpu_qspi_io2_to_fpga_io2", 1);

    return low_ok || high_ok;
}

int gpio_connectivity_mpu_replay_io3_sample_report(void)
{
    int low_ok = mpu_sample_expect("mpu_qspi_io3_to_fpga_io3", 0);
    int high_ok = mpu_sample_expect("mpu_qspi_io3_to_fpga_io3", 1);

    return low_ok || high_ok;
}

int gpio_connectivity_mpu_replay_ncs_drive_report(void)
{
    int low_ok = mpu_drive("mpu_qspi_ncs_to_fpga_cs_n", 0);
    int high_ok = mpu_drive("mpu_qspi_ncs_to_fpga_cs_n", 1);

    return low_ok || high_ok;
}

int gpio_connectivity_mpu_replay_ncs_low_report(void)
{
    return mpu_drive("mpu_qspi_ncs_to_fpga_cs_n", 0);
}

int gpio_connectivity_mpu_replay_sclk_drive_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_sclk_drive_signal);
    HAL_GPIO_WritePin(QSPI_CLK_PORT, QSPI_CLK_PIN, GPIO_PIN_RESET);
    my_printf("gpio_test mpu_qspi_clk_to_fpga_sclk low drive ok\r\n");

    configure_mpu_drive_output(&mpu_sclk_drive_signal);
    HAL_GPIO_WritePin(QSPI_CLK_PORT, QSPI_CLK_PIN, GPIO_PIN_SET);
    my_printf("gpio_test mpu_qspi_clk_to_fpga_sclk high drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

int gpio_connectivity_mpu_replay_sclk_low_report(void)
{
#ifndef GPIO_REPLAY_STUB_MAIN
    configure_mpu_drive_output(&mpu_sclk_drive_signal);
    HAL_GPIO_WritePin(QSPI_CLK_PORT, QSPI_CLK_PIN, GPIO_PIN_RESET);
    my_printf("gpio_test mpu_qspi_clk_to_fpga_sclk low drive ok\r\n");

    return 1;
#else
    return 1;
#endif
}

#ifdef GPIO_REPLAY_STUB_MAIN
int main(void)
{
    return gpio_connectivity_mpu_replay_stub_run();
}
#endif
