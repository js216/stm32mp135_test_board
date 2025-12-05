# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Stanford Research Systems, Inc.

file build/main.elf
target remote localhost:2330
monitor reset
monitor flash device=STM32MP135F
load build/main.elf
monitor go
break main
step
