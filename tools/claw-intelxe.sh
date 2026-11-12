#!/bin/bash
# SPDX-License-Identifier: MIT
# claw-intelxe.sh: install the IntelXe Metal driver (Lunar Lake port) on the MSI Claw 8 AI+ and switch its OpenCore EFI.
# Run on macOS on the Claw, from the folder of IntelXe-LNL-*-macOS.zip (the USB stick's IntelXe-LNL folder works).
#
#   sudo ./claw-intelxe.sh status          checks; which EFI mode is set
#   sudo ./claw-intelxe.sh install         the /Library part (package/install.sh --apply), then the EFI to "accel"
#   sudo ./claw-intelxe.sh mode accel      EFI only: IntelXe.kext, SSDT-GFXOFF and the GFX0 _DSM rename on, AMFI boot-arg
#   sudo ./claw-intelxe.sh mode safe       EFI only: all of that off (GOP display, no acceleration)
#   sudo ./claw-intelxe.sh undo            package/install.sh --undo, then the EFI to "safe"
#   --efi <diskXsY | /Volumes/...>         the EFI to edit (default: the one OpenCore booted from)
#
# "safe" is the mode the installer USB starts in. Black screen after switching to accel: boot Windows and run
# Make-ClawMacUSB.bat -SetMode safe (it edits the USB stick's config.plist), then boot macOS again.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PKG=$HERE/package
APPLE=7C436110-AB2A-4BBB-A880-FE41995C9F82
AMFI=amfi_get_out_of_my_way=0x1
PB=/usr/libexec/PlistBuddy
EFIARG=""
CMD=${1:-status}; shift || true
MODE=""
if [ "$CMD" = mode ]; then MODE=${1:-}; shift || true; fi
while [ $# -gt 0 ]; do case $1 in --efi) EFIARG=${2:-}; shift 2;; *) echo "unknown argument $1"; exit 2;; esac; done

ok()   { printf '  ok    %s\n' "$*"; }
warn() { printf '  warn  %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; }
die()  { printf '  FAIL  %s\n' "$*"; exit 1; }

[ "$(uname -s)" = Darwin ] || die "run this on macOS on the Claw"
need_root() { [ "$(id -u)" = 0 ] || die "run with sudo"; }

gpu_check() {
	local pci; pci=$(ioreg -r -c IOPCIDevice -l 2>/dev/null || true)
	if [[ "$pci" == *'"device-id" = <a0640000>'* ]]; then ok "Arc 140V (8086:64a0) on the PCI bus"
	elif [[ "$pci" == *'"device-id" = <20640000>'* || "$pci" == *'"device-id" = <b0640000>'* ]]; then ok "a Lunar Lake GPU on the PCI bus"
	else bad "no Lunar Lake GPU (8086:64a0/6420/64b0) found"; return 1; fi
}

# ---- the EFI partition OpenCore booted from (boot-path: ...HD(n,GPT,<partition UUID>,...)...)
efi_dev() {
	if [ -n "$EFIARG" ]; then
		case $EFIARG in /Volumes/*) diskutil info "$EFIARG" 2>/dev/null | awk -F': *' '/Device Identifier/{print $2; exit}';;
		*) echo "${EFIARG#/dev/}";; esac
		return
	fi
	local bp uuid d
	bp=$(nvram 4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:boot-path 2>/dev/null | cut -f2-)
	uuid=$(printf '%s' "$bp" | sed -n 's/.*GPT,\([0-9A-Fa-f-]\{36\}\).*/\1/p' | tr a-f A-F)
	[ -n "$uuid" ] || return 1
	for d in $(diskutil list 2>/dev/null | awk '/EFI/{print $NF}'); do
		[ "$(diskutil info "$d" 2>/dev/null | awk -F': *' '/Disk \/ Partition UUID/{print toupper($2); exit}')" = "$uuid" ] && { echo "$d"; return; }
	done
	return 1
}
MNT=""; MOUNTED_HERE=no
efi_mount() {
	local d; d=$(efi_dev) || die "cannot tell which EFI OpenCore booted from: pass --efi diskXsY (diskutil list)"
	MNT=$(diskutil info "$d" 2>/dev/null | awk -F': *' '/Mount Point/{print $2; exit}')
	if [ -z "$MNT" ]; then diskutil mount "$d" >/dev/null || die "diskutil mount $d failed"; MOUNTED_HERE=yes
		MNT=$(diskutil info "$d" | awk -F': *' '/Mount Point/{print $2; exit}'); fi
	[ -f "$MNT/EFI/OC/config.plist" ] || die "$d ($MNT) has no EFI/OC/config.plist"
	ok "EFI $d at $MNT"
}
efi_unmount() { [ "$MOUNTED_HERE" = yes ] && diskutil unmount "$MNT" >/dev/null 2>&1; }

# ---- config.plist entries this port owns
CFG() { printf '%s' "$MNT/EFI/OC/config.plist"; }
find_idx() { # array key value -> index of the first dict in array whose key equals value
	local i=0 v
	while v=$($PB -c "Print :$1:$i:$2" "$(CFG)" 2>/dev/null); do
		[ "$v" = "$3" ] && { echo $i; return 0; }
		i=$((i + 1))
	done
	return 1
}
mode_of() {
	local i k; i=$(find_idx Kernel:Add BundlePath IntelXe.kext) || { echo "none (no IntelXe.kext entry: not an IntelXe EFI)"; return; }
	k=$($PB -c "Print :Kernel:Add:$i:Enabled" "$(CFG)")
	[ "$k" = true ] && echo accel || echo safe
}
set_mode() { # accel|safe
	local on=false i ba; [ "$1" = accel ] && on=true
	cp "$(CFG)" "$(CFG).bak-$(date +%Y%m%d-%H%M%S)" && ok "backup $(CFG).bak-*"
	i=$(find_idx Kernel:Add BundlePath IntelXe.kext) || die "no Kernel > Add entry for IntelXe.kext (rebuild the USB with the IntelXe variant)"
	$PB -c "Set :Kernel:Add:$i:Enabled $on" "$(CFG)"
	[ -d "$MNT/EFI/OC/Kexts/IntelXe.kext" ] || die "EFI/OC/Kexts/IntelXe.kext is missing"
	i=$(find_idx ACPI:Add Path SSDT-GFXOFF.aml) || die "no ACPI > Add entry for SSDT-GFXOFF.aml"
	$PB -c "Set :ACPI:Add:$i:Enabled $on" "$(CFG)"
	[ -f "$MNT/EFI/OC/ACPI/SSDT-GFXOFF.aml" ] || die "EFI/OC/ACPI/SSDT-GFXOFF.aml is missing"
	if i=$(find_idx ACPI:Patch Comment "[intelxe] GFX0 _DSM to XDSM"); then $PB -c "Set :ACPI:Patch:$i:Enabled $on" "$(CFG)"
	else warn "no GFX0 _DSM rename patch in ACPI > Patch"; fi
	ba=$($PB -c "Print :NVRAM:Add:$APPLE:boot-args" "$(CFG)")
	ba=$(printf '%s' "$ba" | tr ' ' '\n' | grep -v "^amfi_get_out_of_my_way=" | tr '\n' ' ' | sed 's/ *$//')
	[ $on = true ] && ba="$ba $AMFI"
	$PB -c "Set :NVRAM:Add:$APPLE:boot-args $ba" "$(CFG)"
	nvram boot-args="$ba" 2>/dev/null || true   # now as well (OpenCore writes it again at the next boot)
	ok "EFI mode $1 (boot-args: $ba)"
	plutil -lint "$(CFG)" >/dev/null || die "config.plist does not lint after the edit: restore the backup"
}

status() {
	echo "== status"
	sw_vers 2>/dev/null | sed 's/^/        /'
	gpu_check
	case "$(csrutil status 2>/dev/null)" in *disabled*|*Custom*) ok "SIP relaxed";; *) warn "SIP enabled (the EFI sets csr-active-config 0x803)";; esac
	case "$(nvram boot-args 2>/dev/null)" in *amfi_get_out_of_my_way=*) ok "AMFI boot-arg set";; *) warn "no AMFI boot-arg yet (mode accel adds it)";; esac
	kmutil showloaded 2>/dev/null | grep -q io.github.yusufklncc.IntelXe && ok "IntelXe.kext loaded" || warn "IntelXe.kext not loaded"
	local p; p=$(sysctl -n debug.intelxe.integrated 2>/dev/null) && [ "$p" = 1 ] && ok "IntelXe runs the integrated path"
	[ -d /Library/Extensions/IntelXeFB.kext ] && ok "IntelXeFB.kext installed" || warn "IntelXeFB.kext not installed"
	ioreg -r -c IntelXe -d 1 2>/dev/null | grep -o '"IntelXePlatform" = "[^"]*"' | sed 's/^/        /'
	efi_mount; echo "        EFI mode: $(mode_of)"; efi_unmount
}

