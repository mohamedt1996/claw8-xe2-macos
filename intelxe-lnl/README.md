# IntelXe on the MSI Claw 8 AI+ (Lunar Lake, Arc 140V)

A port of [yusufklncc/intel-gpu-macos-driver](https://github.com/yusufklncc/intel-gpu-macos-driver) (Metal on Vulkan
on a Linux-`xe`-style kernel driver, written for the Arc B580) to the Claw's integrated Arc 140V. **Untested on
hardware.** Expect to iterate; keep Windows on the internal drive and macOS on an external SSD.

## What the port changes (`patches/`)

| Area | B580 (upstream) | Claw (this port) |
|---|---|---|
| Device memory | 12 GB VRAM through BAR2 | stolen DSM (BIOS "DVMT"/GMS) at BAR2 + 8 MB, DM flag, as Linux `xe` |
| User BOs | VRAM, spill to system | system memory only (no VRAM region in the uAPI) |
| Page tables | VRAM | 64 KB system-memory slabs, walked with PAT WB |
| Compression | flat CCS in VRAM | off: compressed PATs map to uncompressed twins, Mesa gets `INTEL_DEBUG=noccs` |
| Workarounds | graphics 20.01 | graphics 20.04 (`xe_wa.c`, `xe_tuning.c`, GuC WA KLVs), 32-entry PAT |
| Firmware | `xe/bmg_guc_70.bin` | `xe/lnl_guc_70.bin` 70.72.1 |
| Display | DP on the card | the GOP-lit eDP panel, adopted; no rate changes, no VRR |
| Video engines | on | off by default (`-intelxe_video` to try) |

Mesa ANV, the shader translator and the VideoToolbox codecs are upstream v0.1.4's binaries (sources unchanged; ANV
supports Lunar Lake). Kexts, the Metal plugin and the package are rebuilt by `build.sh` in CI.

## Install

1. Windows: `windows\Make-ClawMacUSB.bat` → USB stick (OpenCore in *safe* mode, macOS recovery, `IntelXe-LNL-macOS.zip`).
2. BIOS: Secure Boot off, VT-d on, the largest DVMT/graphics memory offered.
3. Boot the stick, install macOS Tahoe to an **external** SSD.
4. In macOS: `ditto -x -k /Volumes/CLAWMAC/IntelXe-LNL-macOS.zip ~/IntelXe && sudo ~/IntelXe/IntelXe-LNL-*/claw-intelxe.sh install`, reboot.
5. Black screen: on Windows `Make-ClawMacUSB.bat -SetMode safe`, boot again, send the logs below.

## Logs to send back

`sudo ~/IntelXe/IntelXe-LNL-*/claw-intelxe.sh status`, `sysctl debug.intelxe.state`,
`log show --last boot --predicate 'eventMessage CONTAINS "IntelXe"' > ~/intelxe.log`, and
`sh ~/IntelXe/IntelXe-LNL-*/package/tools/report/collect-macos.sh` if present.

Licences: upstream's (MIT; `nullmoth/` and `IntelXeFB.cpp` PolyForm Noncommercial; the translator LGPL). Noncommercial use.
