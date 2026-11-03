// ClawXe2 — Milestone 2: stolen memory (DSM), GGTT access, scanout decode, GGTT self-test.
//
// Read-only by default:
//   * Stolen memory layout from GGC / STOLEN_RESERVED / GSCPSMI_BASE (xe_ttm_stolen_mgr.c)
//   * GGTT PTE array at BAR0 + 8 MiB (xe_ggtt.c)
//   * Firmware (GOP) scanout plane -> GGTT -> CPU physical address, contiguity check
// Opt-in writes:
//   clawxe2fb=1    draw a 256x256 checkerboard into the GOP framebuffer through the
//                  GGTT-resolved address (pass = pattern appears top-left on the panel)
//   clawxe2ggtt=1  map a test page into a free GGTT slot, read PTE back, restore
//
// SPDX-License-Identifier: GPL-2.0-only  (logic derived from Linux drivers/gpu/drm/xe)

#include "ClawXe2.hpp"
#include "Xe2Regs.h"
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <pexpert/pexpert.h>

#define LOG(fmt, ...)  IOLog("ClawXe2: " fmt "\n", ##__VA_ARGS__)

static bool bootArgIs1(const char *name) {
    uint32_t v = 0;
    return PE_parse_boot_argn(name, &v, sizeof(v)) && v == 1;
}

// --------------------------------------------------------------------------------------------
void ClawXe2::m2Run() {
    setProperty("xe2-milestone", "M2-ggtt");
    probeStolen();
    if (!ggttInit()) return;
    scanoutDecode();
    if (bootArgIs1("clawxe2ggtt")) ggttSelfTest();
    else setProperty("xe2-ggtt-selftest", "skipped (set clawxe2ggtt=1)");
}

// --------------------------------------------------------------------------------------------
// Stolen memory: detect_lmembar_integrated() for graphics >= 12.70
bool ClawXe2::probeStolen() {
    stolen_ = Stolen{};

    IODeviceMemory *bar2 = pci_->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!bar2) { setProperty("xe2-stolen", "no-bar2"); return false; }
    stolen_.bar2Phys = bar2->getPhysicalAddress();
    stolen_.bar2Len  = bar2->getLength();
    setNumber("xe2-bar2-phys", stolen_.bar2Phys, 64);
    setNumber("xe2-bar2-length", stolen_.bar2Len, 64);

    uint32_t ggc = rd(xe2::GGC);
    setNumber("xe2-ggc", ggc);
    if (((ggc >> 6) & 0x3) != 0x3) {            // GGMS must be 8 MiB
        setProperty("xe2-stolen", "ggms-not-8mib");
        LOG("GGC 0x%08x: GGMS != 3, unexpected GSM size", ggc);
        return false;
    }

    uint32_t gms = (ggc >> 8) & 0xff;
    uint64_t size;
    if (gms <= 0x04)                     size = uint64_t(gms) * 32 * 1024 * 1024;
    else if (gms >= 0xf0 && gms <= 0xfe) size = uint64_t(gms - 0xf0 + 1) * 4 * 1024 * 1024;
    else { setProperty("xe2-stolen", "invalid-gms"); return false; }
    setNumber("xe2-stolen-dsm-total", size, 64);

    uint64_t sr  = rd64(xe2::STOLEN_RESERVED);
    uint64_t val = (sr >> 7) & 0x7;
    if (val == 5 || val == 6) val--;
    if (val > 3) { setProperty("xe2-stolen", "unknown-wopcm-size"); return false; }
    stolen_.wopcm = uint32_t((1u << val) * 1024 * 1024);
    if (size <= stolen_.wopcm) { setProperty("xe2-stolen", "dsm-smaller-than-wopcm"); return false; }
    size -= stolen_.wopcm;

    stolen_.ioBase = stolen_.bar2Phys + xe2::kStolenBaseInBar2;

    // Wa_14019821291 (media 13.00 - 20.00): BIOS may place GSC PSMI at the top of DSM
    if (mediaIp_ >= 1300 && mediaIp_ <= 2000) {
        uint64_t psmi = rd64(xe2::MEDIA_GT_GSI_OFFSET + xe2::GSCPSMI_BASE) & ~0x3full;
        setNumber("xe2-gscpsmi-base", psmi, 64);
        if (psmi >= stolen_.ioBase && psmi < stolen_.ioBase + size) {
            LOG("reserving %llu bytes of DSM for Wa_14019821291",
                (unsigned long long)(stolen_.ioBase + size - psmi));
            size = psmi - stolen_.ioBase;
        }
    }

    if (size + xe2::kStolenBaseInBar2 > stolen_.bar2Len) {
        setProperty("xe2-stolen", "exceeds-bar2");
        return false;
    }

    stolen_.size    = size;
    stolen_.dsmBase = rd64(xe2::DSMBASE) & xe2::BDSM_MASK;
    stolen_.gsmBase = rd64(xe2::GSMBASE);
    stolen_.valid   = true;

    setNumber("xe2-stolen-io-base", stolen_.ioBase, 64);
    setNumber("xe2-stolen-size", stolen_.size, 64);
    setNumber("xe2-wopcm-size", stolen_.wopcm);
    setNumber("xe2-dsmbase", stolen_.dsmBase, 64);
    setNumber("xe2-gsmbase", stolen_.gsmBase, 64);
    setProperty("xe2-stolen", "ok");
    LOG("stolen: %llu MiB usable at CPU 0x%llx (DSM 0x%llx, WOPCM %u MiB)",
        (unsigned long long)(size >> 20), (unsigned long long)stolen_.ioBase,
        (unsigned long long)stolen_.dsmBase, stolen_.wopcm >> 20);
    return true;
}

