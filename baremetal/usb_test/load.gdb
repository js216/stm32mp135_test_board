set confirm off
set pagination off

define mrc
  load
  step
end

file build/main.elf
target remote localhost:2330
monitor reset
tbreak *0x2FFE0000
monitor flash device=STM32MP135F
load build/main.elf
monitor go
step
