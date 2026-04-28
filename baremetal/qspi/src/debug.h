// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file debug.h
 * @brief Debugging and diagnostics.
 * @author Jakob Kastelic
 * @copyright 2025 Stanford Research Systems, Inc.
 */

#ifndef DEBUG_H
#define DEBUG_H

#define assert_param(expr)                                                     \
   ((expr) ? (void)0U : error_msg(__FILE__, __LINE__, "assert failed"))

#define ERROR(msg) error_msg(__FILE__, __LINE__, msg)

void error_msg(const char *file, const int line, const char *msg);

#endif // DEBUG_H
