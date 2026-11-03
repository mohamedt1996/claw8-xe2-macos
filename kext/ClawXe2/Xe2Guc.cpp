// ClawXe2 — Milestone 3: GuC firmware load (Xe2 / Lunar Lake primary GT).
//
// Sequence mirrors Linux xe: xe_pat_init -> xe_wopcm_init -> xe_uc_sanitize_reset ->
// xe_guc_min_load_for_hwconfig (ADS minimal, params, prepare_xfer, RSA, DMA upload, wait READY)
// -> xe_guc_hwconfig_init (MMIO H2G) -> CTB registration + enable (MMIO H2G).
//
// Opt-in with boot-arg clawxe2guc=1. Loading the GuC locks the WOPCM registers until the next
// reboot/FLR. stop() resets the GuC before any memory it can reach is freed.
//
// SPDX-License-Identifier: GPL-2.0-only  (logic derived from Linux drivers/gpu/drm/xe)

#include "ClawXe2.hpp"
#include "Xe2Regs.h"
#include "Xe2Guc.h"
#include <IOKit/IOLib.h>
#include <libkern/libkern.h>
#include <string.h>
#include <stddef.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

#define LOG(fmt, ...)  IOLog("ClawXe2: " fmt "\n", ##__VA_ARGS__)

extern "C" {
extern const unsigned char clawxe2_guc_fw[];
extern const unsigned int  clawxe2_guc_fw_len;
extern const char          clawxe2_guc_fw_name[];
}

static inline uint32_t pageAlign(uint32_t v) { return (v + 4095u) & ~4095u; }

// --------------------------------------------------------------------------------------------
void ClawXe2::m3Run() {
    uint32_t arg = 0;
    if (!(PE_parse_boot_argn("clawxe2guc", &arg, sizeof(arg)) && arg == 1)) {
        setProperty("xe2-guc", "skipped (set clawxe2guc=1)");
        return;
    }
    setProperty("xe2-milestone", "M3-guc");
    if (!gsm_) { setProperty("xe2-guc", "needs-m2-ggtt"); return; }
    if (clawxe2_guc_fw_len == 0) { setProperty("xe2-guc", "no-firmware-embedded"); return; }
    setProperty("xe2-guc-fw-file", clawxe2_guc_fw_name);

    if (!forcewake(true)) { forcewake(false); setProperty("xe2-guc", "forcewake-timeout"); return; }

    bool ok = false;
    do {
        patProgram();
        if (!gucParseFw(clawxe2_guc_fw, clawxe2_guc_fw_len)) break;

        uint32_t adsSize = pageAlign(sizeof(xe2::GucAdsBlob)) + pageAlign(gucPrivDataSize_);
        if (!bufAlloc(fwBuf_, clawxe2_guc_fw_len, "fw") ||
            !bufAlloc(adsBuf_, adsSize, "ads") ||
            !bufAlloc(logBuf_, xe2::kLogSize, "log") ||
            !bufAlloc(h2gBuf_, xe2::CTB_BUFFER_OFFSET + xe2::CTB_H2G_BUFFER_SIZE, "ct-h2g") ||
            !bufAlloc(g2hBuf_, xe2::CTB_BUFFER_OFFSET + xe2::CTB_G2H_BUFFER_SIZE, "ct-g2h") ||
            !bufAlloc(hwcfgBuf_, xe2::kHwconfigMax, "hwconfig"))
            break;
        memcpy(fwBuf_.cpu, clawxe2_guc_fw, clawxe2_guc_fw_len);
        // Drain GGTT writes before anything reads them (Wa_22019338487) + posting read.
        if (mediaIp_ == 2000) wr(xe2::MEDIA_GT_GSI_OFFSET + xe2::GMD_ID, 0);
        (void)rd(xe2::GMD_ID);

        if (!wopcmInit()) break;
        if (!gucReset())  break;
        adsPopulateMinimal();
        if (!gucUpload()) break;
        gucRunning_ = true;

        if (!gucHwconfig()) break;
        if (!gucCtEnable()) break;
        ok = true;
    } while (false);

    forcewake(false);
    if (!ok) gucTeardown();     // never leave a half-initialised GuC pointing at our memory
    setProperty("xe2-guc", ok ? "running" : "failed");
    LOG("M3 %s", ok ? "complete: GuC READY, hwconfig read, CT enabled" : "stopped, see xe2-guc-* properties");
}

