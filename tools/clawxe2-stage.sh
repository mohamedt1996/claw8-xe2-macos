#!/bin/bash
# Select which ClawXe2 milestone tests run on the next boot (macOS, needs sudo).
# The ClawMac config does not delete boot-args, so this persists until NVRAM reset.
set -eu
base="-v keepsyms=1 debug=0x100 -lilubetaall"
case "${1:-}" in
  1)   args="$base -clawxe2dbg" ;;
  2)   args="$base -clawxe2dbg clawxe2fw=1" ;;
  3)   args="$base -clawxe2dbg clawxe2fw=1 clawxe2fb=1 clawxe2ggtt=1" ;;
  4)   args="$base -clawxe2dbg clawxe2fw=1 clawxe2fb=1 clawxe2ggtt=1 clawxe2guc=1" ;;
  off) args="$base -clawxe2off" ;;
  *)   echo "usage: sudo $0 1|2|3|4|off"; exit 2 ;;
esac
[ "$(id -u)" -eq 0 ] || { echo "run with sudo"; exit 1; }
nvram boot-args="$args"
echo "boot-args set to: $(nvram boot-args | cut -f2)"
echo "Reboot, then run: sudo tools/clawxe2-report.sh"