// --------------------------------------------------------------------------------------------
// GGTT: xe_ggtt_init_early() for graphics >= 12.50, non-VF
bool ClawXe2::ggttInit() {
    if (bar0Len_ < xe2::kMinBar0LenGsm) {
        setProperty("xe2-ggtt", "bar0-too-small-for-gsm");
        LOG("BAR0 0x%llx < 16 MiB, GSM not reachable", (unsigned long long)bar0Len_);
        return false;
    }
    gsm_ = reinterpret_cast<volatile uint64_t *>(mmio_ + xe2::kGsmOffset);
    ggttStart_ = xe2::kWopcmSizeLnl;
    ggttSize_  = (uint64_t(xe2::kGsmSize) / 8) * xe2::kPageSize - ggttStart_;
    if (ggttStart_ + ggttSize_ > xe2::GUC_GGTT_TOP) ggttSize_ = xe2::GUC_GGTT_TOP - ggttStart_;

    setNumber("xe2-ggtt-start", ggttStart_, 64);
    setNumber("xe2-ggtt-size", ggttSize_, 64);
    setProperty("xe2-ggtt", "ok");
    return true;
}

uint64_t ClawXe2::ggttGetPte(uint64_t addr) const {
    return gsm_[addr >> 12];
}

void ClawXe2::ggttSetPte(uint64_t addr, uint64_t pte) {
    gsm_[addr >> 12] = pte;
    // Wa_22019338487 (media 20.00): dummy write to RO media GMD_ID drains prior GTT writes
    if (mediaIp_ == 2000 && (++ggttWrites_ % xe2::kWa22019338487Every) == 0)
        wr(xe2::MEDIA_GT_GSI_OFFSET + xe2::GMD_ID, 0);
}

uint64_t ClawXe2::ggttEncode(uint64_t phys, unsigned pat, bool dm) const {
    uint64_t pte = xe2::PTE_PRESENT | (phys & xe2::PTE_ADDR_MASK);
    if (pat & 1) pte |= xe2::PTE_PAT0;
    if (pat & 2) pte |= xe2::PTE_PAT1;
    if (dm)      pte |= xe2::PTE_DM;
    return pte;
}

bool ClawXe2::ggttResolve(uint64_t addr, uint64_t *cpuPhys, bool *dm) const {
    uint64_t pte = ggttGetPte(addr & ~(xe2::kPageSize - 1));
    if (!(pte & xe2::PTE_PRESENT)) return false;
    bool isDm = pte & xe2::PTE_DM;
    uint64_t a = pte & xe2::PTE_ADDR_MASK;
    // Graphics >= 12.70: DM PTEs hold the offset from GSMBASE, i.e. an offset into BAR2
    if (isDm) {
        if (!stolen_.bar2Phys) return false;
        a += stolen_.bar2Phys;
    }
    *cpuPhys = a + (addr & (xe2::kPageSize - 1));
    if (dm) *dm = isDm;
    return true;
}

