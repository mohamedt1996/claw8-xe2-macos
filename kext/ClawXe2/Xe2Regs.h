// Xe2 (Lunar Lake) register definitions.
// Offsets verified against Linux drivers/gpu/drm/xe/regs/xe_gt_regs.h and
// include/drm/intel/pciids.h (torvalds/linux master, Nov 2026).
#pragma once
#include <stdint.h>

namespace xe2 {

// PCI
constexpr uint16_t kVendorIntel = 0x8086;
constexpr uint16_t kLnlIds[] = { 0x6420, 0x64A0, 0x64B0 }; // INTEL_LNL_IDS

// BAR0 (GTTMMADR): registers in the low 4 MiB, GGTT above.
constexpr uint32_t kMinBar0Len = 0x400000;

// IP version
constexpr uint32_t GMD_ID              = 0x0d8c;
constexpr uint32_t MEDIA_GT_GSI_OFFSET = 0x380000;
constexpr uint32_t GMD_ARCH(uint32_t v)    { return (v >> 22) & 0x3ff; }
constexpr uint32_t GMD_RELEASE(uint32_t v) { return (v >> 14) & 0xff;  }
constexpr uint32_t GMD_REVID(uint32_t v)   { return v & 0x3f; }
constexpr uint32_t kGraphicsXe2Lpg = 2004;  // "Xe2_LPG"
constexpr uint32_t kMediaXe2Lpm    = 2000;  // "Xe2_LPM"

// Forcewake (graphics IP >= 12.70 uses the MTL GT ack register)
constexpr uint32_t FORCEWAKE_GT          = 0xa188;
constexpr uint32_t FORCEWAKE_ACK_GT_MTL  = 0x0dfc;
constexpr uint32_t FORCEWAKE_RENDER      = 0xa278;
constexpr uint32_t FORCEWAKE_ACK_RENDER  = 0x0d84;
constexpr uint32_t FORCEWAKE_KERNEL_BIT  = 1u << 0;            // FORCEWAKE_MT(FORCEWAKE_KERNEL)
constexpr uint32_t FORCEWAKE_KERNEL_MASK = 1u << 16;           // FORCEWAKE_MT_MASK(FORCEWAKE_KERNEL)
constexpr uint32_t kForcewakeTimeoutMs   = 50;                 // XE_FORCE_WAKE_ACK_TIMEOUT_MS

// Topology fuses: each set bit = one DSS = one Xe-core on Xe2
constexpr uint32_t kGeometryDss[] = { 0x913c /*XELP_GT_GEOMETRY_DSS_ENABLE*/,
                                      0x9150 /*XE2_GT_GEOMETRY_DSS_1*/,
                                      0x9154 /*XE2_GT_GEOMETRY_DSS_2*/ };
constexpr uint32_t kComputeDss[]  = { 0x9144 /*XEHP_GT_COMPUTE_DSS_ENABLE*/,
                                      0x9148 /*XEHPC_GT_COMPUTE_DSS_ENABLE_EXT*/,
                                      0x914c /*XE2_GT_COMPUTE_DSS_2*/ };

// ---------------- M2: stolen memory + GGTT ----------------
// Sources: regs/xe_regs.h, regs/xe_gtt_defs.h, xe_ggtt.c, xe_ttm_stolen_mgr.c, xe_wopcm.c, xe_pat.c
constexpr uint32_t GGC             = 0x108040;   // GMS [15:8], GGMS [7:6] (must be 3 = 8 MiB GSM)
constexpr uint32_t DSMBASE         = 0x1080c0;   // 64-bit, BDSM [63:20]
constexpr uint32_t GSMBASE         = 0x108100;   // 64-bit
constexpr uint32_t STOLEN_RESERVED = 0x1082c0;   // 64-bit, WOPCM_SIZE [9:7]
constexpr uint32_t GSCPSMI_BASE    = 0x880c;     // media GT (+MEDIA_GT_GSI_OFFSET), Wa_14019821291
constexpr uint64_t BDSM_MASK       = 0xfffffffffff00000ull;

constexpr uint32_t kGsmOffset   = 0x800000;      // GGTT PTEs live at BAR0 + 8 MiB
constexpr uint32_t kGsmSize     = 0x800000;      // 8 MiB -> 1 Mi PTEs -> 4 GiB GGTT
constexpr uint32_t kMinBar0LenGsm = kGsmOffset + kGsmSize;
constexpr uint64_t kWopcmSizeLnl = 0x800000;     // LNL_WOPCM_SIZE (GGTT starts here)
constexpr uint64_t GUC_GGTT_TOP  = 0xFEE00000ull;
constexpr uint64_t kPageSize     = 4096;
constexpr uint32_t kStolenBaseInBar2 = 0x800000; // DSM = GSM + 8 MiB inside BAR2 (graphics >= 12.70)

// GGTT PTE
constexpr uint64_t PTE_PRESENT   = 1ull << 0;
constexpr uint64_t PTE_DM        = 1ull << 1;    // XE_GGTT_PTE_DM: address is device (stolen) memory
constexpr uint64_t PTE_PAT0      = 1ull << 52;   // XELPG_GGTT_PTE_PAT0
constexpr uint64_t PTE_PAT1      = 1ull << 53;   // XELPG_GGTT_PTE_PAT1
constexpr uint64_t PTE_ADDR_MASK = 0x000ffffffffff000ull; // bits 51:12
constexpr unsigned kPatUncachedLnl = 3;          // xe->pat.idx[XE_CACHE_NONE] on Xe2 iGPU
constexpr unsigned kWa22019338487Every = 63;     // dummy media GMD_ID write every 63 GGTT writes

// Display universal plane 1 (skl_universal_plane_regs.h), pipe stride 0x1000, LNL has pipes A-C
constexpr uint32_t kPipeStride   = 0x1000;
constexpr unsigned kNumPipes     = 3;
constexpr uint32_t PLANE_CTL_1_A    = 0x70180;
constexpr uint32_t PLANE_STRIDE_1_A = 0x70188;
constexpr uint32_t PLANE_SIZE_1_A   = 0x70190;
constexpr uint32_t PLANE_SURF_1_A   = 0x7019c;
constexpr uint32_t PLANE_CTL_ENABLE = 1u << 31;
constexpr uint32_t PLANE_CTL_TILED(uint32_t c)  { return (c >> 10) & 0x7; }  // 0 = linear
constexpr uint32_t PLANE_CTL_FMT_ICL(uint32_t c) { return (c >> 23) & 0x1f; } // icl+ field
constexpr uint32_t kFmtXrgb8888Icl = 8;          // PLANE_CTL_FORMAT_XRGB_8888 (SKL 4 << 24)
constexpr uint32_t PLANE_SURF_ADDR_MASK = 0xfffff000;

// Firmware the full driver will need (linux-firmware, xe/ directory)
//   xe/lnl_guc_70.bin   (GuC >= 70.53.0)
//   xe/lnl_huc.bin
//   xe/lnl_gsc_104.bin  (GSC >= 104.1.0)
//   i915/xe2lpd_dmc.bin (display DMC)

} // namespace xe2
