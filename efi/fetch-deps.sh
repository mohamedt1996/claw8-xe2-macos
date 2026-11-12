#!/bin/bash
# fetch-deps.sh DL: OpenCore, the kexts and Dortania's compiled SSDTs efi/build_efi.py takes (CI; needs gh + GH_TOKEN).
set -euo pipefail
DL=${1:?usage: fetch-deps.sh <download dir>}
fetch() {  # repo  dest  [pattern]
  mkdir -p "$DL/$2"
  gh release download -R "$1" -D "$DL/$2" -p "${3:-*.zip}" --clobber
  rm -f "$DL"/$2/*DEBUG*.zip
  for z in "$DL"/$2/*.zip; do unzip -q -o "$z" -d "$DL/$2/x"; done
  echo "$1: $(ls "$DL"/$2/*.zip | xargs -n1 basename)"
}
fetch acidanthera/OpenCorePkg      opencore '*RELEASE.zip'
fetch acidanthera/Lilu             kexts/lilu '*RELEASE.zip'
fetch acidanthera/VirtualSMC       kexts/vsmc '*RELEASE.zip'
fetch acidanthera/RestrictEvents   kexts/re   '*RELEASE.zip'
fetch acidanthera/NVMeFix          kexts/nvme '*RELEASE.zip'
fetch 1Revenger1/ECEnabler         kexts/ece
fetch b00t0x/CpuTopologyRebuild    kexts/ctr
fetch USBToolBox/kext              kexts/utb
fetch VoodooI2C/VoodooI2C          kexts/vi2c
git clone -q --depth 1 --filter=blob:none --sparse https://github.com/dortania/Getting-Started-With-ACPI "$DL/acpi"
git -C "$DL/acpi" sparse-checkout set extra-files/compiled
