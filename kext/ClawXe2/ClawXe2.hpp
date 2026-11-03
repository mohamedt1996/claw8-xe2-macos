#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>

class ClawXe2 : public IOService {
    OSDeclareDefaultStructors(ClawXe2)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    // ---- M1: device, MMIO, identification ----
    IOPCIDevice  *pci_  = nullptr;
    IOMemoryMap  *bar0_ = nullptr;
    volatile uint8_t *mmio_ = nullptr;
    uint64_t      bar0Len_ = 0;
    bool          fwHeld_ = false;
    uint32_t      graphicsIp_ = 0;
    uint32_t      mediaIp_ = 0;

    uint32_t rd(uint32_t off) const;
    void     wr(uint32_t off, uint32_t val);
    uint64_t rd64(uint32_t off) const;   // two 32-bit reads (lo, hi), as xe_mmio_read64_2x32

    bool mapBar0();
    void readIpVersions();
    bool forcewake(bool wake);
    bool waitAck(uint32_t ackReg, bool set);
    void readTopology();
    void setNumber(const char *key, uint64_t v, unsigned bits = 32);

    // ---- M2: stolen memory + GGTT ----
    struct Stolen {
        bool     valid = false;
        uint64_t bar2Phys = 0, bar2Len = 0;
        uint64_t ioBase = 0;      // CPU physical address of DSM (BAR2 + 8 MiB)
        uint64_t size = 0;        // usable, after WOPCM / GSC carve-outs
        uint64_t dsmBase = 0, gsmBase = 0;
        uint32_t wopcm = 0;
    } stolen_;

    volatile uint64_t *gsm_ = nullptr;   // GGTT PTE array (BAR0 + 8 MiB)
    uint64_t ggttStart_ = 0, ggttSize_ = 0;
    unsigned ggttWrites_ = 0;
    IOBufferMemoryDescriptor *testPage_ = nullptr;

    void     m2Run();
    bool     probeStolen();
    bool     ggttInit();
    uint64_t ggttGetPte(uint64_t ggttAddr) const;
    void     ggttSetPte(uint64_t ggttAddr, uint64_t pte);
    uint64_t ggttEncode(uint64_t phys, unsigned pat, bool deviceMem) const;
    bool     ggttResolve(uint64_t ggttAddr, uint64_t *cpuPhys, bool *dm) const;
    void     scanoutDecode();
    bool     drawTestPattern(unsigned pipe, uint64_t fbPhys, uint32_t w, uint32_t h, uint32_t strideBytes);
    void     ggttSelfTest();
    void     ggttSelfTestCleanup();

    // ---- M3: GuC firmware load, hwconfig, CT ----
    struct GpuBuf {
        IOBufferMemoryDescriptor *md = nullptr;
        uint8_t  *cpu  = nullptr;
        uint32_t  size = 0;        // page aligned
        uint64_t  ggtt = 0;        // GGTT address, 0 if not mapped
    };
    GpuBuf   fwBuf_, adsBuf_, logBuf_, h2gBuf_, g2hBuf_, hwcfgBuf_;
    uint64_t ggttCursor_ = 0;      // next free-window search starts below this address
    bool     gucRunning_ = false;
    bool     ctEnabled_  = false;
    uint32_t gucUcodeSize_ = 0, gucRsaSize_ = 0, gucPrivDataSize_ = 0;
    uint32_t wopcmBase_ = 0, wopcmSize_ = 0;

    void     m3Run();
    bool     bufAlloc(GpuBuf &b, uint32_t size, const char *what);
    void     bufFree(GpuBuf &b);
    bool     mcrLock();
    void     mcrUnlock();
    void     patProgram();
    bool     gucParseFw(const uint8_t *fw, uint32_t len);
    bool     wopcmInit();
    bool     gucReset();
    void     adsPopulateMinimal();
    bool     gucUpload();
    bool     gucWaitReady(uint32_t *status, uint32_t *ms);
    bool     waitReg(uint32_t reg, uint32_t mask, uint32_t want, uint32_t timeoutUs, uint32_t *last = nullptr);
    int64_t  gucMmioSend(const uint32_t *req, unsigned len, uint32_t *resp = nullptr);
    bool     gucSelfCfg(uint16_t key, uint16_t len, uint64_t val);
    bool     gucHwconfig();
    bool     gucCtEnable();
    void     gucTeardown();
};