// --------------------------------------------------------------------------------------------
// GPU-visible buffers: kernel memory (46-bit DMA mask), mapped page by page into a free GGTT
// window, PAT index 2 (WB, 2-way coherent) so CPU-cached writes are visible to the GuC.
bool ClawXe2::bufAlloc(GpuBuf &b, uint32_t size, const char *what) {
    b.size = pageAlign(size);
    b.md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut, b.size, 0x00003ffffffff000ull);
    if (!b.md || b.md->prepare() != kIOReturnSuccess) {
        OSSafeReleaseNULL(b.md);
        LOG("alloc %s (%u bytes) failed", what, b.size);
        setProperty("xe2-guc-error", "alloc-failed");
        return false;
    }
    b.cpu = reinterpret_cast<uint8_t *>(b.md->getBytesNoCopy());
    memset(b.cpu, 0, b.size);

    // Find b.size/4K consecutive non-present PTEs, searching downward from the last window.
    const uint64_t pages = b.size / xe2::kPageSize;
    uint64_t top = ggttCursor_ ? ggttCursor_ : xe2::GUC_GGTT_TOP;
    uint64_t run = 0, start = 0;
    for (uint64_t a = top - xe2::kPageSize, n = 0; a >= ggttStart_ && n < 65536; a -= xe2::kPageSize, n++) {
        if (ggttGetPte(a) & xe2::PTE_PRESENT) { run = 0; continue; }
        if (++run == pages) { start = a; break; }
    }
    if (!start) {
        LOG("no GGTT window for %s", what);
        setProperty("xe2-guc-error", "ggtt-window-not-found");
        b.md->complete(); OSSafeReleaseNULL(b.md);
        return false;
    }
    for (uint64_t i = 0; i < pages; i++) {
        IOByteCount seg = 0;
        uint64_t phys = b.md->getPhysicalSegment(i * xe2::kPageSize, &seg, kIOMemoryMapperNone);
        ggttSetPte(start + i * xe2::kPageSize, ggttEncode(phys, xe2::kPatWbLnl, false));
    }
    b.ggtt = start;
    ggttCursor_ = start;
    LOG("%s: %u KiB at GGTT 0x%llx", what, b.size >> 10, (unsigned long long)start);
    return true;
}

void ClawXe2::bufFree(GpuBuf &b) {
    if (b.ggtt && gsm_) {
        for (uint64_t off = 0; off < b.size; off += xe2::kPageSize) ggttSetPte(b.ggtt + off, 0);
        if (mediaIp_ == 2000) wr(xe2::MEDIA_GT_GSI_OFFSET + xe2::GMD_ID, 0);
    }
    if (b.md) { b.md->complete(); OSSafeReleaseNULL(b.md); }
    b = GpuBuf{};
}

// --------------------------------------------------------------------------------------------
bool ClawXe2::waitReg(uint32_t reg, uint32_t mask, uint32_t want, uint32_t timeoutUs, uint32_t *last) {
    uint32_t v = 0;
    const bool coarse = timeoutUs > 20000;
    for (uint32_t t = 0; ; ) {
        v = rd(reg);
        if ((v & mask) == want) { if (last) *last = v; return true; }
        if (t >= timeoutUs) break;
        if (coarse) { IOSleep(1); t += 1000; } else { IODelay(10); t += 10; }
    }
    if (last) *last = v;
    return false;
}

// MCR steering semaphore (graphics >= 12.70): acquired when a read returns 1, released by writing 1.
bool ClawXe2::mcrLock() {
    return waitReg(xe2::STEER_SEMAPHORE, 0x1, 0x1, 100);
}
void ClawXe2::mcrUnlock() { wr(xe2::STEER_SEMAPHORE, 0x1); }

// xe_pat_init(): Xe2 graphics PAT via MCR multicast writes + PAT_ATS (no PTA on iGPU)
void ClawXe2::patProgram() {
    bool locked = mcrLock();
    for (unsigned i = 0; i < 32; i++) wr(xe2::patIndexReg(i), xe2::kXe2PatTable[i]);
    wr(xe2::PAT_ATS, xe2::kXe2PatAts);
    if (locked) mcrUnlock();
    setProperty("xe2-pat", locked ? "programmed" : "programmed (steer semaphore timeout)");
    setNumber("xe2-pat2-readback", rd(xe2::patIndexReg(2)));
}

