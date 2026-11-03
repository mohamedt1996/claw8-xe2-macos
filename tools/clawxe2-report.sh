#!/bin/bash
# Run on macOS after booting with ClawXe2.kext. Prints driver state and saves a report.
set -u
out="clawxe2-report-$(date +%Y%m%d-%H%M%S).txt"
{
  echo "== System"; sw_vers; uname -a
  echo "== Kext"; kextstat 2>/dev/null | grep -i clawxe2 || kmutil showloaded 2>/dev/null | grep -i clawxe2 || echo "ClawXe2 not loaded"
  echo "== GPU PCI"; ioreg -l -w0 -r -c IOPCIDevice | grep -iE '"device-id"|"vendor-id"|IOName.*display|<0x64a0|a0640000' | head -20
  echo "== ClawXe2 properties"; ioreg -l -w0 -r -c ClawXe2 | grep -E '"xe2-'
  echo "== Kernel log"; log show --last boot --predicate 'eventMessage CONTAINS "ClawXe2"' --style compact 2>/dev/null | tail -40
} | tee "$out"
echo; echo "Saved $out — attach it to a GitHub issue."
