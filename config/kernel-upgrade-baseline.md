# Kernel Upgrade Baseline

current linux commit: 47937a24f0ed893d100fe94a566c6bd83dd78de5
target linux commit: 80d29586bafa4734bad35a8f063919fdd79f5dec
target branch: origin/v6.6-stm32mp

Boot constraints:
- keep the local single-stage bootloader path
- no TF-A
- no OP-TEE
- no U-Boot