// --------------------------------------------------------------------------------------------
// parse_css_header() + guc_read_css_info()
bool ClawXe2::gucParseFw(const uint8_t *fw, uint32_t len) {
    if (len < sizeof(xe2::UcCssHeader)) { setProperty("xe2-guc-error", "fw-too-small"); return false; }
    xe2::UcCssHeader css;
    memcpy(&css, fw, sizeof(css));

    uint32_t hdr = (css.header_size_dw - css.key_size_dw - css.modulus_size_dw - css.exponent_size_dw) * 4;
    if (hdr != sizeof(xe2::UcCssHeader)) { setProperty("xe2-guc-error", "css-header-size"); return false; }
    gucUcodeSize_    = (css.size_dw - css.header_size_dw) * 4;
    gucRsaSize_      = css.key_size_dw * 4;
    gucPrivDataSize_ = css.private_data_size;
    if (len < sizeof(xe2::UcCssHeader) + gucUcodeSize_ + gucRsaSize_) {
        setProperty("xe2-guc-error", "fw-truncated"); return false;
    }

    unsigned maj = xe2::cssMajor(css.sw_version), min = xe2::cssMinor(css.sw_version),
             pat = xe2::cssPatch(css.sw_version);
    char ver[32];
    snprintf(ver, sizeof(ver), "%u.%u.%u", maj, min, pat);
    setProperty("xe2-guc-fw-version", ver);
    snprintf(ver, sizeof(ver), "%u.%u.%u", xe2::cssMajor(css.submission_version),
             xe2::cssMinor(css.submission_version), xe2::cssPatch(css.submission_version));
    setProperty("xe2-guc-fw-submission-version", ver);
    setNumber("xe2-guc-fw-ucode-size", gucUcodeSize_);
    setNumber("xe2-guc-fw-rsa-size", gucRsaSize_);
    setNumber("xe2-guc-fw-private-data-size", gucPrivDataSize_);
    setNumber("xe2-guc-fw-build-type", xe2::cssBuildType(css.ukernel_info), 8);

    // "We don't support GuC releases older than 70.29.2"
    if ((maj << 16 | min << 8 | pat) < (70u << 16 | 29u << 8 | 2u)) {
        setProperty("xe2-guc-error", "fw-too-old"); return false;
    }
    LOG("GuC firmware %u.%u.%u, ucode %u bytes, rsa %u bytes", maj, min, pat, gucUcodeSize_, gucRsaSize_);
    return true;
}

// --------------------------------------------------------------------------------------------
// xe_wopcm_init() + __wopcm_init_regs() for LNL (no HuC partition reserved in M3)
bool ClawXe2::wopcmInit() {
    const uint32_t guc_fw_size = sizeof(xe2::UcCssHeader) + gucUcodeSize_;
    uint32_t regBase = rd(xe2::DMA_GUC_WOPCM_OFFSET), regSize = rd(xe2::GUC_WOPCM_SIZE);
    bool locked = (regSize & xe2::GUC_WOPCM_SIZE_LOCKED) && (regBase & xe2::GUC_WOPCM_OFFSET_VALID);

    uint32_t base, size;
    if (locked) {
        base = regBase & xe2::GUC_WOPCM_OFFSET_MASK;
        size = regSize & xe2::GUC_WOPCM_SIZE_MASK;
    } else {
        base = (0 + xe2::WOPCM_RESERVED_SIZE + xe2::GUC_WOPCM_OFFSET_ALIGNMENT - 1) &
               ~(xe2::GUC_WOPCM_OFFSET_ALIGNMENT - 1);
        uint32_t lim = xe2::kWopcmTotalLnl - xe2::WOPCM_HW_CTX_RESERVED;
        if (base > lim) base = lim;
        size = (xe2::kWopcmTotalLnl - xe2::WOPCM_HW_CTX_RESERVED - base) & xe2::GUC_WOPCM_SIZE_MASK;
    }

    // __check_layout()
    uint32_t avail = xe2::kWopcmTotalLnl - xe2::WOPCM_HW_CTX_RESERVED;
    if (base >= avail || size > avail - base ||
        size < guc_fw_size + xe2::GUC_WOPCM_RESERVED + xe2::GUC_WOPCM_STACK_RESERVED) {
        setProperty("xe2-guc-error", "wopcm-layout"); return false;
    }

    if (!locked) {
        wr(xe2::GUC_WOPCM_SIZE, size);
        if ((rd(xe2::GUC_WOPCM_SIZE) & (xe2::GUC_WOPCM_SIZE_MASK | xe2::GUC_WOPCM_SIZE_LOCKED)) !=
            (size | xe2::GUC_WOPCM_SIZE_LOCKED)) { setProperty("xe2-guc-error", "wopcm-size-verify"); return false; }
        wr(xe2::DMA_GUC_WOPCM_OFFSET, base);
        if ((rd(xe2::DMA_GUC_WOPCM_OFFSET) & (xe2::GUC_WOPCM_OFFSET_MASK | xe2::GUC_WOPCM_OFFSET_VALID)) !=
            (base | xe2::GUC_WOPCM_OFFSET_VALID)) { setProperty("xe2-guc-error", "wopcm-offset-verify"); return false; }
    }
    wopcmBase_ = base; wopcmSize_ = size;
    setNumber("xe2-guc-wopcm-base", base);
    setNumber("xe2-guc-wopcm-size", size);
    setProperty("xe2-guc-wopcm-preprogrammed", locked);
    return true;
}

