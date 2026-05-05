# SPDX-License-Identifier: BSD-3-Clause
# load.gdb --- TODO: description
# Copyright (c) 2026 Jakob Kastelic
set confirm off
set pagination off

file build/main.elf
target remote localhost:2330
monitor reset
monitor flash device=STM32MP135F
load build/main.elf
monitor go
break main
step
