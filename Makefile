# SPDX-License-Identifier: MIT
# Makefile --- TODO: description
# Copyright (c) 2026 Jakob Kastelic
DTS = custom
#DTS = stm32mp135f-dk

LO = ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

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

kernel:
	cp config/linux.conf linux/.config
	$(MAKE) -C linux $(LO) olddefconfig
	rm -f linux/arch/arm/boot/zImage
	$(MAKE) -C linux $(LO) -j$(shell nproc) zImage
	truncate -s +500K linux/arch/arm/boot/zImage

dtb:
	cp config/$(DTS).dts linux/arch/arm/boot/dts/
	$(MAKE) -C linux $(LO) $(DTS).dtb

save:
	$(MAKE) -C linux $(LO) savedefconfig
	cp linux/defconfig config/linux.conf

br:
	$(MAKE) -C buildroot BR2_DEFCONFIG=../config/buildroot.conf defconfig
	$(MAKE) -C buildroot

sd:
	python3 bootloader/scripts/sdimage.py \
		buildroot/output/images/sdcard.img \
		bootloader/build/main.stm32 \
		--partition linux/arch/arm/boot/zImage \
		--partition linux/arch/arm/boot/dts/$(DTS).dtb \
		--partition buildroot/output/images/rootfs.ext2

# Pack the V7-on-Armv7 image into the same MBR shape `two` expects:
# bootloader as the unpartitioned LBA-128 file, then the unix kernel
# and the V7 rootfs as MBR partitions for `two` to copy into DDR.
sd-unix:
	mkdir -p buildroot/output/images
	# sdimage.py places the first three positional files at LBAs
	# 128 / 640 / 896. The bench bootloader's `two` command default
	# expects kernel at SD block 896 and DTB at 640, so we slot the
	# unix kernel into the 896 position with an empty placeholder
	# at 640. The V7 fs is emitted as the trailing MBR partition
	# for bench-side `mbr_load` workflows.
	: > buildroot/output/images/.dtb_placeholder
	python3 bootloader/scripts/sdimage.py \
		buildroot/output/images/unix-sdcard.img \
		bootloader/build/main.stm32 \
		buildroot/output/images/.dtb_placeholder \
		../unix-v7-c99/unix \
		--partition ../unix-v7-c99/root.img

nand:
	python3 bootloader/scripts/nandimage.py \
		buildroot/output/images/nand.img \
		--boot bootloader/build/main.stm32 \
		--dtb linux/arch/arm/boot/dts/$(DTS).dtb \
		--kernel linux/arch/arm/boot/zImage \
		--rootfs buildroot/output/images/rootfs.ubi

copy:
	cp buildroot/output/images/sdcard.img /mnt/c/Users/Jkastelic/Downloads
	cp buildroot/output/images/nand.img /mnt/c/Users/Jkastelic/Downloads
	cp bootloader/build/main.stm32 /mnt/c/Users/Jkastelic/Downloads/m
	@#ssh root@172.25.0.143 "dd of=/dev/mmcblk0p2" < linux/arch/arm/boot/dts/custom.dtb
	@#ssh root@172.25.0.61 "dd of=/dev/mmcblk0p1" < linux/arch/arm/boot/zImage

clean:
	rm -rf build
	$(MAKE) -C bootloader clean
	$(MAKE) -C buildroot clean
	$(MAKE) -C linux $(LO) clean