// --------------------------------------------------------------------------------------------
// xe_guc_reset(), including Wa_14025883347 (graphics 20.04 - 30.05)
bool ClawXe2::gucReset() {
    uint32_t st = rd(xe2::GUC_STATUS);
    if (!(st & xe2::GS_MIA_IN_RESET) && (rd(xe2::BOOT_HASH_CHK) & xe2::GUC_BOOT_UKERNEL_VALID)) {
        wr(xe2::GUC_MAX_IDLE_COUNT, rd(xe2::GUC_MAX_IDLE_COUNT) | xe2::GUC_IDLE_FLOW_DISABLE);
        waitReg(xe2::GUC_STATUS, 0xff00, xe2::GUC_LOAD_READY << 8, 100000);
        waitReg(xe2::GUC_SRAM_STATUS, xe2::GUC_SRAM_HANDLING_MASK, 0, 5000);
    }

    wr(xe2::GDRST, xe2::GRDOM_GUC);
    uint32_t gdrst = 0;
    if (!waitReg(xe2::GDRST, xe2::GRDOM_GUC, 0, 5000, &gdrst)) {
        setNumber("xe2-guc-gdrst", gdrst);
        setProperty("xe2-guc-error", "reset-timeout");
        return false;
    }
    st = rd(xe2::GUC_STATUS);
    if (!(st & xe2::GS_MIA_IN_RESET)) {
        setNumber("xe2-guc-status", st);
        setProperty("xe2-guc-error", "mia-not-in-reset");
        return false;
    }
    gucRunning_ = false;
    ctEnabled_ = false;
    return true;
}

// --------------------------------------------------------------------------------------------
// xe_guc_ads_populate_minimal(): no engines, invalid mapping table, policies, doorbells
void ClawXe2::adsPopulateMinimal() {
    auto *blob = reinterpret_cast<xe2::GucAdsBlob *>(adsBuf_.cpu);
    memset(adsBuf_.cpu, 0, adsBuf_.size);
    const uint32_t base = uint32_t(adsBuf_.ggtt);
    const uint32_t privOff = pageAlign(sizeof(xe2::GucAdsBlob));

    blob->policies.dpc_promote_time   = xe2::GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US;
    blob->policies.max_num_work_items = xe2::GLOBAL_POLICY_MAX_NUM_WI;
    blob->policies.global_flags       = 0;
    blob->policies.is_valid           = 1;

    for (unsigned c = 0; c < xe2::GUC_MAX_ENGINE_CLASSES; c++)
        for (unsigned i = 0; i < xe2::GUC_MAX_INSTANCES_PER_CLASS; i++)
            blob->system_info.mapping_table[c][i] = xe2::GUC_MAX_INSTANCES_PER_CLASS;

    uint32_t dbs = rd(xe2::DIST_DBS_POPULATED);
    blob->system_info.generic_gt_sysinfo[xe2::GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI] =
        ((dbs >> 16) & 0xff) + 1;

    blob->ads.scheduler_policies = base + offsetof(xe2::GucAdsBlob, policies);
    blob->ads.gt_system_info     = base + offsetof(xe2::GucAdsBlob, system_info);
    blob->ads.private_data       = base + privOff;

    setNumber("xe2-guc-ads-ggtt", base);
    setNumber("xe2-guc-ads-blob-size", sizeof(xe2::GucAdsBlob));
}