// --------------------------------------------------------------------------------------------
// Decode the firmware scanout: PLANE_SURF (GGTT address) -> PTE -> CPU physical
void ClawXe2::scanoutDecode() {
    static const char kPipe[] = { 'A', 'B', 'C' };
    bool found = false;

    for (unsigned p = 0; p < xe2::kNumPipes; p++) {
        uint32_t o   = p * xe2::kPipeStride;
        uint32_t ctl = rd(xe2::PLANE_CTL_1_A + o);
        if (!(ctl & xe2::PLANE_CTL_ENABLE)) continue;

        uint32_t surf   = rd(xe2::PLANE_SURF_1_A + o) & xe2::PLANE_SURF_ADDR_MASK;
        uint32_t sz     = rd(xe2::PLANE_SIZE_1_A + o);
        uint32_t w      = (sz & 0x1fff) + 1;
        uint32_t h      = ((sz >> 16) & 0x1fff) + 1;
        uint32_t tiling = xe2::PLANE_CTL_TILED(ctl);
        uint32_t fmt    = xe2::PLANE_CTL_FMT_ICL(ctl);
        uint32_t stride = (rd(xe2::PLANE_STRIDE_1_A + o) & 0xfff) * 64;  // linear: 64-byte units

        uint64_t phys0 = 0; bool dm = false;
        bool mapped = ggttResolve(surf, &phys0, &dm);

        uint64_t bytes = uint64_t(stride) * h;
        uint32_t pages = uint32_t((bytes + xe2::kPageSize - 1) / xe2::kPageSize);
        uint32_t contiguous = 0;
        if (mapped) {
            for (uint32_t i = 0; i < pages; i++) {
                uint64_t ph; bool d;
                if (!ggttResolve(surf + uint64_t(i) * xe2::kPageSize, &ph, &d)) break;
                if (ph != phys0 + uint64_t(i) * xe2::kPageSize || d != dm) break;
                contiguous++;
            }
        }
        bool inStolen = dm && stolen_.valid && phys0 >= stolen_.ioBase &&
                        phys0 + bytes <= stolen_.ioBase + stolen_.size + stolen_.wopcm;

        LOG("pipe %c plane1: %ux%u stride %u fmt %u tiling %u GGTT 0x%08x -> %s 0x%llx, "
            "%u/%u pages contiguous%s", kPipe[p], w, h, stride, fmt, tiling, surf,
            mapped ? (dm ? "stolen" : "sysmem") : "UNMAPPED", (unsigned long long)phys0,
            contiguous, pages, inStolen ? ", inside DSM" : "");

        if (found) continue;   // publish the first active pipe only
        found = true;
        char pipeName[2] = { kPipe[p], 0 };
        setProperty("xe2-scanout-pipe", pipeName);
        setNumber("xe2-scanout-width", w);
        setNumber("xe2-scanout-height", h);
        setNumber("xe2-scanout-stride", stride);
        setNumber("xe2-scanout-format", fmt, 8);
        setNumber("xe2-scanout-tiling", tiling, 8);
        setNumber("xe2-scanout-ggtt", surf);
        setNumber("xe2-scanout-pte", ggttGetPte(surf), 64);
        setNumber("xe2-scanout-phys", phys0, 64);
        setProperty("xe2-scanout-memory", mapped ? (dm ? "stolen" : "sysmem") : "unmapped");
        setNumber("xe2-scanout-pages", pages);
        setNumber("xe2-scanout-pages-contiguous", contiguous);
        setProperty("xe2-scanout-in-dsm", inStolen);

        bool ok = mapped && contiguous == pages;
        setProperty("xe2-ggtt-decode", ok ? "ok" : "incomplete");

        if (bootArgIs1("clawxe2fb")) {
            if (ok && tiling == 0 && fmt == xe2::kFmtXrgb8888Icl)
                setProperty("xe2-fb-pattern",
                            drawTestPattern(p, phys0, w, h, stride) ? "drawn" : "map-failed");
            else
                setProperty("xe2-fb-pattern", "skipped (needs linear XRGB8888, contiguous)");
        }
    }
    if (!found) setProperty("xe2-scanout-pipe", "none");
}

