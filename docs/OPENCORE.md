# M0 — Booting macOS on the MSI Claw 8 AI+ (untested)

Target: macOS 26 Tahoe (last Intel release), OpenCore ≥ 1.0.5. Nobody has publicly booted macOS on Lunar Lake; treat this as a checklist, not a known-good EFI.

## BIOS (hold Vol+ at power on)
- Disable Secure Boot, enable VT-x/VT-d (or set `DisableIoMapper`).
- Set DVMT pre-allocated to the maximum offered.
- Disable CFG Lock if exposed; otherwise enable `AppleXcpmCfgLock`.

## CPU (Core Ultra 7 258V: 4 P-cores + 4 LP E-cores, no Hyper-Threading)
- `Kernel → Emulate`: spoof Comet Lake
  - `Cpuid1Data` `55060A00 00000000 00000000 00000000`
  - `Cpuid1Mask` `FFFFFFFF 00000000 00000000 00000000`
- `Kernel → Quirks → ProvideCurrentCpuInfo = true`
- Kexts: `Lilu`, `VirtualSMC`, `CpuTopologyRebuild` (hybrid core ordering), `RestrictEvents`.

## Graphics (no acceleration until M6/M8)
- Do **not** inject an iGPU `AAPL,ig-platform-id`; macOS will use the GOP framebuffer.
- Boot-args for bring-up: `-v keepsyms=1 debug=0x100 -clawxe2dbg clawxe2fw=1 clawxe2fb=1 clawxe2ggtt=1`
- Recovery entry: same EFI with `-clawxe2off`.

## SMBIOS
- `MacBookPro16,2` (Intel, Tahoe-supported).

## Peripherals — expected status
| Device | Status |
|---|---|
| NVMe SSD | should work (`NVMeFix`) |
| USB-C / USB4 | needs USB map (`USBToolBox`) |
| Wi-Fi 7 (Intel BE201) | likely unsupported by `itlwm`; use USB Ethernet |
| Bluetooth | `IntelBluetoothFirmware` unverified for BE201 |
| Touchscreen | `VoodooI2C` (I2C-HID), unverified |
| Gamepad / buttons | unsupported (MSI HID) |
| Battery | `ECEnabler` + `SMCBatteryManager`, needs ACPI patching |
| Audio | `AppleALC` layout unknown for this codec |
| Sleep | disable until stable |

## M1 test
1. Copy `ClawXe2.kext` (CI artifact) to `EFI/OC/Kexts`, add to `config.plist → Kernel → Add` after Lilu.
2. Boot, then run `tools/clawxe2-report.sh`.
3. Expected: `xe2-status = xe2-lpg-detected`, `xe2-graphics-ip = 2004`, `xe2-xe-cores-geometry = 8`.

## M2 test
1. Boot with the args above.
2. Look at the top-left of the screen during/after boot: a 256×256 magenta/green checkerboard with a white border = pass.
3. `tools/clawxe2-report.sh` should show `xe2-stolen = ok`, `xe2-ggtt-decode = ok`, `xe2-ggtt-selftest = pte-readback-ok`.
