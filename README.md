# claw8-xe2-macos

Native macOS driver for the **Intel Arc 140V (Xe2_LPG, Lunar Lake)** in the **MSI Claw 8 AI+**, bare metal. Early research stage.

> **Status: Milestone 1 of 8.** No acceleration, no Metal, no display yet. The kext only identifies and safely pokes the GPU. Compiles against MacKernelSDK; never run on hardware.

## What ClawXe2.kext does today (M1)
- Matches `8086:6420 / 64A0 / 64B0` (Linux `INTEL_LNL_IDS`).
- Maps BAR0, reads `GMD_ID` for graphics and media IP → expects **Xe2_LPG 20.04** / Xe2_LPM 20.00.
- With `clawxe2fw=1`: GT + render forcewake handshake, reads DSS fuses → **8 Xe-cores = Arc 140V**.
- Publishes everything as `xe2-*` IORegistry properties. Read-only unless `clawxe2fw=1`.

## Build
On macOS with Xcode CLT:
```sh
git clone https://github.com/acidanthera/MacKernelSDK
make SDK=MacKernelSDK      # → build/ClawXe2.kext
```
Or download the `ClawXe2` artifact from GitHub Actions.

## Test on the Claw
1. Get macOS booting: [`docs/OPENCORE.md`](docs/OPENCORE.md).
2. Add `ClawXe2.kext` to OpenCore after Lilu; boot-args `-v -clawxe2dbg clawxe2fw=1`.
3. Run `tools/clawxe2-report.sh`, open an issue with the output.

On Windows, `tools/Get-ClawGpuIds.ps1` confirms the device ID first.

## Boot-args
| Arg | Effect |
|---|---|
| `-clawxe2off` | Don't attach |
| `-clawxe2dbg` | Verbose log |
| `clawxe2fw=1` | Enable forcewake + topology probe (writes registers) |

## Plan
[`ROADMAP.md`](ROADMAP.md): GGTT → GuC/HuC/GSC firmware → PPGTT + submission → compute → Xe2LPD display → Mesa ANV → Metal. Each step is mapped to the Linux `xe` source it ports.

## License
GPL-2.0-only (derived from Linux `drivers/gpu/drm/xe`).
