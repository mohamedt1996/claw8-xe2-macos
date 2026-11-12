#!/bin/bash
# SPDX-License-Identifier: MIT
# build.sh: the IntelXe Metal driver stack for the MSI Claw 8 AI+ (Lunar Lake, Arc 140V), on macOS (CI: macos-26).
#
#   intelxe-lnl/build.sh            -> intelxe-lnl/out/
#
# 1. upstream intel-gpu-macos-driver at UPSTREAM_COMMIT + patches/*.patch (the Lunar Lake port)
# 2. IntelXe.kext + IntelXeFB.kext   kernel/IntelXe/build.py --platform lnl (xe/lnl GuC 70.72.1 built in)
# 3. NMXAccel.kext + NMXAGDC.kext     nullmoth/build/build-intelxe.sh kexts (NMXAccel now matches 8086:64a0)
# 4. NVMTLDriver (Metal plugin)       the same clang line as build-intelxe.sh's plugin step (iGPU: unified memory,
#                                     INTEL_DEBUG=noccs)
# 5. the release package              the upstream UPSTREAM_RELEASE Tahoe package with 2-4 swapped in, through the
#                                     patched release/package.sh (install.sh accepts the Lunar Lake ids). Mesa ANV,
#                                     the shader translator and the codecs are the release's: their sources are unchanged.
# 6. SSDT-GFXOFF.aml (iasl) and tools/claw-intelxe.sh
#
# Outputs: IntelXe-LNL-<ver>-macOS.zip (what claw-intelxe.sh installs), IntelXe-LNL-<ver>-EFI.zip (IntelXe.kext +
# SSDT-GFXOFF.aml for efi/build_efi.py --variant intelxe), BUILDINFO.txt.
# Needs: Xcode command line tools, git, python3, gh (GH_TOKEN) or curl, iasl (brew install acpica).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
eval "$(grep -E '^[A-Z_]+=' "$HERE/UPSTREAM")"
WORK=${WORK:-$HERE/work}
RESULT=${RESULT:-$HERE/out}
mkdir -p "$WORK"
SRC=$WORK/src
log() { printf '\n== %s\n' "$*"; }

