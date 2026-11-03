ClawMac EFI - OpenCore for the MSI Claw 8 AI+ (Core Ultra 7 258V / Arc 140V)
==========================================================================

EXPERIMENTAL. Nobody has publicly booted macOS on Lunar Lake yet.

Contents
  EFI/BOOT/BOOTx64.efi      OpenCore boot loader
  EFI/OC/config.plist       Claw configuration (CPU spoof, laptop quirks, verbose boot)
  EFI/OC/Kexts/ClawXe2.kext Arc 140V research driver (milestones 1-3, opt-in tests)

Before booting
  1. SMBIOS serials in config.plist are placeholders (FILL_ME_*).
     windows\Make-ClawMacUSB.bat fills them automatically with macserial.
  2. macOS recovery is NOT included (Apple licence). The Windows script
     downloads it from Apple onto the USB stick.

BIOS (hold Vol+ while powering on)
  Secure Boot: Disabled   VT-d: Enabled   DVMT pre-allocated: maximum

Driver test stages (set from macOS Terminal, then reboot):
  sudo tools/clawxe2-stage.sh 1   probe only
  sudo tools/clawxe2-stage.sh 2   + forcewake / Xe-core count
  sudo tools/clawxe2-stage.sh 3   + GGTT checkerboard test
  sudo tools/clawxe2-stage.sh 4   + GuC firmware
  sudo tools/clawxe2-stage.sh off disable ClawXe2
Reset NVRAM from the OpenCore picker to return to the default boot-args.

Source: https://github.com/mohamedt1996/claw8-xe2-macos
