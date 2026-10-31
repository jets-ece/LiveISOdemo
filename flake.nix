{
  description = "PhoenixBoot / Live Bootable Toolchain Development Environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        buildDeps = with pkgs; [
          gcc
          gnumake
          pkg-config
          glibc.static
          grub2
          grub2_efi
          xorriso
          mtools
          dosfstools
          squashfs-tools
          cpio
          qemu

          bash
          coreutils
          #sbsigntool
          mtools
          openssl
        ];

        minimalInit = pkgs.stdenv.mkDerivation {
          pname = "minimal-init";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.gcc ];
          buildInputs = [ pkgs.glibc.static ];

          buildPhase = ''
            gcc -static -O2 init.c -o init
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp init $out/bin/init
          '';
        };

        phoenixKernel = pkgs.linux_latest.override {
          ignoreConfigErrors = true;
          structuredExtraConfig = with pkgs.lib.kernel; {
            # Console + PS/2 keyboard (QEMU and VirtualBox default)
            VT = yes;
            VT_CONSOLE = yes;
            VGA_CONSOLE = yes;
            FRAMEBUFFER_CONSOLE = yes;
            INPUT = yes;
            INPUT_KEYBOARD = yes;
            INPUT_EVDEV = yes;
            SERIO = yes;
            SERIO_I8042 = yes;
            SERIO_LIBPS2 = yes;
            KEYBOARD_ATKBD = yes;

            # USB keyboards
            USB_SUPPORT = yes;
            USB = yes;
            USB_PCI = yes;
            USB_XHCI_HCD = yes;
            USB_XHCI_PCI = yes;
            USB_EHCI_HCD = yes;
            USB_EHCI_PCI = yes;
            USB_OHCI_HCD = yes;
            USB_OHCI_HCD_PCI = yes;
            USB_UHCI_HCD = yes;
            HID_SUPPORT = yes;
            HID = yes;
            HID_GENERIC = yes;
            USB_HID = yes;

            # Graphics
            DRM = yes;
            DRM_SIMPLEDRM = yes;
            DRM_BOCHS = yes;
            DRM_VBOXVIDEO = yes;
            DRM_FBDEV_EMULATION = yes;
            FB = yes;
            FB_VESA = yes;
            FB_EFI = yes;

            # Needed by init
            DEVTMPFS = yes;
            DEVTMPFS_MOUNT = yes;
            TTY = yes;
            UNIX98_PTYS = yes;
            BLK_DEV_INITRD = yes;
            RD_GZIP = yes;
          };
        };

      in {
        devShells.default = pkgs.mkShell {
          buildInputs = buildDeps;

          shellHook = ''
            echo "================================================="
            echo "  PhoenixBoot Live USB Development Environment   "
            echo "================================================="
            echo "Run 'make iso' to generate a bootable image."
            echo "Run 'make qemu' to test the generated ISO."
            echo "================================================="
          '';
        };

        packages = {
          default = minimalInit;
          kernel = phoenixKernel;
        };
      }
    );
}