log "upstream $UPSTREAM_URL @ ${UPSTREAM_COMMIT:0:7} + $(ls "$HERE"/patches/*.patch | wc -l | tr -d ' ') patch(es)"
rm -rf "$SRC"
git init -q "$SRC"
git -C "$SRC" fetch -q --depth 1 "$UPSTREAM_URL" "$UPSTREAM_COMMIT"
git -C "$SRC" checkout -q FETCH_HEAD
git -C "$SRC" -c user.name=build -c user.email=build@localhost am -q "$HERE"/patches/*.patch
git -C "$SRC" log --oneline -3

log "MacKernelSDK"
[ -d "$WORK/MacKernelSDK/Headers" ] || git clone -q --depth 1 https://github.com/acidanthera/MacKernelSDK "$WORK/MacKernelSDK"
export MACKERNELSDK=$WORK/MacKernelSDK

log "Lunar Lake GuC/HuC (linux-firmware)"
FW=$WORK/fw; mkdir -p "$FW"
fetch_fw() { # name sha
	local f=$FW/$1
	[ -f "$f" ] && [ "$(shasum -a 256 "$f" | cut -d' ' -f1)" = "$2" ] && { echo "ok      $1"; return 0; }
	for base in https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/xe \
	            "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/xe"; do
		curl -fsSL -o "$f.part" "$base/$1" || continue
		if [ "$(shasum -a 256 "$f.part" | cut -d' ' -f1)" = "$2" ]; then mv "$f.part" "$f"; echo "fetched $1 ($base)"; return 0; fi
		echo "sha256 mismatch for $1 from $base" >&2
	done
	rm -f "$f.part"; echo "could not fetch $1 with sha256 $2" >&2; return 1
}
fetch_fw lnl_guc_70.bin "$LNL_GUC_SHA"
fetch_fw lnl_huc.bin "$LNL_HUC_SHA"

log "IntelXe.kext + IntelXeFB.kext (--platform lnl)"
KOUT=$WORK/kexts; rm -rf "$KOUT"
python3 "$SRC/kernel/IntelXe/build.py" --platform lnl --sdk "$MACKERNELSDK" --firmware "$FW" --output "$KOUT"
for k in IntelXe IntelXeFB; do
	f=$KOUT/$k.kext/Contents/MacOS/$k
	file "$f" | grep -q "kext bundle" || { echo "$f is not a kext bundle"; exit 1; }
	plutil -extract IOKitPersonalities xml1 -o - "$KOUT/$k.kext/Contents/Info.plist" | grep -q 0x64a08086 || {
		echo "$k.kext does not match 8086:64a0"; exit 1; }
done
nm "$KOUT/IntelXe.kext/Contents/MacOS/IntelXe" | grep -q kXpFw_lnl || echo "note: no XeProbe LNL image (probe stage 3 only)"

log "NMXAccel.kext + NMXAGDC.kext"
NOUT=$WORK/nullmoth; rm -rf "$NOUT"; mkdir -p "$NOUT"
OUT=$NOUT "$SRC/nullmoth/build/build-intelxe.sh" kexts

log "Mesa include/ @ ${MESA_COMMIT:0:11} (Vulkan headers for the plugin)"
MI=$WORK/mesa
if [ ! -f "$MI/include/vulkan/vulkan_core.h" ]; then
	rm -rf "$MI"; git init -q "$MI"
	git -C "$MI" remote add origin "$MESA_URL"
	git -C "$MI" config core.sparseCheckout true
	echo "include/" > "$MI/.git/info/sparse-checkout"
	git -C "$MI" fetch -q --depth 1 --filter=blob:none origin "$MESA_COMMIT"
	git -C "$MI" checkout -q FETCH_HEAD
fi

log "NVMTLDriver (Metal plugin)"
SDK=${MACOS_SDK:-$(xcrun --show-sdk-path)}
xcrun --sdk "$SDK" clang -isysroot "$SDK" -arch x86_64 -mmacosx-version-min=15.0 -bundle -fobjc-arc -O1 -DNVMTL_RELEASE=1 \
	-Wno-protocol -Wno-incomplete-implementation -Werror=unguarded-availability-new -I"$MI/include" \
	-o "$NOUT/NVMTLDriver" "$SRC/nullmoth/plugin/NVMTLDevice.m" \
	-framework Foundation -framework Metal -framework IOSurface -framework IOKit -framework CoreFoundation
file "$NOUT/NVMTLDriver"

log "upstream $UPSTREAM_RELEASE Tahoe package"
REL=$WORK/release; mkdir -p "$REL"
Z=intel-gpu-macos-driver-$UPSTREAM_VERSION-tahoe.zip
if [ ! -f "$REL/$Z" ]; then
	if command -v gh >/dev/null && [ -n "${GH_TOKEN:-}" ]; then
		gh release download "$UPSTREAM_RELEASE" -R "${UPSTREAM_URL#https://github.com/}" -D "$REL" -p "$Z" -p "*.sha256" --clobber
	else
		curl -fsSL -o "$REL/$Z" "$UPSTREAM_URL/releases/download/$UPSTREAM_RELEASE/$Z"
		curl -fsSL -o "$REL/intel-gpu-macos-driver-$UPSTREAM_VERSION.sha256" \
			"$UPSTREAM_URL/releases/download/$UPSTREAM_RELEASE/intel-gpu-macos-driver-$UPSTREAM_VERSION.sha256" || true
	fi
fi
S=$(ls "$REL"/*.sha256 2>/dev/null | head -1 || true)
if [ -n "$S" ] && grep -q "$Z" "$S"; then
	want=$(grep "$Z" "$S" | awk '{print $1}'); got=$(shasum -a 256 "$REL/$Z" | cut -d' ' -f1)
	[ "$want" = "$got" ] || { echo "$Z: sha256 $got, the release lists $want"; exit 1; }
	echo "ok      $Z matches the release's .sha256"
else echo "warn    no .sha256 entry for $Z: not verified"; fi
T=$WORK/pkgin; rm -rf "$T"; mkdir -p "$T"
ditto -x -k "$REL/$Z" "$T"
T=$(find "$T" -maxdepth 1 -mindepth 1 -type d | head -1)
[ -f "$T/install.sh" ] && [ -d "$T/Library" ] && [ -d "$T/EFI/OC/Kexts" ] || { echo "unexpected package layout in $Z"; exit 1; }

log "swap in the Lunar Lake binaries"
put_kext() { # src dst
	[ -d "$2" ] || echo "note: $2 was not in the package"
	rm -rf "$2"; ditto "$1" "$2"
}
put_kext "$KOUT/IntelXe.kext" "$T/EFI/OC/Kexts/IntelXe.kext"
put_kext "$KOUT/IntelXeFB.kext" "$T/Library/Extensions/IntelXeFB.kext"
put_kext "$NOUT/NMXAccel.kext" "$T/Library/Extensions/NMXAccel.kext"
put_kext "$NOUT/NMXAGDC.kext" "$T/Library/Extensions/NMXAGDC.kext"
codesign -f -s - "$T/Library/Extensions/IntelXeFB.kext"
n=0
while IFS= read -r b; do   # every copy of the Metal plugin (/Library fallback NVMTLDriverLib.bundle, system-volume copy)
	exe=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$b/Contents/Info.plist" 2>/dev/null || true)
	case "$exe" in NVMTLDriver*) ;; *) continue;; esac
	cp "$NOUT/NVMTLDriver" "$b/Contents/MacOS/$exe"
	codesign -f -s - "$b/Contents/MacOS/$exe"
	codesign -f -s - "$b"
	echo "ok      ${b#$T/}/Contents/MacOS/$exe"; n=$((n + 1))
done < <(find "$T" -type d -name 'NVMTLDriver*.bundle' -prune)
[ $n -gt 0 ] || { echo "no Metal plugin bundle found in the package"; exit 1; }
# system-volume/ keeps its own SHA256SUMS (package.sh checks it)
if [ -d "$T/system-volume" ]; then
	(cd "$T/system-volume" && find . -type f ! -name SHA256SUMS | sed 's#^\./##' | LC_ALL=C sort |
		while IFS= read -r f; do shasum -a 256 "$f"; done > SHA256SUMS)
fi
# HybridPM is for 12th-14th gen hybrid CPUs, not Lunar Lake: kept in the package (package.sh wants it), never enabled

log "package ($CLAW_VERSION)"
PO=$WORK/pkgout; rm -rf "$PO"
bash "$SRC/release/package.sh" "$CLAW_VERSION" "$T" "$T" "$PO"
PZ=$PO/intel-gpu-macos-driver-$CLAW_VERSION-tahoe.zip
[ -f "$PZ" ] || { echo "package.sh made no $PZ"; exit 1; }

log "SSDT-GFXOFF.aml"
A=$WORK/acpi; rm -rf "$A"; mkdir -p "$A"
cp "$HERE/acpi/SSDT-GFXOFF.dsl" "$A/"
(cd "$A" && iasl -ve SSDT-GFXOFF.dsl)

log "outputs"
rm -rf "$RESULT"; mkdir -p "$RESULT"
M=$WORK/IntelXe-LNL-$CLAW_VERSION-macOS; rm -rf "$M"; mkdir -p "$M"
ditto -x -k "$PZ" "$M"
P=$(find "$M" -maxdepth 1 -mindepth 1 -type d | head -1)
mv "$P" "$M/package"
cp "$HERE/../tools/claw-intelxe.sh" "$M/claw-intelxe.sh"; chmod 755 "$M/claw-intelxe.sh"
cp "$A/SSDT-GFXOFF.aml" "$A/SSDT-GFXOFF.dsl" "$M/"
cp "$HERE/README.md" "$M/README.md"
(cd "$WORK" && ditto -c -k --norsrc --noextattr --keepParent "$(basename "$M")" "$RESULT/IntelXe-LNL-$CLAW_VERSION-macOS.zip")
E=$WORK/efi-parts; rm -rf "$E"; mkdir -p "$E/Kexts" "$E/ACPI"
ditto "$KOUT/IntelXe.kext" "$E/Kexts/IntelXe.kext"
cp "$A/SSDT-GFXOFF.aml" "$E/ACPI/"
(cd "$E" && zip -qr "$RESULT/IntelXe-LNL-$CLAW_VERSION-EFI.zip" Kexts ACPI)
{
	echo "IntelXe for the MSI Claw 8 AI+ $CLAW_VERSION"
	echo "upstream $UPSTREAM_URL ${UPSTREAM_COMMIT:0:12} + $(cd "$HERE/patches" && ls *.patch | tr '\n' ' ')"
	echo "userland from $UPSTREAM_RELEASE ($Z)"
	for f in "$KOUT/IntelXe.kext/Contents/MacOS/IntelXe" "$KOUT/IntelXeFB.kext/Contents/MacOS/IntelXeFB" \
	         "$NOUT/NMXAccel.kext/Contents/MacOS/NMXAccel" "$NOUT/NMXAGDC.kext/Contents/MacOS/NMXAGDC" "$NOUT/NVMTLDriver"; do
		printf '%s  %s\n' "$(shasum -a 256 "$f" | cut -c1-16)" "$(basename "$f")"
	done
} > "$RESULT/BUILDINFO.txt"
cat "$RESULT/BUILDINFO.txt"
ls -l "$RESULT"
