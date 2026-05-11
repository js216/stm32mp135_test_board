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

# Pack the V7-on-Armv7 image into the MBR shape `two` expects.  The
# bootloader's `two` handler is sd_load_mbr(): it reads the MBR table
# and copies partition[0] to DEF_LINUX_ADDR (0xC2000000), partition[1]
# to DEF_DTB_ADDR (0xC4000000).  So the unix kernel must be the FIRST
# MBR partition, not a positional LBA-896 file.  It also has to be a
# flat binary -- objcopy strips the ELF wrapper so a `jump 0xC2000000`
# lands on .text rather than the ELF header.  Partition[1] is a 1KB
# placeholder so sd_load_mbr's second sd_read writes harmless filler
# to DDR; partition[2] is the V7 rootfs, kept on-card for the (future)
# v7 SD block driver to mount.
sd-unix:
	mkdir -p buildroot/output/images
	arm-none-eabi-objcopy -O binary \
		../unix-v7-c99/unix \
		buildroot/output/images/unix.bin
	# Pad unix.bin to a fixed 64 KiB so root.img's MBR LBA stays
	# stable across kernel-size changes -- the EVB test plans hard-
	# code the LBA byte-by-byte over UART, and a one-block shift in
	# the kernel partition desyncs every section.  128 blocks of slack
	# above the current ~56-block kernel is plenty for the v7-real-
	# kernel mission's gradual link-in of sys/*.c.
	truncate -s 65536 buildroot/output/images/unix.bin
	dd if=/dev/zero of=buildroot/output/images/.dtb_placeholder \
		bs=512 count=2 status=none
	python3 bootloader/scripts/sdimage.py \
		buildroot/output/images/unix-sdcard.img \
		bootloader/build/main.stm32 \
		--partition buildroot/output/images/unix.bin \
		--partition buildroot/output/images/.dtb_placeholder \
		--partition ../unix-v7-c99/root.img

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
