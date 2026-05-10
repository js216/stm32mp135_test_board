# SPDX-License-Identifier: MIT
# Makefile --- TODO: description
# Copyright (c) 2026 Jakob Kastelic
DTS = custom
#DTS = stm32mp135f-dk

LO = ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

.PHONY: all boot patch pmic clock dts kernel dtb save br sd nand copy clean

all: boot kernel dtb br sd copy

boot:
	$(MAKE) -C bootloader clean
	$(MAKE) -C bootloader -j$(shell nproc) #CFLAGS_EXTRA="-DEVB"

patch:
	@if git -C linux apply -R --check ../config/patch.linux 2>/dev/null; then \
		echo "patch.linux already applied"; \
	else \
		git -C linux apply ../config/patch.linux; \
	fi

pmic:
	@if git -C linux apply -R --check ../config/patch.pmic 2>/dev/null; then \
		echo "patch.pmic already applied"; \
	else \
		git -C linux apply ../config/patch.pmic; \
	fi

clock:
	@if git -C linux apply -R --check ../config/patch.clock 2>/dev/null; then \
		echo "patch.clock already applied"; \
	else \
		git -C linux apply ../config/patch.clock; \
	fi

dts:
	@if git -C linux apply -R --check ../config/patch.dts 2>/dev/null; then \
		echo "patch.dts already applied"; \
	else \
		git -C linux apply ../config/patch.dts; \
	fi

kernel: pmic clock
	cp config/linux.conf linux/.config
	$(MAKE) -C linux $(LO) clean
	$(MAKE) -C linux $(LO) olddefconfig
	$(MAKE) -C linux $(LO) -j$(shell nproc) zImage
	truncate -s +500K linux/arch/arm/boot/zImage

dtb: dts
	cp config/$(DTS).dts linux/arch/arm/boot/dts/st/
	$(MAKE) -C linux $(LO) st/$(DTS).dtb

save:
	$(MAKE) -C linux $(LO) savedefconfig
	cp linux/defconfig config/linux.conf

br:
	$(MAKE) -C buildroot BR2_DEFCONFIG=../config/buildroot.conf defconfig
	$(MAKE) -C buildroot

sd: dtb
	python3 bootloader/scripts/sdimage.py \
		buildroot/output/images/sdcard.img \
		bootloader/build/main.stm32 \
		--partition linux/arch/arm/boot/zImage \
		--partition linux/arch/arm/boot/dts/st/$(DTS).dtb \
		--partition buildroot/output/images/rootfs.ext2

nand:
	python3 bootloader/scripts/nandimage.py \
		buildroot/output/images/nand.img \
		--boot bootloader/build/main.stm32 \
		--dtb linux/arch/arm/boot/dts/st/$(DTS).dtb \
		--kernel linux/arch/arm/boot/zImage \
		--rootfs buildroot/output/images/rootfs.ubi

copy:
	cp buildroot/output/images/sdcard.img /mnt/c/Users/Jkastelic/Downloads
	cp buildroot/output/images/nand.img /mnt/c/Users/Jkastelic/Downloads
	cp bootloader/build/main.stm32 /mnt/c/Users/Jkastelic/Downloads/m
	@#ssh root@172.25.0.143 "dd of=/dev/mmcblk0p2" < linux/arch/arm/boot/dts/st/custom.dtb
	@#ssh root@172.25.0.61 "dd of=/dev/mmcblk0p1" < linux/arch/arm/boot/zImage

clean:
	rm -rf build
	$(MAKE) -C bootloader clean
	$(MAKE) -C buildroot clean
	$(MAKE) -C linux $(LO) clean
