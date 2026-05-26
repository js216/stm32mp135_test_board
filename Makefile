# SPDX-License-Identifier: MIT
# Makefile --- TODO: description
# Copyright (c) 2026 Jakob Kastelic
DTS = custom
#DTS = stm32mp135f-dk

LO = ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

.PHONY: all boot patch keys kernel dtb save br sd nand copy clean

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

# Generate a fresh ed25519 dropbear host key into the overlay if one
# isn't there already. The file is .gitignored so a rotation never
# leaks into history. The mission's SSH-smoke section derives the
# matching trusted pubkey from the buildroot target tree at Build time.
keys:
	@if [ ! -s config/overlay/etc/dropbear/dropbear_ed25519_host_key.bin ]; then \
		python3 -c "from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey; from cryptography.hazmat.primitives import serialization as s; import struct; k = Ed25519PrivateKey.generate(); seed = k.private_bytes(s.Encoding.Raw, s.PrivateFormat.Raw, s.NoEncryption()); pub = k.public_key().public_bytes(s.Encoding.Raw, s.PublicFormat.Raw); open('config/overlay/etc/dropbear/dropbear_ed25519_host_key.bin','wb').write(struct.pack('>I',11)+b'ssh-ed25519'+struct.pack('>I',64)+seed+pub)"; \
		echo "generated config/overlay/etc/dropbear/dropbear_ed25519_host_key.bin"; \
	else \
		echo "dropbear host key already present"; \
	fi

kernel:
	cp config/linux.conf linux/.config
	$(MAKE) -C linux $(LO) olddefconfig
	$(MAKE) -C linux $(LO) -j$(shell nproc) zImage

dtb:
	cp config/$(DTS).dts linux/arch/arm/boot/dts/
	gcc -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
		-I linux/scripts/dtc/include-prefixes \
		-o linux/arch/arm/boot/dts/.$(DTS).dtb.dts.tmp \
		linux/arch/arm/boot/dts/$(DTS).dts
	linux/scripts/dtc/dtc -o linux/arch/arm/boot/dts/$(DTS).dtb \
		-b 0 -i linux/arch/arm/boot/dts/ \
		-i linux/scripts/dtc/include-prefixes \
		-Wno-interrupt_provider -Wno-unique_unit_address \
		-Wno-unit_address_vs_reg -Wno-avoid_unnecessary_addr_size \
		-Wno-alias_paths -Wno-graph_child_address \
		-Wno-simple_bus_reg -@ \
		linux/arch/arm/boot/dts/.$(DTS).dtb.dts.tmp

save:
	$(MAKE) -C linux $(LO) savedefconfig
	cp linux/defconfig config/linux.conf

br: keys
	$(MAKE) -C buildroot BR2_DEFCONFIG=../config/buildroot.conf defconfig
	$(MAKE) -C buildroot

sd: br dtb
	python3 bootloader/scripts/sdimage.py \
		buildroot/output/images/sdcard.img \
		bootloader/build/main.stm32 \
		--partition linux/arch/arm/boot/zImage \
		--partition linux/arch/arm/boot/dts/$(DTS).dtb \
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
