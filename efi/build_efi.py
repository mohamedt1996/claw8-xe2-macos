#!/usr/bin/env python3
"""Assemble an OpenCore EFI folder for the MSI Claw 8 AI+ (Lunar Lake, Core Ultra 7 258V).

usage:
  build_efi.py --opencore <unzipped OpenCore-*-RELEASE dir>
               --kexts <dir containing *.kext (searched recursively, PlugIns skipped)>
               --ssdts <dir containing *.aml>
               --out <output dir, receives EFI/>

The config is derived from OpenCore's Docs/Sample.plist with the settings from docs/OPENCORE.md.
SMBIOS serials are placeholders: the Windows USB maker (or the user) fills them with macserial.
Nothing here has been booted on a Claw yet - see README.
"""
import argparse
import os
import plistlib
import shutil
import sys

# Kexts in the EFI, by bundle name. Order is resolved by dependencies, not this list.
WANTED_KEXTS = [
    "Lilu.kext",
    "VirtualSMC.kext",
    "SMCBatteryManager.kext",
    "ECEnabler.kext",
    "CpuTopologyRebuild.kext",
    "RestrictEvents.kext",
    "NVMeFix.kext",
    "USBToolBox.kext",
    "VoodooI2C.kext",
    "VoodooI2CHID.kext",
    "ClawXe2.kext",
]
OPTIONAL_KEXTS = {"USBToolBox.kext", "VoodooI2C.kext", "VoodooI2CHID.kext", "ECEnabler.kext"}

SSDTS = ["SSDT-EC-USBX-LAPTOP.aml", "SSDT-AWAC.aml", "SSDT-PNLF.aml"]

DRIVERS = ["OpenRuntime.efi", "OpenHfsPlus.efi", "ResetNvramEntry.efi"]

BOOT_ARGS = "-v keepsyms=1 debug=0x100 -lilubetaall -clawxe2dbg"

# Comet Lake CPUID spoof (Kernel -> Emulate), as used for Alder Lake and newer hybrid laptops
CPUID1_DATA = bytes.fromhex("55060A00" "00000000" "00000000" "00000000")
CPUID1_MASK = bytes.fromhex("FFFFFFFF" "00000000" "00000000" "00000000")


def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def find_kexts(root: str) -> dict:
    """Map bundle name -> path for top-level kexts anywhere under root (ignores nested PlugIns)."""
    found = {}
    for dirpath, dirnames, _ in os.walk(root):
        for d in list(dirnames):
            if d.endswith(".kext"):
                if "PlugIns" in dirpath.split(os.sep):
                    continue
                found.setdefault(d, os.path.join(dirpath, d))
                dirnames.remove(d)       # don't descend; plugins handled separately
            elif d.endswith(".dSYM") or d.startswith("__MACOSX"):
                dirnames.remove(d)
    return found


def kext_info(path: str) -> dict:
    with open(os.path.join(path, "Contents", "Info.plist"), "rb") as f:
        return plistlib.load(f)


def kext_entries(name: str, dest_root: str) -> list:
    """Kernel->Add entries for a kext and its PlugIns, with identifiers and dependencies."""
    out = []

    def add(rel: str):
        full = os.path.join(dest_root, rel)
        info = kext_info(full)
        exe = info.get("CFBundleExecutable", "")
        exe_rel = f"Contents/MacOS/{exe}" if exe and os.path.exists(
            os.path.join(full, "Contents", "MacOS", exe)) else ""
        out.append({
            "id": info.get("CFBundleIdentifier", rel),
            "deps": set(info.get("OSBundleLibraries", {}).keys()),
            "entry": {
                "Arch": "x86_64", "BundlePath": rel, "Comment": os.path.basename(rel),
                "Enabled": True, "ExecutablePath": exe_rel, "MaxKernel": "", "MinKernel": "",
                "PlistPath": "Contents/Info.plist",
            },
        })
        plugins = os.path.join(full, "Contents", "PlugIns")
        if os.path.isdir(plugins):
            for p in sorted(os.listdir(plugins)):
                if p.endswith(".kext"):
                    add(f"{rel}/Contents/PlugIns/{p}")

    add(name)
    return out