bool ClawXe2::drawTestPattern(unsigned pipe, uint64_t fbPhys, uint32_t w, uint32_t h, uint32_t stride) {
    const uint32_t side = 256 < w && 256 < h ? 256 : (w < h ? w : h);
    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
        fbPhys, IOByteCount(stride) * side, kIODirectionInOut);
    if (!md) return false;
    IOMemoryMap *map = md->createMappingInTask(kernel_task, 0, kIOMapAnywhere | kIOMapWriteCombineCache);
    md->release();
    if (!map) return false;

    auto *base = reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());
    for (uint32_t y = 0; y < side; y++) {
        auto *row = reinterpret_cast<volatile uint32_t *>(base + IOByteCount(y) * stride);
        for (uint32_t x = 0; x < side; x++) {
            bool border = x < 4 || y < 4 || x >= side - 4 || y >= side - 4;
            bool cell   = ((x >> 5) ^ (y >> 5)) & 1;
            row[x] = border ? 0x00FFFFFF : (cell ? 0x00FF00FF : 0x0000FF00);  // white / magenta / green
        }
    }
    map->release();
    LOG("pipe %c: drew %ux%u test pattern at 0x%llx", "ABC"[pipe], side, side, (unsigned long long)fbPhys);
    return true;
}

// --------------------------------------------------------------------------------------------
// GGTT self-test: map one uncached system page at a free GGTT slot and read the PTE back.
// Only proves the CPU->GSM write path; GPU-side translation is verified in M3 (GuC reads GGTT).
void ClawXe2::ggttSelfTest() {
    testPage_ = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
        xe2::kPageSize, 0x00003ffffffff000ull);   // 46-bit DMA mask (lnl_desc.dma_mask_size)
    if (!testPage_ || testPage_->prepare() != kIOReturnSuccess) {
        OSSafeReleaseNULL(testPage_);
        setProperty("xe2-ggtt-selftest", "alloc-failed");
        return;
    }
    auto *words = reinterpret_cast<uint32_t *>(testPage_->getBytesNoCopy());
    for (unsigned i = 0; i < xe2::kPageSize / 4; i++) words[i] = 0xC1A70000u | i;
    uint64_t phys = testPage_->getPhysicalSegment(0, nullptr, kIOMemoryMapperNone);

    // Search downward from GUC_GGTT_TOP for a non-present PTE
    uint64_t slot = 0, orig = 0;
    for (uint64_t a = xe2::GUC_GGTT_TOP - xe2::kPageSize, n = 0;
         a >= ggttStart_ && n < 8192; a -= xe2::kPageSize, n++) {
        uint64_t pte = ggttGetPte(a);
        if (!(pte & xe2::PTE_PRESENT)) { slot = a; orig = pte; break; }
    }
    if (!slot) {
        setProperty("xe2-ggtt-selftest", "no-free-slot");
        ggttSelfTestCleanup();
        return;
    }

    uint64_t want = ggttEncode(phys, xe2::kPatUncachedLnl, false);
    ggttSetPte(slot, want);
    if (mediaIp_ == 2000) wr(xe2::MEDIA_GT_GSI_OFFSET + xe2::GMD_ID, 0);  // force drain
    (void)rd(xe2::GMD_ID);                                                // posting read
    uint64_t got = ggttGetPte(slot);

    uint64_t back = 0; bool dm = true;
    bool resolved = ggttResolve(slot, &back, &dm);

    setNumber("xe2-ggtt-selftest-slot", slot, 64);
    setNumber("xe2-ggtt-selftest-pte", got, 64);
    setNumber("xe2-ggtt-selftest-page-phys", phys, 64);
    bool ok = got == want && resolved && !dm && back == phys;
    setProperty("xe2-ggtt-selftest", ok ? "pte-readback-ok" : "pte-mismatch");
    LOG("GGTT self-test: slot 0x%llx wrote 0x%llx read 0x%llx -> %s",
        (unsigned long long)slot, (unsigned long long)want, (unsigned long long)got,
        ok ? "OK" : "MISMATCH");

    // Restore the original (non-present) PTE right away: nothing should use this mapping
    // until the GuC TLB invalidation path exists (M3).
    ggttSetPte(slot, orig);
    if (mediaIp_ == 2000) wr(xe2::MEDIA_GT_GSI_OFFSET + xe2::GMD_ID, 0);
    ggttSelfTestCleanup();
}

void ClawXe2::ggttSelfTestCleanup() {
    if (testPage_) {
        testPage_->complete();
        OSSafeReleaseNULL(testPage_);
    }
}