case $CMD in
status) status;;
install)
	need_root
	[ -x "$PKG/install.sh" ] || die "package/install.sh not found next to this script"
	gpu_check || exit 1
	efi_mount
	[ "$(mode_of)" != none ] || die "this EFI has no IntelXe entries: rebuild the USB with Make-ClawMacUSB.bat (IntelXe variant)"
	ba=$(nvram boot-args 2>/dev/null | cut -f2-)
	case "$ba" in *amfi_get_out_of_my_way=*) ;; *) nvram boot-args="$ba $AMFI" && ok "AMFI boot-arg set for this session";; esac
	echo "== package/install.sh --apply"
	(cd "$PKG" && ./install.sh --apply) || { efi_unmount; die "install.sh --apply failed (nothing changed in the EFI)"; }
	echo "== EFI"
	set_mode accel
	efi_unmount
	echo; echo "Done. Reboot. If the screen stays black, boot Windows and run Make-ClawMacUSB.bat -SetMode safe."
	;;
mode)
	need_root
	case $MODE in accel|safe) ;; *) die "mode accel|safe";; esac
	efi_mount; set_mode "$MODE"; efi_unmount
	[ "$MODE" = accel ] && [ ! -d /Library/Extensions/IntelXeFB.kext ] && warn "IntelXeFB.kext is not installed: run install first, or the display goes black"
	;;
undo)
	need_root
	[ -x "$PKG/install.sh" ] && (cd "$PKG" && ./install.sh --undo)
	efi_mount; set_mode safe; efi_unmount
	;;
*) sed -n '3,15p' "$0"; exit 2;;
esac