// --------------------------------------------------------------------------------------------
// guc_init_params() + guc_write_params() + guc_prepare_xfer() + guc_xfer_rsa() + uc_fw_xfer()
bool ClawXe2::gucUpload() {
    uint32_t params[xe2::GUC_CTL_MAX_DWORDS] = {};
    // Log: crash 16K and event 64K in 4K units, capture 1M in 1M units
    params[xe2::GUC_CTL_LOG_PARAMS] = xe2::GUC_LOG_VALID | xe2::GUC_LOG_NOTIFY_ON_HALF_FULL |
        xe2::GUC_LOG_CAPTURE_ALLOC_UNITS |
        xe2::guc_log_crash(xe2::kLogCrashSize / 4096 - 1) |
        xe2::guc_log_event(xe2::kLogEventSize / 4096 - 1) |
        xe2::guc_log_capture(xe2::kLogCaptureSize / (1u << 20) - 1) |
        (uint32_t(logBuf_.ggtt) & 0xfffff000);
    params[xe2::GUC_CTL_FEATURE] = 0;
    params[xe2::GUC_CTL_DEBUG]   = xe2::GUC_LOG_DISABLED;
    params[xe2::GUC_CTL_ADS]     = xe2::guc_ads_addr(uint32_t(adsBuf_.ggtt >> 12));
    params[xe2::GUC_CTL_WA]      = 0;
    params[xe2::GUC_CTL_DEVID]   = (uint32_t(pci_->configRead16(kIOPCIConfigDeviceID)) << 16) |
                                   pci_->configRead8(kIOPCIConfigRevisionID);

    wr(xe2::softScratch(0), 0);
    for (unsigned i = 0; i < xe2::GUC_CTL_MAX_DWORDS; i++) wr(xe2::softScratch(1 + i), params[i]);

    wr(xe2::GUC_SHIM_CONTROL, xe2::GUC_ENABLE_READ_CACHE_LOGIC | xe2::GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA |
                              xe2::GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA | xe2::GUC_ENABLE_MIA_CLOCK_GATING |
                              xe2::guc_mocs_index(xe2::kMocsUcIndexLnl));
    wr(xe2::GT_PM_CONFIG, xe2::GT_DOORBELL_ENABLE);
    wr(xe2::PMINTRMSK, rd(xe2::PMINTRMSK) & ~xe2::ARAT_EXPIRED_INTRMSK);

    // RSA: by GGTT address if the key is > 256 bytes, otherwise through the scratch registers
    const uint32_t rsaOff = sizeof(xe2::UcCssHeader) + gucUcodeSize_;
    if (gucRsaSize_ > 256) {
        wr(xe2::uosRsaScratch(0), uint32_t(fwBuf_.ggtt) + rsaOff);
    } else {
        if (gucRsaSize_ < xe2::UOS_RSA_SCRATCH_COUNT * 4) { setProperty("xe2-guc-error", "rsa-too-small"); return false; }
        const uint32_t *rsa = reinterpret_cast<const uint32_t *>(fwBuf_.cpu + rsaOff);
        for (unsigned i = 0; i < xe2::UOS_RSA_SCRATCH_COUNT; i++) wr(xe2::uosRsaScratch(i), rsa[i]);
    }

    // DMA header + ucode from GGTT into WOPCM at 8K
    const uint64_t src = fwBuf_.ggtt;   // css_offset == 0 for CSS-layout GuC binaries
    wr(xe2::DMA_ADDR_0_LOW, uint32_t(src));
    wr(xe2::DMA_ADDR_0_HIGH, uint32_t(src >> 32) | xe2::DMA_ADDRESS_SPACE_GGTT);
    wr(xe2::DMA_ADDR_1_LOW, xe2::kGucUcodeWopcmOffset);
    wr(xe2::DMA_ADDR_1_HIGH, xe2::DMA_ADDRESS_SPACE_WOPCM);
    wr(xe2::DMA_COPY_SIZE, sizeof(xe2::UcCssHeader) + gucUcodeSize_);
    wr(xe2::DMA_CTRL, xe2::masked_enable(xe2::UOS_MOVE | xe2::START_DMA));
    uint32_t dmaCtrl = 0;
    bool dmaOk = waitReg(xe2::DMA_CTRL, xe2::START_DMA, 0, 100000, &dmaCtrl);
    wr(xe2::DMA_CTRL, xe2::masked_disable(xe2::UOS_MOVE));
    if (!dmaOk) {
        setNumber("xe2-guc-dma-ctrl", dmaCtrl);
        setProperty("xe2-guc-error", "dma-timeout");
        return false;
    }

    uint32_t status = 0, ms = 0;
    bool ready = gucWaitReady(&status, &ms);
    setNumber("xe2-guc-status", status);
    setNumber("xe2-guc-load-ms", ms);
    setNumber("xe2-guc-status-ukernel", xe2::gsUkernel(status), 8);
    setNumber("xe2-guc-status-bootrom", xe2::gsBootrom(status), 8);
    setNumber("xe2-guc-status-auth", xe2::gsAuth(status), 8);
    if (!ready) {
        if (xe2::gsUkernel(status) == xe2::GUC_LOAD_EXCEPTION) setNumber("xe2-guc-exception-eip", rd(xe2::softScratch(13)));
        if (xe2::gsBootrom(status) == xe2::BOOTROM_NO_KEY_FOUND) setNumber("xe2-guc-header-info", rd(xe2::GUC_HEADER_INFO));
        setProperty("xe2-guc-error", "load-failed");
        LOG("GuC load failed: status 0x%08x (ukernel 0x%02x, bootrom 0x%02x, auth %u) after %u ms",
            status, xe2::gsUkernel(status), xe2::gsBootrom(status), xe2::gsAuth(status), ms);
        return false;
    }
    LOG("GuC READY in %u ms (status 0x%08x)", ms, status);
    return true;
}

