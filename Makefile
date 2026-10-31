SHELL := /bin/bash
.SHELLFLAGS := -eo pipefail -c

KERNEL_PATH := kernel/bzImage
ISO         := phoenixboot.iso

all: iso

# ---------- init ----------
init: init.c
	gcc -static -o init init.c src/* 
       	
initrd.img: init
	rm -rf build_initrd
	mkdir -p build_initrd
	cp init build_initrd/init
	cd build_initrd && find . -print0 | cpio --null -o --format=newc | gzip -9 > ../initrd.img
	rm -rf build_initrd

# ---------- kernel (minimal, built once, no nixpkgs kernel) ----------
$(KERNEL_PATH):
	env -u NIX_LDFLAGS -u NIX_CFLAGS_COMPILE -u NIX_CFLAGS_LINK \
	nix-shell --pure -p gcc13 gnumake flex bison bc elfutils openssl perl ncurses pkg-config \
		curl cacert gnutar xz gzip coreutils gnused gnugrep gawk findutils diffutils bash \
		--run "SSL_CERT_FILE=\$$NIX_SSL_CERT_FILE bash build-kernel.sh"

kernel: $(KERNEL_PATH)

# ---------- ISO ----------
iso: $(KERNEL_PATH) initrd.img
	rm -rf iso
	mkdir -p iso/boot/grub
	cp $(KERNEL_PATH) iso/boot/vmlinuz
	cp initrd.img iso/boot/initrd.img
	printf '%s\n' \
		'set default=0' \
		'set timeout=3' \
		'menuentry "PhoenixBoot Live (Minimal C Init)" {' \
		'    linux /boot/vmlinuz console=tty0' \
		'    initrd /boot/initrd.img' \
		'}' > iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso

qemu: iso
	qemu-system-x86_64 -cdrom $(ISO) -m 512M

# Removes everything except the compiled kernel
clean:
	rm -rf init initrd.img iso $(ISO) build_initrd

# Also removes the kernel (forces a rebuild next time)
distclean: clean
	rm -rf kernel

.PHONY: all iso qemu kernel clean distclean
