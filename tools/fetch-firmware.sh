#!/bin/sh
# Downloads the Lunar Lake GuC firmware and its license from linux-firmware.
# Usage: tools/fetch-firmware.sh [destdir]   (default: firmware/)
set -eu
dest="${1:-firmware}"
base="https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main"
mkdir -p "$dest"
# linux-firmware moved the licence texts to LICENSES/ (LICENSE.xe at the top level now 404s)
for f in xe/lnl_guc_70.bin "LICENSES/LICENSE.xe LICENSE.xe"; do
    got=no
    for p in $f; do
        out="$dest/$(basename "$p")"
        echo "fetch $p"
        if curl -sSfL --retry 3 -o "$out.tmp" "$base/$p"; then mv "$out.tmp" "$out"; got=yes; break; fi
        rm -f "$out.tmp"
    done
    [ $got = yes ] || { echo "could not fetch $f"; exit 1; }
done
ls -l "$dest"