// guc_wait_ucode()/guc_load_done(): poll every 10 ms, up to 5 s
bool ClawXe2::gucWaitReady(uint32_t *status, uint32_t *ms) {
    for (uint32_t t = 0; t <= 5000; t += 10) {
        uint32_t s = rd(xe2::GUC_STATUS);
        *status = s; *ms = t;
        uint32_t uk = xe2::gsUkernel(s), br = xe2::gsBootrom(s);
        if (uk == xe2::GUC_LOAD_READY) return true;
        switch (uk) {
        case xe2::GUC_LOAD_DEVID_BUILD_MISMATCH: case xe2::GUC_LOAD_PREPROD_MISMATCH:
        case xe2::GUC_LOAD_DEVID_INVALID_GUCTYPE: case xe2::GUC_LOAD_HWCONFIG_ERROR:
        case xe2::GUC_LOAD_BOOTROM_VERSION_MISMATCH: case xe2::GUC_LOAD_DPC_ERROR:
        case xe2::GUC_LOAD_EXCEPTION: case xe2::GUC_LOAD_INIT_DATA_INVALID:
        case xe2::GUC_LOAD_MPU_DATA_INVALID: case xe2::GUC_LOAD_INIT_MMIO_SAVE_RESTORE_INVALID:
        case xe2::GUC_LOAD_KLV_WORKAROUND_INIT_ERROR: case xe2::GUC_LOAD_INVALID_FTR_FLAG:
            return false;
        }
        switch (br) {
        case xe2::BOOTROM_NO_KEY_FOUND: case xe2::BOOTROM_RSA_FAILED: case xe2::BOOTROM_PAVPC_FAILED:
        case xe2::BOOTROM_WOPCM_FAILED: case xe2::BOOTROM_LOADLOC_FAILED: case xe2::BOOTROM_JUMP_FAILED:
        case xe2::BOOTROM_RC6CTXCONFIG_FAILED: case xe2::BOOTROM_MPUMAP_INCORRECT:
        case xe2::BOOTROM_EXCEPTION: case xe2::BOOTROM_PROD_KEY_CHECK_FAILURE:
            return false;
        }
        IOSleep(10);
    }
    return false;
}

