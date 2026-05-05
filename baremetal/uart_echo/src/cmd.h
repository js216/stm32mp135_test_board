// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file cmd.h
 * @brief Minimal line-edit shell for uart_echo.
 * @author Jakob Kastelic
 * @copyright 2026 Jakob Kastelic
 */

#ifndef CMD_H
#define CMD_H

#include <stdint.h>

void cmd_init(void);
void cmd_poll(void);
void cmd_reset(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif
