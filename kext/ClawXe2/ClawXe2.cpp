// ClawXe2 — milestone 1 of a native Xe2 (Lunar Lake / Arc 140V) driver for macOS.
//
// Default behaviour is READ-ONLY: match the GPU, map BAR0, read GMD_ID (graphics + media IP),
// publish results to the IORegistry. Writes happen only with boot-arg `clawxe2fw=1`
// (forcewake GT+render, read topology fuses, release forcewake).
//
// Boot-args:  -clawxe2off   disable entirely
//             -clawxe2dbg   verbose IOLog
//             clawxe2fw=1   enable forcewake + topology probe
//
// SPDX-License-Identifier: GPL-2.0-only  (logic derived from Linux drivers/gpu/drm/xe)

#include "ClawXe2.hpp"
#include "Xe2Regs.h"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <pexpert/pexpert.h>

#define super IOService
OSDefineMetaClassAndStructors(ClawXe2, IOService)

static bool gDebug = false;
#define LOG(fmt, ...)  IOLog("ClawXe2: " fmt "\n", ##__VA_ARGS__)
#define DBG(fmt, ...)  do { if (gDebug) LOG(fmt, ##__VA_ARGS__); } while (0)

static bool hasBootFlag(const char *flag) {
    char tmp[2];
    return PE_parse_boot_argn(flag, tmp, sizeof(tmp));
}

uint32_t ClawXe2::rd(uint32_t off) const { return OSReadLittleInt32(mmio_, off); }
void     ClawXe2::wr(uint32_t off, uint32_t v) { OSWriteLittleInt32(mmio_, off, v); }

void ClawXe2::setNumber(const char *key, uint64_t v, unsigned bits) {
    setProperty(key, v, bits);
}

IOService *ClawXe2::probe(IOService *provider, SInt32 *score) {
    if (hasBootFlag("-clawxe2off")) return nullptr;
    auto *dev = OSDynamicCast(IOPCIDevice, provider);
    if (!dev) return nullptr;

    uint16_t ven = dev->configRead16(kIOPCIConfigVendorID);
    uint16_t did = dev->configRead16(kIOPCIConfigDeviceID);
    if (ven != xe2::kVendorIntel) return nullptr;
    for (uint16_t id : xe2::kLnlIds)
        if (id == did) return super::probe(provider, score);
    return nullptr;
}

bool ClawXe2::start(IOService *provider) {
    gDebug = hasBootFlag("-clawxe2dbg");
    if (!super::start(provider)) return false;

    pci_ = OSDynamicCast(IOPCIDevice, provider);
    if (!pci_) return false;
    pci_->retain();

    uint16_t did = pci_->configRead16(kIOPCIConfigDeviceID);
    uint8_t  rev = pci_->configRead8(kIOPCIConfigRevisionID);
    LOG("Lunar Lake GPU 8086:%04x rev %02x", did, rev);
    setNumber("xe2-device-id", did, 16);
    setNumber("xe2-pci-revision", rev, 8);
    setProperty("xe2-milestone", "M1-probe");

    if (!mapBar0()) {
        setProperty("xe2-status", "bar0-map-failed");
        return true;   // stay attached so the failure is visible in ioreg
    }

    readIpVersions();

    uint32_t fwArg = 0;
    if (PE_parse_boot_argn("clawxe2fw", &fwArg, sizeof(fwArg)) && fwArg == 1) {
        if (forcewake(true)) {
            setProperty("xe2-forcewake", "ok");
            readTopology();
            forcewake(false);
        } else {
            setProperty("xe2-forcewake", "ack-timeout");
            forcewake(false);
        }
    } else {
        setProperty("xe2-forcewake", "skipped (set clawxe2fw=1)");
    }

    registerService();
    LOG("probe complete — see: ioreg -l -w0 -c ClawXe2");
    return true;
}

void ClawXe2::stop(IOService *provider) {
    if (fwHeld_) forcewake(false);
    OSSafeReleaseNULL(bar0_);
    mmio_ = nullptr;
    OSSafeReleaseNULL(pci_);
    super::stop(provider);
}

bool ClawXe2::mapBar0() {
    pci_->setMemoryEnable(true);
    bar0_ = pci_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!bar0_) { LOG("cannot map BAR0"); return false; }

    bar0Len_ = bar0_->getLength();
    mmio_ = reinterpret_cast<volatile uint8_t *>(bar0_->getVirtualAddress());
    setNumber("xe2-bar0-length", bar0Len_, 64);
    setNumber("xe2-bar0-phys", bar0_->getPhysicalAddress(), 64);
    DBG("BAR0 phys 0x%llx len 0x%llx", (unsigned long long)bar0_->getPhysicalAddress(),
        (unsigned long long)bar0Len_);

    if (bar0Len_ < xe2::kMinBar0Len) {
        LOG("BAR0 too small (0x%llx)", (unsigned long long)bar0Len_);
        OSSafeReleaseNULL(bar0_);
        mmio_ = nullptr;
        return false;
    }
    return true;
}

void ClawXe2::readIpVersions() {
    uint32_t g = rd(xe2::GMD_ID);
    uint32_t m = rd(xe2::MEDIA_GT_GSI_OFFSET + xe2::GMD_ID);

    if (g == 0xffffffff) {   // device not responding / powered down
        setProperty("xe2-status", "mmio-reads-ffffffff");
        LOG("GMD_ID reads 0xffffffff — GPU not responding");
        return;
    }

    uint32_t gv = xe2::GMD_ARCH(g) * 100 + xe2::GMD_RELEASE(g);
    uint32_t mv = xe2::GMD_ARCH(m) * 100 + xe2::GMD_RELEASE(m);
    setNumber("xe2-gmdid-graphics-raw", g);
    setNumber("xe2-gmdid-media-raw", m);
    setNumber("xe2-graphics-ip", gv);
    setNumber("xe2-graphics-revid", xe2::GMD_REVID(g), 8);
    setNumber("xe2-media-ip", mv);

    bool ok = (gv == xe2::kGraphicsXe2Lpg);
    setProperty("xe2-status", ok ? "xe2-lpg-detected" : "unexpected-graphics-ip");
    LOG("graphics IP %u.%02u (rev %u), media IP %u.%02u — %s",
        gv / 100, gv % 100, xe2::GMD_REVID(g), mv / 100, mv % 100,
        ok ? "Xe2_LPG confirmed" : "NOT Xe2_LPG");
}

bool ClawXe2::waitAck(uint32_t ackReg, bool set) {
    for (uint32_t us = 0; us < xe2::kForcewakeTimeoutMs * 1000; us += 10) {
        bool bit = rd(ackReg) & xe2::FORCEWAKE_KERNEL_BIT;
        if (bit == set) return true;
        IODelay(10);
    }
    return false;
}

bool ClawXe2::forcewake(bool wake) {
    uint32_t v = xe2::FORCEWAKE_KERNEL_MASK | (wake ? xe2::FORCEWAKE_KERNEL_BIT : 0);
    wr(xe2::FORCEWAKE_GT, v);
    wr(xe2::FORCEWAKE_RENDER, v);
    bool gt = waitAck(xe2::FORCEWAKE_ACK_GT_MTL, wake);
    bool rc = waitAck(xe2::FORCEWAKE_ACK_RENDER, wake);
    fwHeld_ = wake && gt && rc;
    DBG("forcewake %s: gt=%d render=%d", wake ? "get" : "put", gt, rc);
    return gt && rc;
}

void ClawXe2::readTopology() {
    unsigned geo = 0, cmp = 0;
    for (uint32_t r : xe2::kGeometryDss) geo += __builtin_popcount(rd(r));
    for (uint32_t r : xe2::kComputeDss)  cmp += __builtin_popcount(rd(r));
    setNumber("xe2-xe-cores-geometry", geo, 8);
    setNumber("xe2-xe-cores-compute", cmp, 8);
    // Arc 140V = 8 Xe-cores (Claw 8 AI+ / 258V), Arc 130V = 7.
    const char *sku = geo == 8 ? "Arc 140V" : geo == 7 ? "Arc 130V" : "unknown";
    setProperty("xe2-sku", sku);
    LOG("topology: %u geometry / %u compute Xe-cores (%s)", geo, cmp, sku);
}
