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
