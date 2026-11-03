# Xe2 macOS driver roadmap (MSI Claw 8 AI+, Arc 140V)

Each milestone lists the Linux `drivers/gpu/drm/xe` sources to port and a pass criterion that must be met **on the Claw** before moving on.

| # | Milestone | Linux xe reference | Pass criterion | Status |
|---|---|---|---|---|
| M0 | macOS boots on the Claw (GOP framebuffer, no accel) | — | Desktop reachable, `ioreg` shows GPU `8086:64a0` | docs only (`docs/OPENCORE.md`) |
| M1 | Probe: map BAR0, read GMD_ID, forcewake, Xe-core fuses | `xe_pci.c` (`read_gmdid`), `xe_force_wake.c`, `xe_gt_topology.c` | `xe2-graphics-ip = 2004`, `xe2-xe-cores-geometry = 8` | code done, untested |
| M2 | Stolen memory + GGTT: DSM layout, GGTT PTE read/write, scanout decode | `xe_ttm_stolen_mgr.c`, `xe_ggtt.c`, `xe_wopcm.c`, `xe_pat.c`, `regs/xe_gtt_defs.h` | `xe2-ggtt-decode = ok`; `clawxe2fb=1` draws a checkerboard top-left; `xe2-ggtt-selftest = pte-readback-ok` | **code done, untested** |
| M3 | Firmware: GuC, HuC, GSC load and auth | `xe_uc_fw.c`, `xe_guc.c`, `xe_guc_ads.c`, `xe_guc_ct.c`, `xe_huc.c`, `xe_gsc.c`; firmware `xe/lnl_guc_70.bin`, `xe/lnl_huc.bin`, `xe/lnl_gsc_104.bin` | GuC reports `READY`, CT channel round-trip | not started |
| M4 | Memory + submission: PPGTT (4-level), BOs, exec queues via GuC | `xe_vm.c`, `xe_pt.c`, `xe_bo.c`, `xe_exec_queue.c`, `xe_guc_submit.c`, `xe_lrc.c`, `xe_hw_engine.c` | `MI_STORE_DWORD` batch on RCS writes expected value | not started |
| M5 | Compute: run a SPIR-V/OpenCL kernel (IGC Xe2 binary) | `xe_exec.c`; Intel IGC (Xe2 target) | Vector-add on 64 Ki elements matches CPU | not started |
| M6 | Display: Xe2LPD eDP panel, DMC, modeset, IOFramebuffer | `drivers/gpu/drm/xe/display` + shared i915 display code; `i915/xe2lpd_dmc.bin` | Native-resolution framebuffer, backlight control | not started |
| M7 | Userspace Vulkan: Mesa ANV (xe backend) over an IOKit DRM shim | Mesa `src/intel/vulkan` (Xe2 supported upstream); shim pattern from ArcMTL `anv_xnu_drm_shim.c` | `vkcube` offscreen renders | not started |
| M8 | Metal: Metal→Vulkan provider (AIR→SPIR-V) + IOAccelerator for WindowServer | WTLIntelGfx / ArcMTL architecture (PolyForm-NC licensed — reimplement or license) | WindowServer composites on Arc, Metal sample runs | not started |

## M2 notes

- Xe2 iGPU has no CPU aperture through GGTT (`xe_ttm_stolen_cpu_access_needs_ggtt()` is false for ≥ 12.70); stolen memory is reached directly through BAR2 + 8 MiB. So the M2 proof is: firmware scanout address → GGTT PTE → CPU address, then drawing pixels at that address.
- GGTT write self-test only proves the CPU→GSM path. GPU-side use of a new mapping needs GGTT TLB invalidation, which on Xe2 goes through the GuC (`PVC_GUC_TLB_INV_DESC0/1`) → M3.
- Workarounds applied: Wa_22019338487 (dummy media `GMD_ID` write every 63 GGTT writes), Wa_14019821291 (GSC PSMI carve-out at top of DSM).

## Shortcut worth evaluating

[WTLIntelGfx](https://github.com/InSnh-Gd/WTLIntelGfx) already runs Linux i915 inside XNU through FreeBSD `drm-kmod` + LinuxKPI, with Mesa ANV and a Metal plugin, on Arrow/Meteor Lake. Xe2 needs the `xe` driver instead of `i915` (its issue #8). Porting `xe` onto that LinuxKPI layer would collapse M2–M8 into one large port instead of a from-scratch IOKit driver. M1 here is still useful as an independent hardware-sanity check.

## Related work

- [LunarBridge-Hackintosh](https://github.com/IT-EXPRESS-Bayern/LunarBridge-Hackintosh) — Arc 130V research (forcewake, GGTT planner, GuC codec; no execution yet).
- [ArcMTL](https://github.com/longurara/ArcMTL) — Meteor/Arrow Lake Metal 3 + ANV stack.
- [JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe) — Metal on Xe-LP by reusing Apple's TGL driver (not applicable to Xe2).

## Constraints

- macOS 26 Tahoe is the last x86 release; macOS 27 is Apple-silicon only.
- Porting `xe` code makes the kernel driver GPL-2.0.
- Each milestone needs a Claw booting macOS, serial/`-v` logging, and a recovery path (second EFI entry with `-clawxe2off`).