// --------------------------------------------------------------------------------------------
// xe_guc_mmio_send_recv(): returns DATA0 (>= 0) or a negative error
int64_t ClawXe2::gucMmioSend(const uint32_t *req, unsigned len, uint32_t *resp) {
    if (!len || len > xe2::VF_SW_FLAG_COUNT) return -22;
    for (unsigned retry = 0; retry < 8; retry++) {
        for (unsigned i = 0; i < len; i++) wr(xe2::vfSwFlag(i), req[i]);
        (void)rd(xe2::vfSwFlag(xe2::VF_SW_FLAG_COUNT - 1));
        wr(xe2::GUC_HOST_INTERRUPT, 0);

        uint32_t h = 0;
        if (!waitReg(xe2::vfSwFlag(0), 1u << 31, 1u << 31, 50000, &h)) {
            setNumber("xe2-guc-mmio-last-header", h);
            return -60;   // timeout
        }
        if (xe2::hxgType(h) == xe2::HXG_TYPE_NO_RESPONSE_BUSY) {
            const uint32_t m = (xe2::HXG_TYPE_RESPONSE_SUCCESS & xe2::HXG_TYPE_RESPONSE_FAILURE) << 28;
            if (!waitReg(xe2::vfSwFlag(0), m, m, 2000000, &h)) return -60;
        }
        if (xe2::hxgType(h) == xe2::HXG_TYPE_NO_RESPONSE_RETRY) { IOSleep(1u << retry); continue; }
        if (xe2::hxgType(h) == xe2::HXG_TYPE_RESPONSE_FAILURE) {
            setNumber("xe2-guc-mmio-failure", h);
            return -6;
        }
        if (xe2::hxgType(h) != xe2::HXG_TYPE_RESPONSE_SUCCESS || xe2::hxgOrigin(h) != xe2::HXG_ORIGIN_GUC) {
            setNumber("xe2-guc-mmio-unexpected", h);
            return -71;
        }
        if (resp) {
            resp[0] = h;
            for (unsigned i = 1; i < xe2::VF_SW_FLAG_COUNT; i++) resp[i] = rd(xe2::vfSwFlag(i));
        }
        return xe2::hxgData0(h);
    }
    return -11;
}

