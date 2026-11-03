# claw8-xe2-macos

Native macOS driver for the **Intel Arc 140V (Xe2_LPG, Lunar Lake)** in the **MSI Claw 8 AI+**, bare metal. Early research stage.

> **Status: Milestone 3 of 8.** No acceleration, no Metal, no driver-owned display yet. The kext identifies the GPU, understands its memory (stolen memory, GGTT) and can boot Intel's GuC scheduler firmware. Compiles against MacKernelSDK; never run on hardware.

## What ClawXe2.kext does today (M1)
- Matches `8086:6420 / 64A0 / 64B0` (Linux `INTEL_LNL_IDS`).
- Maps BAR0, reads `GMD_ID` for graphics and media IP → expects **Xe2_LPG 20.04** / Xe2_LPM 20.00.
- With `clawxe2fw=1`: GT + render forcewake handshake, reads DSS fuses → **8 Xe-cores = Arc 140V**.
- Publishes everything as `xe2-*` IORegistry properties. Read-only unless `clawxe2fw=1`.

## Milestone 2 (stolen memory + GGTT)
- Reads stolen-memory layout (`GGC`, `STOLEN_RESERVED`, `DSMBASE`), applies Wa_14019821291.
- Locates the GGTT page table at BAR0 + 8 MiB (4 GiB GGTT, starts after the 8 MiB WOPCM).
- Decodes the firmware's active display plane: `PLANE_SURF` → GGTT PTE → CPU physical address, checks every page is mapped contiguously.
- `clawxe2fb=1`: draws a 256×256 magenta/green checkerboard into that framebuffer. **If it appears top-left on the Claw's screen, GGTT decoding is correct.**
- `clawxe2ggtt=1`: writes a test PTE into a free GGTT slot, reads it back, restores it.

## Milestone 3 (GuC firmware)
- Programs the Xe2 PAT table, partitions WOPCM, resets and boots the **GuC** (`xe/lnl_guc_70.bin`, embedded at build time).
- Reads the GPU's **hwconfig** table from the GuC (published as `xe2-guc-hwconfig`), which also proves the GuC can write system memory through our GGTT.
- Registers and enables the **CT** (command transport) channel used for all later submission work.
- Opt-in: `clawxe2guc=1`. Loading the GuC locks WOPCM until the next reboot.

## Build
On macOS with Xcode CLT:
```sh
git clone https://github.com/acidanthera/MacKernelSDK
tools/fetch-firmware.sh    # GuC firmware + licence (without it M3 reports no-firmware-embedded)
make SDK=MacKernelSDK      # → build/ClawXe2.kext
```
Or download the `ClawXe2` artifact from GitHub Actions.

## Make the installer USB (Windows)
1. Wait for the GitHub Actions **build** to pass; it publishes release **efi-latest** with:
   - `ClawMac-EFI.iso` — UEFI-bootable OpenCore EFI (ClawXe2 included), validated with `ocvalidate`
   - `ClawMac-EFI.zip` — same EFI plus `macserial` / `macrecovery` for the USB maker
2. On any Windows PC: download this repo, double-click **`windows/Make-ClawMacUSB.bat`**. It:
   - downloads the EFI, generates your own SMBIOS serials,
   - downloads macOS Tahoe recovery **from Apple** (needs Python 3; offers to install it),
   - erases the USB stick you pick (types `ERASE` to confirm) and writes EFI + recovery.
3. The ISO has no macOS inside (Apple's licence forbids redistributing it); it's for VMs or writing the EFI alone with Rufus. For the Claw use the USB from step 2.

`-NoUsb` builds the USB contents into `%USERPROFILE%\ClawMac\usb` without touching any disk.

## Test on the Claw
1. Get macOS booting: [`docs/OPENCORE.md`](docs/OPENCORE.md).
2. Add `ClawXe2.kext` to OpenCore after Lilu; boot-args `-v -clawxe2dbg clawxe2fw=1 clawxe2fb=1 clawxe2ggtt=1 clawxe2guc=1`.
3. Pick a test stage with `sudo tools/clawxe2-stage.sh 1|2|3|4`, reboot, then run `sudo tools/clawxe2-report.sh` and open an issue with the output.

On Windows, `tools/Get-ClawGpuIds.ps1` confirms the device ID first.

## Boot-args
| Arg | Effect |
|---|---|
| `-clawxe2off` | Don't attach |
| `-clawxe2dbg` | Verbose log |
| `clawxe2fw=1` | Enable forcewake + topology probe (writes registers) |
| `-clawxe2nom2` | Skip milestone 2 |
| `clawxe2fb=1` | M2: draw test pattern via GGTT-resolved framebuffer |
| `clawxe2ggtt=1` | M2: GGTT PTE write/read-back self-test |
| `clawxe2guc=1` | M3: load GuC, read hwconfig, enable CT |

## Plan
[`ROADMAP.md`](ROADMAP.md): GGTT → GuC/HuC/GSC firmware → PPGTT + submission → compute → Xe2LPD display → Mesa ANV → Metal. Each step is mapped to the Linux `xe` source it ports.

## License
Driver: GPL-2.0-only (derived from Linux `drivers/gpu/drm/xe`). Embedded GuC firmware: Intel, redistributable in binary form under `LICENSE.xe` from linux-firmware.
