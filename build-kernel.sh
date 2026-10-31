#!/usr/bin/env bash
set -euo pipefail

VER=6.12.30
mkdir -p kernel && cd kernel

if [ ! -d linux-$VER ]; then
  curl -L -O https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$VER.tar.xz
  tar xf linux-$VER.tar.xz
  rm linux-$VER.tar.xz
fi

cd linux-$VER
make tinyconfig

# Core
./scripts/config -e 64BIT -e PRINTK -e BLK_DEV_INITRD -e RD_GZIP \
  -e BINFMT_ELF -e BINFMT_SCRIPT -e PROC_FS -e SYSFS \
  -e DEVTMPFS -e DEVTMPFS_MOUNT -e TTY -e UNIX98_PTYS

# Console on the VGA screen
./scripts/config -e VT -e VT_CONSOLE -e VGA_CONSOLE -e CONSOLE_TRANSLATIONS

# PS/2 keyboard (QEMU and VirtualBox default)
./scripts/config -e INPUT -e INPUT_KEYBOARD -e INPUT_EVDEV \
  -e SERIO -e SERIO_I8042 -e SERIO_LIBPS2 -e KEYBOARD_ATKBD

# USB keyboard (optional, small)
./scripts/config -e PCI -e USB_SUPPORT -e USB -e USB_PCI \
  -e USB_XHCI_HCD -e USB_XHCI_PCI -e USB_EHCI_HCD -e USB_EHCI_PCI \
  -e USB_OHCI_HCD -e USB_OHCI_HCD_PCI -e USB_UHCI_HCD \
  -e HID -e HID_GENERIC -e USB_HID

# Serial console (lets you see output with qemu -nographic)
./scripts/config -e SERIAL_8250 -e SERIAL_8250_CONSOLE

make olddefconfig
make -j"$(nproc)" bzImage KCFLAGS="-std=gnu11" HOSTCFLAGS="-std=gnu11"
cp arch/x86/boot/bzImage ../bzImage
echo