def order_by_dependencies(items: list) -> list:
    """Stable topological sort: a kext comes after every kext it depends on."""
    ids = {i["id"] for i in items}
    done, result = set(), []
    pending = list(items)
    while pending:
        progressed = False
        for it in list(pending):
            if all(d in done or d not in ids for d in it["deps"]):
                result.append(it["entry"])
                done.add(it["id"])
                pending.remove(it)
                progressed = True
        if not progressed:
            die("circular kext dependencies: " + ", ".join(i["id"] for i in pending))
    return result


def build_config(sample: dict, kext_add: list, ssdts: list, drivers: list) -> dict:
    c = sample
    for k in [k for k in c if k.startswith("#")]:
        del c[k]

    # ACPI
    c["ACPI"]["Add"] = [{"Comment": s, "Enabled": True, "Path": s} for s in ssdts]
    c["ACPI"]["Delete"] = []
    c["ACPI"]["Patch"] = []

    # Booter (Alder Lake+ laptop profile)
    q = c["Booter"]["Quirks"]
    q.update({
        "AvoidRuntimeDefrag": True, "DevirtualiseMmio": True, "EnableWriteUnprotector": False,
        "ProtectUefiServices": True, "ProvideCustomSlide": True, "RebuildAppleMemoryMap": True,
        "SetupVirtualMap": True, "SyncRuntimePermissions": True, "ResizeAppleGpuBars": -1,
    })
    c["Booter"]["Patch"] = []
    c["Booter"]["MmioWhitelist"] = []

    # No device properties: ClawXe2 owns the iGPU, nothing is spoofed for Apple's drivers
    c["DeviceProperties"]["Add"] = {}
    c["DeviceProperties"]["Delete"] = {}

    # Kernel
    k = c["Kernel"]
    k["Add"] = kext_add
    k["Block"] = []
    k["Force"] = []
    k["Patch"] = []
    k["Emulate"].update({"Cpuid1Data": CPUID1_DATA, "Cpuid1Mask": CPUID1_MASK})
    k["Quirks"].update({
        "AppleCpuPmCfgLock": False, "AppleXcpmCfgLock": True, "DisableIoMapper": True,
        "PanicNoKextDump": True, "PowerTimeoutKernelPanic": True, "ProvideCurrentCpuInfo": True,
        "XhciPortLimit": False,
    })
    k["Scheme"].update({"KernelArch": "x86_64", "KernelCache": "Auto", "FuzzyMatch": True})

    # Misc
    m = c["Misc"]
    m["Boot"].update({"HideAuxiliary": False, "ShowPicker": True, "Timeout": 10, "PickerMode": "Builtin"})
    m["Debug"].update({"AppleDebug": True, "ApplePanic": True, "DisableWatchDog": True,
                       "Target": 67, "DisplayLevel": 2147483714})   # log to EFI file + console
    m["Security"].update({"AllowSetDefault": True, "BlacklistAppleUpdate": True,
                          "ScanPolicy": 0, "SecureBootModel": "Disabled", "Vault": "Optional",
                          "DmgLoading": "Signed"})
    m["Entries"] = []
    m["BlessOverride"] = []
    m["Tools"] = [{
        "Arguments": "", "Auxiliary": False, "Comment": "UEFI shell", "Enabled": True,
        "Flavour": "OpenShell:UEFIShell:Shell", "FullNvramAccess": False, "Name": "UEFI Shell",
        "Path": "OpenShell.efi", "RealPath": False, "TextMode": False,
    }]

    # NVRAM: boot-args is NOT in Delete, so `sudo nvram boot-args=...` from macOS persists
    # (tools/clawxe2-stage.sh). Reset NVRAM from the picker to return to the config default.
    apple = "7C436110-AB2A-4BBB-A880-FE41995C9F82"
    add = c["NVRAM"]["Add"][apple]
    for key in [x for x in add if x.startswith("#")]:
        del add[key]
    add["boot-args"] = BOOT_ARGS
    add["csr-active-config"] = bytes(4)
    add["prev-lang:kbd"] = b"en-US:0"
    c["NVRAM"]["Delete"][apple] = ["ForceDisplayRotationInEFI"]
    c["NVRAM"]["WriteFlash"] = True

    # PlatformInfo: placeholders, filled by windows/Make-ClawMacUSB.ps1 via macserial
    g = c["PlatformInfo"]["Generic"]
    g.update({"SystemProductName": "MacBookPro16,2", "SystemSerialNumber": "FILL_ME_SERIAL",
              "MLB": "FILL_ME_MLB", "SystemUUID": "00000000-0000-0000-0000-000000000000",
              "ROM": bytes(6)})

    # UEFI
    c["UEFI"]["Drivers"] = [{"Arguments": "", "Comment": d, "Enabled": True, "HideVerbose": False,
                             "LoadEarly": False, "Path": d} for d in drivers]
    c["UEFI"]["Quirks"].update({"ReleaseUsbOwnership": True, "RequestBootVarRouting": True,
                                "UnblockFsConnect": False})
    c["UEFI"]["Output"].update({"ProvideConsoleGop": True, "Resolution": "Max"})
    c["UEFI"]["ReservedMemory"] = []
    return c


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--opencore", required=True)
    ap.add_argument("--kexts", required=True)
    ap.add_argument("--ssdts", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    oc_x64 = os.path.join(a.opencore, "X64", "EFI")
    sample = os.path.join(a.opencore, "Docs", "Sample.plist")
    for p in (oc_x64, sample):
        if not os.path.exists(p):
            die(f"{p} missing - pass the unzipped OpenCore RELEASE folder")

    efi = os.path.join(a.out, "EFI")
    if os.path.exists(efi):
        shutil.rmtree(efi)
    shutil.copytree(oc_x64, efi)
    oc = os.path.join(efi, "OC")

    # Drivers: keep only the ones we enable
    drv_dir = os.path.join(oc, "Drivers")
    for f in os.listdir(drv_dir):
        if f not in DRIVERS:
            os.remove(os.path.join(drv_dir, f))
    missing = [d for d in DRIVERS if not os.path.exists(os.path.join(drv_dir, d))]
    if missing:
        die("OpenCore drivers missing: " + ", ".join(missing))

    # Tools: keep only OpenShell
    tools_dir = os.path.join(oc, "Tools")
    for f in os.listdir(tools_dir):
        if f != "OpenShell.efi":
            os.remove(os.path.join(tools_dir, f))

    # SSDTs
    acpi_dir = os.path.join(oc, "ACPI")
    os.makedirs(acpi_dir, exist_ok=True)
    ssdts = []
    for s in SSDTS:
        src = os.path.join(a.ssdts, s)
        if os.path.exists(src):
            shutil.copy2(src, acpi_dir)
            ssdts.append(s)
        else:
            print(f"warning: {s} not found, skipped")

    # Kexts
    available = find_kexts(a.kexts)
    kext_dir = os.path.join(oc, "Kexts")
    os.makedirs(kext_dir, exist_ok=True)
    items = []
    for name in WANTED_KEXTS:
        if name not in available:
            if name in OPTIONAL_KEXTS:
                print(f"warning: optional {name} not found, skipped")
                continue
            die(f"required {name} not found under {a.kexts}")
        shutil.copytree(available[name], os.path.join(kext_dir, name), symlinks=True)
        items += kext_entries(name, kext_dir)
    kext_add = order_by_dependencies(items)

    with open(sample, "rb") as f:
        cfg = build_config(plistlib.load(f), kext_add, ssdts, DRIVERS)
    with open(os.path.join(oc, "config.plist"), "wb") as f:
        plistlib.dump(cfg, f, sort_keys=False)

    print("EFI assembled:", efi)
    for e in kext_add:
        print("  kext", e["BundlePath"])
    for s in ssdts:
        print("  ssdt", s)


if __name__ == "__main__":
    main()
