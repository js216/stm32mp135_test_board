// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file cli.h
 * @brief Single-character command shell over UART4 for QSPI bring-up.
 * @author Jakob Kastelic
 * @copyright 2026 Jakob Kastelic
 */

#ifndef CLI_H
#define CLI_H

#include <stdbool.h>

void cli_init(void);
void cli_poll(void);
bool cli_busy(void);
bool cli_idle_jedec_enabled(void);

#endif
