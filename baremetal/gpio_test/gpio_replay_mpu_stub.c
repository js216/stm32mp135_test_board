// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Jakob Kastelic

#include <string.h>

#include "connectivity_mpu_replay.h"

int gpio_connectivity_mpu_replay_stub_run(void);

static int mpu_drive(const char *signal, int value)
{
    return signal != 0 && (value == 0 || value == 1);
}

static int mpu_sample_expect(const char *signal, int expected)
{
    return signal != 0 && (expected == 0 || expected == 1);
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

#ifdef GPIO_REPLAY_STUB_MAIN
int main(void)
{
    return gpio_connectivity_mpu_replay_stub_run();
}
#endif
