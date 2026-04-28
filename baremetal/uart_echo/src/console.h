// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file console.h
 * @brief UART receive buffer and interrupt flag
 * @author Jakob Kastelic
 * @copyright 2026 Stanford Research Systems, Inc.
 *
 * This module sits below the command interpreter.  It owns the character
 * ring buffer and the Ctrl-C interrupt flag so that:
 *   - setup.c (UART IRQ) can push characters without depending on cmd.h
 *   - fmc.c (long operations) can check for interruption without depending
 *     on cmd.h
 *   - cmd.c depends on console.h, not the other way around
 */

#ifndef CONSOLE_H
#define CONSOLE_H

/* Feed one received character into the ring buffer.
 * Ctrl-C (0x03) sets the interrupt flag instead of enqueuing. */
void console_push(char c);

/* Return 1 and clear the flag if Ctrl-C was received since last call. */
int console_interrupted(void);

/* Return 1 if at least one character has arrived since the last call;
 * clears the flag on read (used by cmd_autoboot). */
int console_key_pressed(void);

/* Return 1 if the ring buffer has no pending characters. */
int console_rx_empty(void);

/* Remove and return the next character from the ring buffer.
 * Undefined behaviour if console_rx_empty() is true. */
char console_rx_get(void);

#endif // CONSOLE_H

// end file console.h
