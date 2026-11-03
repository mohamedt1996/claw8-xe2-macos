#pragma once
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>

class ClawXe2 : public IOService {
    OSDeclareDefaultStructors(ClawXe2)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    IOPCIDevice  *pci_  = nullptr;
    IOMemoryMap  *bar0_ = nullptr;
    volatile uint8_t *mmio_ = nullptr;
    uint64_t      bar0Len_ = 0;
    bool          fwHeld_ = false;

    uint32_t rd(uint32_t off) const;
    void     wr(uint32_t off, uint32_t val);

    bool mapBar0();
    void readIpVersions();
    bool forcewake(bool wake);
    bool waitAck(uint32_t ackReg, bool set);
    void readTopology();
    void setNumber(const char *key, uint64_t v, unsigned bits = 32);
};
