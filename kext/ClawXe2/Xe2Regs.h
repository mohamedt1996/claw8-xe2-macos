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

// Firmware the full driver will need (linux-firmware, xe/ directory)
//   xe/lnl_guc_70.bin   (GuC >= 70.53.0)
//   xe/lnl_huc.bin
//   xe/lnl_gsc_104.bin  (GSC >= 104.1.0)
//   i915/xe2lpd_dmc.bin (display DMC)

} // namespace xe2
