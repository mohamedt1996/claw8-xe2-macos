// GuC / uC definitions for Xe2 (Lunar Lake).
// Register offsets and ABI layouts transcribed from Linux drivers/gpu/drm/xe:
//   regs/xe_guc_regs.h, regs/xe_gt_regs.h, abi/uc_fw_abi.h, abi/guc_errors_abi.h,
//   abi/guc_messages_abi.h, abi/guc_actions_abi.h, abi/guc_klvs_abi.h,
//   abi/guc_communication_ctb_abi.h, abi/guc_scheduler_abi.h, abi/guc_capture_abi.h,
//   xe_guc_fwif.h (MIT), xe_guc.c, xe_wopcm.c, xe_pat.c, xe_mocs.c, xe_guc_log.h, xe_guc_ct.c.
#pragma once
#include <stdint.h>

namespace xe2 {

// ------------------------------------------------------------------ PAT (xe_pat.c, Xe2)
constexpr uint32_t PAT_ATS = 0x47fc;
constexpr uint32_t patIndexReg(unsigned i) { return i < 8 ? 0x4800 + i * 4 : 0x4848 + (i - 8) * 4; }
constexpr uint32_t XE2_PAT(unsigned noPromote, unsigned compEn, unsigned l3clos,
                           unsigned l3pol, unsigned l4pol, unsigned coh) {
    return (noPromote ? 1u << 10 : 0) | (compEn ? 1u << 9 : 0) | (l3clos << 6) |
           (l3pol << 4) | (l4pol << 2) | coh;
}
constexpr uint32_t kXe2PatTable[32] = {
    XE2_PAT(0,0,0,0,3,0), XE2_PAT(0,0,0,0,3,2), XE2_PAT(0,0,0,0,3,3), XE2_PAT(0,0,0,3,3,0),
    XE2_PAT(0,0,0,3,0,2), XE2_PAT(0,0,0,3,3,2), XE2_PAT(1,0,0,1,3,0), XE2_PAT(0,0,0,3,0,3),
    XE2_PAT(0,0,0,3,0,0), XE2_PAT(0,1,0,0,3,0), XE2_PAT(0,1,0,3,0,0), XE2_PAT(1,1,0,1,3,0),
    XE2_PAT(0,1,0,3,3,0), XE2_PAT(0,0,0,0,0,0), XE2_PAT(0,1,0,0,0,0), XE2_PAT(1,1,0,1,1,0),
    0, 0, 0, 0,                                                     // 16..19 reserved
    XE2_PAT(0,0,1,0,3,0), XE2_PAT(0,1,1,0,3,0), XE2_PAT(0,0,1,0,3,2), XE2_PAT(0,0,1,0,3,3),
    XE2_PAT(0,0,2,0,3,0), XE2_PAT(0,1,2,0,3,0), XE2_PAT(0,0,2,0,3,2), XE2_PAT(0,0,2,0,3,3),
    XE2_PAT(0,0,3,0,3,0), XE2_PAT(0,1,3,0,3,0), XE2_PAT(0,0,3,0,3,2), XE2_PAT(0,0,3,0,3,3),
};
constexpr uint32_t kXe2PatAts = XE2_PAT(0,0,0,0,3,3);
constexpr unsigned kPatWbLnl = 2;          // xe->pat.idx[XE_CACHE_WB]
constexpr uint32_t STEER_SEMAPHORE = 0x0fd0;

// MOCS (xe_mocs.c, LUNARLAKE)
constexpr uint32_t kMocsUcIndexLnl = 3;

// ------------------------------------------------------------------ GT / GuC registers
constexpr uint32_t GDRST           = 0x941c;  constexpr uint32_t GRDOM_GUC = 1u << 3;
constexpr uint32_t PMINTRMSK       = 0xa168;  constexpr uint32_t ARAT_EXPIRED_INTRMSK = 1u << 9;
constexpr uint32_t GT_PM_CONFIG    = 0x13816c; constexpr uint32_t GT_DOORBELL_ENABLE = 1u << 0;
constexpr uint32_t DIST_DBS_POPULATED = 0x0d08;

constexpr uint32_t GUC_STATUS      = 0xc000;
constexpr uint32_t GS_MIA_IN_RESET = 1u << 0;
constexpr uint32_t gsUkernel(uint32_t s) { return (s >> 8) & 0xff; }
constexpr uint32_t gsBootrom(uint32_t s) { return (s >> 1) & 0x7f; }
constexpr uint32_t gsMia(uint32_t s)     { return (s >> 16) & 0x7; }
constexpr uint32_t gsAuth(uint32_t s)    { return (s >> 30) & 0x3; }
constexpr uint32_t BOOT_HASH_CHK   = 0xc010;  constexpr uint32_t GUC_BOOT_UKERNEL_VALID = 1u << 31;
constexpr uint32_t GUC_HEADER_INFO = 0xc014;
constexpr uint32_t GUC_WOPCM_SIZE  = 0xc050;
constexpr uint32_t GUC_WOPCM_SIZE_MASK = 0xfffff000; constexpr uint32_t GUC_WOPCM_SIZE_LOCKED = 1u << 0;
constexpr uint32_t GUC_SHIM_CONTROL = 0xc064;
constexpr uint32_t GUC_ENABLE_MIA_CLOCK_GATING          = 1u << 15;
constexpr uint32_t GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA = 1u << 10;
constexpr uint32_t GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA  = 1u << 9;
constexpr uint32_t GUC_ENABLE_READ_CACHE_LOGIC          = 1u << 1;
constexpr uint32_t guc_mocs_index(uint32_t i) { return (i & 0xf) << 24; }
constexpr uint32_t softScratch(unsigned n)  { return 0xc180 + n * 4; }
constexpr uint32_t uosRsaScratch(unsigned i) { return 0xc200 + i * 4; }
constexpr unsigned UOS_RSA_SCRATCH_COUNT = 64;
constexpr uint32_t DMA_ADDR_0_LOW = 0xc300, DMA_ADDR_0_HIGH = 0xc304;
constexpr uint32_t DMA_ADDR_1_LOW = 0xc308, DMA_ADDR_1_HIGH = 0xc30c;
constexpr uint32_t DMA_ADDRESS_SPACE_WOPCM = 7u << 16, DMA_ADDRESS_SPACE_GGTT = 8u << 16;
constexpr uint32_t DMA_COPY_SIZE = 0xc310, DMA_CTRL = 0xc314;
constexpr uint32_t UOS_MOVE = 1u << 4, START_DMA = 1u << 0;
constexpr uint32_t DMA_GUC_WOPCM_OFFSET = 0xc340;
constexpr uint32_t GUC_WOPCM_OFFSET_MASK = 0xffffc000, GUC_WOPCM_OFFSET_VALID = 1u << 0;
constexpr uint32_t GUC_SRAM_STATUS = 0xc398;  constexpr uint32_t GUC_SRAM_HANDLING_MASK = 0x3u << 7;
constexpr uint32_t GUC_MAX_IDLE_COUNT = 0xc3e4; constexpr uint32_t GUC_IDLE_FLOW_DISABLE = 1u << 31;
constexpr uint32_t GUC_HOST_INTERRUPT = 0x1901f0;            // primary GT notify
constexpr uint32_t vfSwFlag(unsigned n) { return 0x190240 + n * 4; }
constexpr unsigned VF_SW_FLAG_COUNT = 4;

constexpr uint32_t masked_enable(uint32_t b)  { return (b << 16) | b; }
constexpr uint32_t masked_disable(uint32_t b) { return b << 16; }

// ------------------------------------------------------------------ load status (guc_errors_abi.h)
enum : uint32_t {
    GUC_LOAD_READY = 0xF0, GUC_LOAD_DEVID_BUILD_MISMATCH = 0x02, GUC_LOAD_PREPROD_MISMATCH = 0x03,
    GUC_LOAD_DEVID_INVALID_GUCTYPE = 0x04, GUC_LOAD_HWCONFIG_START = 0x05, GUC_LOAD_HWCONFIG_ERROR = 0x07,
    GUC_LOAD_BOOTROM_VERSION_MISMATCH = 0x08, GUC_LOAD_DPC_ERROR = 0x60, GUC_LOAD_EXCEPTION = 0x70,
    GUC_LOAD_INIT_DATA_INVALID = 0x71, GUC_LOAD_MPU_DATA_INVALID = 0x73,
    GUC_LOAD_INIT_MMIO_SAVE_RESTORE_INVALID = 0x74, GUC_LOAD_KLV_WORKAROUND_INIT_ERROR = 0x75,
    GUC_LOAD_INVALID_FTR_FLAG = 0x76,
};
enum : uint32_t {
    BOOTROM_NO_KEY_FOUND = 0x13, BOOTROM_PROD_KEY_CHECK_FAILURE = 0x2B, BOOTROM_RSA_FAILED = 0x50,
    BOOTROM_PAVPC_FAILED = 0x73, BOOTROM_WOPCM_FAILED = 0x74, BOOTROM_LOADLOC_FAILED = 0x75,
    BOOTROM_JUMP_FAILED = 0x77, BOOTROM_RC6CTXCONFIG_FAILED = 0x78, BOOTROM_MPUMAP_INCORRECT = 0x7A,
    BOOTROM_EXCEPTION = 0x7E,
};

// ------------------------------------------------------------------ firmware CSS header (uc_fw_abi.h)
struct __attribute__((packed)) UcCssHeader {
    uint32_t module_type, header_size_dw, header_version, reserved0, module_vendor, date, size_dw;
    uint32_t key_size_dw, modulus_size_dw, exponent_size_dw;          // rsa_info
    // guc_info (22 dwords)
    uint32_t time, reserved1[5], sw_version, submission_version, reserved2[11];
    uint32_t header_info, private_data_size, ukernel_info;
};
static_assert(sizeof(UcCssHeader) == 128, "uc_css_header must be 128 bytes");
constexpr unsigned cssMajor(uint32_t v) { return (v >> 16) & 0xff; }
constexpr unsigned cssMinor(uint32_t v) { return (v >> 8) & 0xff; }
constexpr unsigned cssPatch(uint32_t v) { return v & 0xff; }
constexpr unsigned cssBuildType(uint32_t ukinfo) { return (ukinfo >> 2) & 0x3; }   // 0 prod, 1 preprod, 2 debug

// WOPCM (xe_wopcm.c)
constexpr uint32_t kWopcmTotalLnl       = 8u << 20;   // LNL_WOPCM_SIZE
constexpr uint32_t WOPCM_RESERVED_SIZE  = 16u << 10;
constexpr uint32_t GUC_WOPCM_RESERVED   = 16u << 10;
constexpr uint32_t GUC_WOPCM_STACK_RESERVED = 8u << 10;
constexpr uint32_t WOPCM_HW_CTX_RESERVED = (32u + 4u) << 10;
constexpr uint32_t GUC_WOPCM_OFFSET_ALIGNMENT = 1u << 14;
constexpr uint32_t kGucUcodeWopcmOffset = 0x2000;     // ucode loaded at 8K, below is stack

// ------------------------------------------------------------------ GuC init params (xe_guc_fwif.h)
constexpr unsigned GUC_CTL_LOG_PARAMS = 0, GUC_CTL_WA = 1, GUC_CTL_FEATURE = 2,
                   GUC_CTL_DEBUG = 3, GUC_CTL_ADS = 4, GUC_CTL_DEVID = 5, GUC_CTL_MAX_DWORDS = 14;
constexpr uint32_t GUC_LOG_VALID = 1u << 0, GUC_LOG_NOTIFY_ON_HALF_FULL = 1u << 1,
                   GUC_LOG_CAPTURE_ALLOC_UNITS = 1u << 2, GUC_LOG_DISABLED = 1u << 6;
constexpr uint32_t guc_log_crash(uint32_t v)   { return (v & 0x3) << 4; }
constexpr uint32_t guc_log_event(uint32_t v)   { return (v & 0xf) << 6; }
constexpr uint32_t guc_log_capture(uint32_t v) { return (v & 0x3) << 10; }
constexpr uint32_t guc_ads_addr(uint32_t pfn)  { return (pfn << 1) & 0x003ffffe; }   // GENMASK(21,1)

// GuC log buffer (xe_guc_log.h, non-debug build)
constexpr uint32_t kLogEventSize   = 64u << 10;
constexpr uint32_t kLogCrashSize   = 16u << 10;
constexpr uint32_t kLogCaptureSize = 1u << 20;
constexpr uint32_t kLogSize        = 4096 + kLogEventSize + kLogCrashSize + kLogCaptureSize;

// ------------------------------------------------------------------ ADS (xe_guc_fwif.h, MIT)
constexpr unsigned GUC_MAX_ENGINE_CLASSES = 16, GUC_MAX_INSTANCES_PER_CLASS = 32,
                   GUC_CAPTURE_LIST_INDEX_MAX = 2, GUC_GENERIC_GT_SYSINFO_MAX = 16,
                   GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI = 2, GUC_UM_HW_QUEUE_MAX = 3;
struct __attribute__((packed)) GucMmioRegSet { uint32_t address; uint16_t count, reserved; };
struct __attribute__((packed)) GucAds {
    GucMmioRegSet reg_state_list[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
    uint32_t reserved0, scheduler_policies, gt_system_info, reserved1, control_data;
    uint32_t golden_context_lrca[GUC_MAX_ENGINE_CLASSES];
    uint32_t eng_state_size[GUC_MAX_ENGINE_CLASSES];
    uint32_t private_data, um_init_data;
    uint32_t capture_instance[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
    uint32_t capture_class[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
    uint32_t capture_global[GUC_CAPTURE_LIST_INDEX_MAX];
    uint32_t wa_klv_addr_lo, wa_klv_addr_hi, wa_klv_size, reserved[11];
};
struct __attribute__((packed)) GucPolicies {
    uint32_t submission_queue_depth[GUC_MAX_ENGINE_CLASSES];
    uint32_t dpc_promote_time, is_valid, max_num_work_items, global_flags, reserved[4];
};
struct __attribute__((packed)) GucGtSystemInfo {
    uint8_t  mapping_table[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
    uint32_t engine_enabled_masks[GUC_MAX_ENGINE_CLASSES];
    uint32_t generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_MAX];
};
struct __attribute__((packed)) GucEngineUsageRecord {
    uint32_t current_context_index, last_switch_in_stamp, reserved0, total_runtime, reserved1[4];
};
struct __attribute__((packed)) GucEngineUsage {
    GucEngineUsageRecord engines[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
};
struct __attribute__((packed)) GucUmQueueParams { uint64_t base_dpa; uint32_t base_ggtt_address, size_in_bytes, rsvd[4]; };
struct __attribute__((packed)) GucUmInitParams {
    uint64_t page_response_timeout_in_us; uint32_t rsvd[6];
    GucUmQueueParams queue_params[GUC_UM_HW_QUEUE_MAX];
};
struct __attribute__((packed)) GucAdsBlob {      // struct __guc_ads_blob (fixed part)
    GucAds ads;
    GucPolicies policies;
    GucGtSystemInfo system_info;
    GucEngineUsage engine_usage;
    GucUmInitParams um_init_params;
};
constexpr uint32_t GLOBAL_POLICY_MAX_NUM_WI = 15;
constexpr uint32_t GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US = 500000;

// ------------------------------------------------------------------ HXG messages (guc_messages_abi.h)
constexpr uint32_t HXG_ORIGIN_SHIFT = 31, HXG_TYPE_SHIFT = 28;
constexpr uint32_t HXG_ORIGIN_HOST = 0, HXG_ORIGIN_GUC = 1;
constexpr uint32_t HXG_TYPE_REQUEST = 0, HXG_TYPE_NO_RESPONSE_BUSY = 3, HXG_TYPE_NO_RESPONSE_RETRY = 5,
                   HXG_TYPE_RESPONSE_FAILURE = 6, HXG_TYPE_RESPONSE_SUCCESS = 7;
constexpr uint32_t hxgOrigin(uint32_t h) { return h >> 31; }
constexpr uint32_t hxgType(uint32_t h)   { return (h >> 28) & 0x7; }
constexpr uint32_t hxgData0(uint32_t h)  { return h & 0x0fffffff; }
constexpr uint32_t hxgRequest(uint32_t action, uint32_t data0 = 0) {
    return (HXG_ORIGIN_HOST << HXG_ORIGIN_SHIFT) | (HXG_TYPE_REQUEST << HXG_TYPE_SHIFT) |
           ((data0 & 0xfff) << 16) | (action & 0xffff);
}

// Actions / KLVs
constexpr uint32_t GUC_ACTION_HOST2GUC_SELF_CFG   = 0x0508;
constexpr uint32_t GUC_ACTION_HOST2GUC_CONTROL_CTB = 0x4509;
constexpr uint32_t GUC_CTB_CONTROL_DISABLE = 0, GUC_CTB_CONTROL_ENABLE = 1;
constexpr uint32_t XE_GUC_ACTION_GET_HWCONFIG     = 0x4100;
constexpr uint16_t KLV_H2G_CTB_ADDR = 0x0902, KLV_H2G_CTB_DESC_ADDR = 0x0903, KLV_H2G_CTB_SIZE = 0x0904;
constexpr uint16_t KLV_G2H_CTB_ADDR = 0x0905, KLV_G2H_CTB_DESC_ADDR = 0x0906, KLV_G2H_CTB_SIZE = 0x0907;

// CT buffers (xe_guc_ct.c)
constexpr uint32_t CTB_DESC_SIZE        = 2048;            // ALIGN(sizeof(desc)=64, 2K)
constexpr uint32_t CTB_BUFFER_OFFSET    = CTB_DESC_SIZE * 2;
constexpr uint32_t CTB_H2G_BUFFER_SIZE  = 16u << 10;
constexpr uint32_t CTB_G2H_BUFFER_SIZE  = 128u << 10;

// hwconfig buffer (GuC returns its size; preallocate so no GGTT update is needed after load)
constexpr uint32_t kHwconfigMax = 16u << 10;

} // namespace xe2
