#!/bin/sh
# Pack an EFI folder into a UEFI-bootable ISO (El Torito FAT image + hybrid GPT for USB writers).
# usage: efi/make_iso.sh <dir containing EFI/> <out.iso>
set -eu
src="$1"; out="$2"
[ -d "$src/EFI/BOOT" ] || { echo "error: $src/EFI/BOOT missing" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/iso"
cp -R "$src/EFI" "$work/iso/"
[ -f "$src/README.txt" ] && cp "$src/README.txt" "$work/iso/"

# FAT image sized to content + 8 MiB headroom, minimum 64 MiB (FAT32 needs >= 33 MiB)
kib=$(du -sk "$work/iso/EFI" | cut -f1)
mib=$(( kib / 1024 + 8 ))
[ "$mib" -lt 64 ] && mib=64
dd if=/dev/zero of="$work/iso/efiboot.img" bs=1M count="$mib" status=none
mkfs.vfat -F 32 -n CLAWMAC "$work/iso/efiboot.img" >/dev/null
mcopy -s -i "$work/iso/efiboot.img" "$work/iso/EFI" ::/

xorriso -as mkisofs -iso-level 3 -R -J -V CLAWMAC_EFI \
    -e efiboot.img -no-emul-boot -isohybrid-gpt-basdat \
    -o "$out" "$work/iso" 2>/dev/null
echo "$out: $(du -h "$out" | cut -f1)"