// guc_self_cfg(): must reply with DATA0 == 1
bool ClawXe2::gucSelfCfg(uint16_t key, uint16_t len, uint64_t val) {
    const uint32_t req[4] = {
        xe2::hxgRequest(xe2::GUC_ACTION_HOST2GUC_SELF_CFG),
        (uint32_t(key) << 16) | len,
        uint32_t(val),
        uint32_t(val >> 32),
    };
    int64_t r = gucMmioSend(req, 4);
    if (r != 1) {
        char e[48];
        snprintf(e, sizeof(e), "self-cfg 0x%04x -> %lld", key, (long long)r);
        setProperty("xe2-guc-error", e);
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------------------------
// xe_guc_hwconfig_init(): ask the size, then have the GuC copy the KLV table into our buffer.
// This is the first time the GuC writes system memory through our GGTT mapping (M2 end-to-end).
bool ClawXe2::gucHwconfig() {
    const uint32_t q[4] = { xe2::XE_GUC_ACTION_GET_HWCONFIG, 0, 0, 0 };
    int64_t size = gucMmioSend(q, 4);
    if (size <= 0 || size > xe2::kHwconfigMax) {
        setNumber("xe2-guc-hwconfig-size", uint64_t(size), 64);
        setProperty("xe2-guc-error", size <= 0 ? "hwconfig-size-query" : "hwconfig-too-big");
        return false;
    }
    memset(hwcfgBuf_.cpu, 0xA5, hwcfgBuf_.size);   // poison: proves the GuC actually wrote
    const uint32_t c[4] = { xe2::XE_GUC_ACTION_GET_HWCONFIG, uint32_t(hwcfgBuf_.ggtt),
                            uint32_t(hwcfgBuf_.ggtt >> 32), uint32_t(size) };
    int64_t r = gucMmioSend(c, 4);
    if (r < 0) { setProperty("xe2-guc-error", "hwconfig-copy"); return false; }

    // Parse KLVs: key, len (dwords), value[len]
    const uint32_t *d = reinterpret_cast<const uint32_t *>(hwcfgBuf_.cpu);
    const uint32_t dw = uint32_t(size) / 4;
    OSDictionary *dict = OSDictionary::withCapacity(64);
    unsigned count = 0;
    bool sane = true;
    for (uint32_t i = 0; i + 2 <= dw; ) {
        uint32_t key = d[i], n = d[i + 1];
        if (i + 2 + n > dw) { sane = false; break; }
        if (dict && n >= 1) {
            char k[12]; snprintf(k, sizeof(k), "%u", key);
            OSNumber *num = OSNumber::withNumber(d[i + 2], 32);
            if (num) { dict->setObject(k, num); num->release(); }
        }
        count++;
        i += 2 + n;
    }
    setNumber("xe2-guc-hwconfig-size", uint64_t(size), 32);
    setNumber("xe2-guc-hwconfig-klvs", count);
    if (dict) { setProperty("xe2-guc-hwconfig", dict); dict->release(); }
    if (OSData *raw = OSData::withBytes(hwcfgBuf_.cpu, uint32_t(size))) {
        setProperty("xe2-guc-hwconfig-raw", raw); raw->release();
    }
    bool written = d[0] != 0xA5A5A5A5u;
    setProperty("xe2-guc-hwconfig-status", !written ? "not-written" : sane ? "ok" : "malformed");
    LOG("hwconfig: %lld bytes, %u KLVs%s", (long long)size, count, written ? "" : " (buffer untouched!)");
    return written && sane && count > 0;
}

// --------------------------------------------------------------------------------------------
// CTB registration (guc_ct_ctb_{h2g,g2h}_register) + guc_ct_control_toggle(enable)
bool ClawXe2::gucCtEnable() {
    memset(h2gBuf_.cpu, 0, xe2::CTB_BUFFER_OFFSET);
    memset(g2hBuf_.cpu, 0, xe2::CTB_BUFFER_OFFSET);
    const uint64_t h2gDesc = h2gBuf_.ggtt, h2gCmds = h2gBuf_.ggtt + xe2::CTB_BUFFER_OFFSET;
    const uint64_t g2hDesc = g2hBuf_.ggtt + xe2::CTB_DESC_SIZE, g2hCmds = g2hBuf_.ggtt + xe2::CTB_BUFFER_OFFSET;

    if (!gucSelfCfg(xe2::KLV_H2G_CTB_DESC_ADDR, 2, h2gDesc) ||
        !gucSelfCfg(xe2::KLV_H2G_CTB_ADDR, 2, h2gCmds) ||
        !gucSelfCfg(xe2::KLV_H2G_CTB_SIZE, 1, xe2::CTB_H2G_BUFFER_SIZE) ||
        !gucSelfCfg(xe2::KLV_G2H_CTB_DESC_ADDR, 2, g2hDesc) ||
        !gucSelfCfg(xe2::KLV_G2H_CTB_ADDR, 2, g2hCmds) ||
        !gucSelfCfg(xe2::KLV_G2H_CTB_SIZE, 1, xe2::CTB_G2H_BUFFER_SIZE))
        return false;

    const uint32_t req[2] = { xe2::hxgRequest(xe2::GUC_ACTION_HOST2GUC_CONTROL_CTB), xe2::GUC_CTB_CONTROL_ENABLE };
    int64_t r = gucMmioSend(req, 2);
    if (r != 0) {
        setNumber("xe2-guc-ct-enable-result", uint64_t(r), 64);
        setProperty("xe2-guc-error", "ct-enable");
        return false;
    }
    ctEnabled_ = true;
    setProperty("xe2-guc-ct", "enabled");
    setNumber("xe2-guc-ct-h2g-ggtt", h2gDesc, 64);
    setNumber("xe2-guc-ct-g2h-ggtt", g2hBuf_.ggtt, 64);
    LOG("CT enabled (H2G 0x%llx, G2H 0x%llx)", (unsigned long long)h2gDesc, (unsigned long long)g2hBuf_.ggtt);
    return true;
}

// --------------------------------------------------------------------------------------------
// Stop the GuC before freeing anything it can DMA into.
void ClawXe2::gucTeardown() {
    if (mmio_) {
        bool fw = forcewake(true);
        if (ctEnabled_) {
            const uint32_t req[2] = { xe2::hxgRequest(xe2::GUC_ACTION_HOST2GUC_CONTROL_CTB), xe2::GUC_CTB_CONTROL_DISABLE };
            gucMmioSend(req, 2);
            ctEnabled_ = false;
        }
        if (gucRunning_ || fwBuf_.md) gucReset();
        (void)fw;
        forcewake(false);
    }
    bufFree(hwcfgBuf_); bufFree(g2hBuf_); bufFree(h2gBuf_);
    bufFree(logBuf_);   bufFree(adsBuf_); bufFree(fwBuf_);
    ggttCursor_ = 0;
}
