#!/bin/sh
# Downloads the Lunar Lake GuC firmware and its license from linux-firmware.
# Usage: tools/fetch-firmware.sh [destdir]   (default: firmware/)
set -eu
dest="${1:-firmware}"
base="https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main"
mkdir -p "$dest"
for f in xe/lnl_guc_70.bin LICENSE.xe; do
    out="$dest/$(basename "$f")"
    echo "fetch $f"
    curl -sSfL --retry 3 -o "$out.tmp" "$base/$f"
    mv "$out.tmp" "$out"
done
ls -l "$dest"
