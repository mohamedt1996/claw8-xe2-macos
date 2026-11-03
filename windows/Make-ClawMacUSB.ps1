#Requires -Version 5.1
<#
.SYNOPSIS
  Creates a macOS installer USB for the MSI Claw 8 AI+ (OpenCore + ClawXe2 + macOS Tahoe recovery).

.DESCRIPTION
  1. Downloads ClawMac-EFI.zip (OpenCore EFI built by this repo's CI, release "efi-latest").
  2. Generates unique SMBIOS data with OpenCore's macserial and writes it into config.plist.
  3. Downloads macOS recovery from Apple with OpenCore's macrecovery.py (needs Python 3).
  4. Erases the chosen USB stick (GPT, FAT32) and copies EFI + com.apple.recovery.boot.

  macOS images are downloaded from Apple on your PC; they are never redistributed.

.PARAMETER DiskNumber   USB disk number to erase (skips the menu; still asks to confirm).
.PARAMETER EfiZip       Use a local ClawMac-EFI.zip instead of downloading.
.PARAMETER MacOS        tahoe (default) or sequoia.
.PARAMETER WorkDir      Download/cache folder (default: %USERPROFILE%\ClawMac).
.PARAMETER NoUsb        Prepare everything in WorkDir\usb but don't touch any disk.

.EXAMPLE
  .\Make-ClawMacUSB.ps1
.EXAMPLE
  .\Make-ClawMacUSB.ps1 -NoUsb      # build the USB contents into a folder only
#>
[CmdletBinding()]
param(
    [int]$DiskNumber = -1,
    [string]$EfiZip,
    [ValidateSet('tahoe', 'sequoia')][string]$MacOS = 'tahoe',
    [string]$WorkDir = (Join-Path $env:USERPROFILE 'ClawMac'),
    [switch]$NoUsb
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # makes Invoke-WebRequest much faster on 5.1
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo     = 'mohamedt1996/claw8-xe2-macos'
$EfiUrl   = "https://github.com/$Repo/releases/download/efi-latest/ClawMac-EFI.zip"
$Boards   = @{
    tahoe   = @{ Board = 'Mac-CFF7D910A743CAAF'; Extra = @('-os', 'latest') }
    sequoia = @{ Board = 'Mac-7BA5B2D9E42DDD94'; Extra = @() }
}
$Model    = 'MacBookPro16,2'

function Step([string]$m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Ok([string]$m)   { Write-Host "    [OK] $m" -ForegroundColor Green }
function Warn([string]$m) { Write-Host "    [!]  $m" -ForegroundColor Yellow }

# ---------------------------------------------------------------- admin
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $NoUsb -and -not ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run as Administrator (use Make-ClawMacUSB.bat, it elevates automatically).'
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

# ---------------------------------------------------------------- 1. EFI
Step 'OpenCore EFI'
$efiRoot = Join-Path $WorkDir 'efi'
if (-not $EfiZip) {
    $EfiZip = Join-Path $WorkDir 'ClawMac-EFI.zip'
    Write-Host "    downloading $EfiUrl"
    try { Invoke-WebRequest -Uri $EfiUrl -OutFile $EfiZip -UseBasicParsing }
    catch { throw "Download failed. Check that the GitHub Actions build passed and published release 'efi-latest'. ($($_.Exception.Message))" }
}
if (Test-Path $efiRoot) { Remove-Item $efiRoot -Recurse -Force }
Expand-Archive -Path $EfiZip -DestinationPath $efiRoot -Force
$config = Join-Path $efiRoot 'EFI\OC\config.plist'
if (-not (Test-Path $config)) { throw "config.plist not found in $EfiZip" }
if (Test-Path (Join-Path $efiRoot 'OPENCORE_VERSION.txt')) { Ok (Get-Content (Join-Path $efiRoot 'OPENCORE_VERSION.txt')) }
Ok "EFI extracted to $efiRoot"

# ---------------------------------------------------------------- 2. SMBIOS
Step "SMBIOS ($Model)"
$macserial = Get-ChildItem -Path (Join-Path $efiRoot 'Utilities\macserial') -Filter 'macserial.exe' -Recurse | Select-Object -First 1
if (-not $macserial) { throw 'macserial.exe missing from the EFI zip' }
$line = (& $macserial.FullName -m $Model -g -n 1 | Where-Object { $_ -match '\|' } | Select-Object -First 1)
if (-not $line) { throw 'macserial produced no output' }
$parts  = $line -split '\|' | ForEach-Object { $_.Trim() }
$serial = $parts[1]; $mlb = $parts[2]
$uuid   = [guid]::NewGuid().ToString().ToUpper()
$rom    = New-Object byte[] 6
(New-Object Security.Cryptography.RNGCryptoServiceProvider).GetBytes($rom)

$settings = New-Object Xml.XmlReaderSettings
$settings.DtdProcessing = [Xml.DtdProcessing]::Ignore
$reader = [Xml.XmlReader]::Create($config, $settings)
$xml = New-Object Xml.XmlDocument
$xml.PreserveWhitespace = $true
$xml.Load($reader); $reader.Close()

function Get-DictValue($dict, [string]$key) {
    $k = $dict.SelectSingleNode("key[text()='$key']")
    if (-not $k) { throw "plist key $key not found" }
    $n = $k.NextSibling
    while ($n -and $n.NodeType -ne [Xml.XmlNodeType]::Element) { $n = $n.NextSibling }
    return $n
}
$root    = $xml.SelectSingleNode('/plist/dict')
$generic = Get-DictValue (Get-DictValue $root 'PlatformInfo') 'Generic'
(Get-DictValue $generic 'SystemSerialNumber').InnerText = $serial
(Get-DictValue $generic 'MLB').InnerText               = $mlb
(Get-DictValue $generic 'SystemUUID').InnerText        = $uuid
(Get-DictValue $generic 'ROM').InnerText               = [Convert]::ToBase64String($rom)
(Get-DictValue $generic 'SystemProductName').InnerText = $Model
$utf8 = New-Object Text.UTF8Encoding($false)
$writer = New-Object IO.StreamWriter($config, $false, $utf8)
$xml.Save($writer); $writer.Close()
Ok "serial $serial, MLB $mlb, UUID $uuid"
Set-Content -Path (Join-Path $WorkDir 'smbios.txt') -Value "Model $Model`nSerial $serial`nMLB $mlb`nUUID $uuid`nROM $([BitConverter]::ToString($rom))"

# ---------------------------------------------------------------- 3. Recovery
Step "macOS $MacOS recovery (from Apple)"
$recDir = Join-Path $WorkDir "recovery-$MacOS\com.apple.recovery.boot"
$dmg = Get-ChildItem -Path $recDir -Filter '*.dmg' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dmg) {
    Ok "using cached $($dmg.FullName)"
} else {
    $py = $null
    foreach ($c in @('py', 'python', 'python3')) {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notmatch 'WindowsApps') { $py = $cmd.Source; break }
    }
    if (-not $py) {
        Warn 'Python 3 not found.'
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            $ans = Read-Host '    Install Python 3.12 with winget now? (y/n)'
            if ($ans -eq 'y') {
                winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
                $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
                $py = (Get-Command py -ErrorAction SilentlyContinue).Source
            }
        }
        if (-not $py) { throw 'Install Python 3 from python.org (tick "Add to PATH") and run again.' }
    }
    $macrec = Get-ChildItem -Path (Join-Path $efiRoot 'Utilities\macrecovery') -Filter 'macrecovery.py' -Recurse | Select-Object -First 1
    if (-not $macrec) { throw 'macrecovery.py missing from the EFI zip' }
    New-Item -ItemType Directory -Force -Path $recDir | Out-Null
    $b = $Boards[$MacOS]
    $pyArgs = @($macrec.FullName, '-b', $b.Board, '-m', '00000000000000000') + $b.Extra + @('download', '-o', $recDir)
    if ((Split-Path $py -Leaf) -eq 'py.exe') { $pyArgs = @('-3') + $pyArgs }
    Write-Host "    $py $($pyArgs -join ' ')"
    & $py @pyArgs
    if ($LASTEXITCODE -ne 0) { throw "macrecovery failed ($LASTEXITCODE)" }
    $dmg = Get-ChildItem -Path $recDir -Filter '*.dmg' | Select-Object -First 1
    if (-not $dmg) { throw 'recovery image not downloaded' }
    Ok "$($dmg.Name) ($([math]::Round($dmg.Length / 1MB)) MB)"
}

# ---------------------------------------------------------------- 4. USB
$usbStage = Join-Path $WorkDir 'usb'
if ($NoUsb) {
    Step 'Staging USB contents (no disk changes)'
    if (Test-Path $usbStage) { Remove-Item $usbStage -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $usbStage | Out-Null
    Copy-Item (Join-Path $efiRoot 'EFI') $usbStage -Recurse
    Copy-Item $recDir -Destination (Join-Path $usbStage 'com.apple.recovery.boot') -Recurse
    Copy-Item (Join-Path $efiRoot 'README.txt') $usbStage -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $WorkDir 'smbios.txt') $usbStage
    Ok "Copy the contents of $usbStage to a FAT32 USB stick."
    return
}

Step 'Select USB stick (it will be ERASED)'
$disks = @(Get-Disk | Where-Object { $_.BusType -eq 'USB' -and $_.Size -gt 4GB })
if ($disks.Count -eq 0) { throw 'No USB disk >= 4 GB found. Plug one in and run again.' }
$disks | Format-Table Number, FriendlyName, @{n='Size (GB)'; e={[math]::Round($_.Size / 1GB, 1)}}, PartitionStyle -AutoSize | Out-Host
if ($DiskNumber -lt 0) { $DiskNumber = [int](Read-Host '    Disk number') }
$disk = $disks | Where-Object Number -eq $DiskNumber
if (-not $disk) { throw "Disk $DiskNumber is not one of the USB disks listed." }
$confirm = Read-Host "    Type ERASE to wipe '$($disk.FriendlyName)' ($([math]::Round($disk.Size / 1GB, 1)) GB)"
if ($confirm -cne 'ERASE') { throw 'Cancelled.' }

Step 'Formatting'
if ($disk.PartitionStyle -ne 'RAW') { Clear-Disk -Number $DiskNumber -RemoveData -RemoveOEM -Confirm:$false }
Initialize-Disk -Number $DiskNumber -PartitionStyle GPT -ErrorAction SilentlyContinue
$size = [math]::Min([int64]$disk.Size - 64MB, 16GB)      # Windows formats FAT32 only up to 32 GB
$part = New-Partition -DiskNumber $DiskNumber -Size $size -AssignDriveLetter `
        -GptType '{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}'
Format-Volume -Partition $part -FileSystem FAT32 -NewFileSystemLabel 'CLAWMAC' -Confirm:$false | Out-Null
$letter = (Get-Partition -DiskNumber $DiskNumber -PartitionNumber $part.PartitionNumber).DriveLetter
if (-not $letter) { throw 'No drive letter assigned to the new partition.' }
$usb = "$($letter):\"
Ok "FAT32 at $usb"

Step 'Copying'
Copy-Item (Join-Path $efiRoot 'EFI') $usb -Recurse -Force
Copy-Item $recDir -Destination $usb -Recurse -Force
Copy-Item (Join-Path $efiRoot 'README.txt') $usb -ErrorAction SilentlyContinue
Copy-Item (Join-Path $WorkDir 'smbios.txt') $usb
Ok 'EFI, com.apple.recovery.boot, README, smbios.txt'

Write-Host @"

Done. Next:
  1. BIOS (hold Vol+ at power on): Secure Boot OFF, VT-d ON, DVMT max.
  2. Plug the USB stick and an external SSD (64 GB+) into the Claw.
  3. Boot menu -> CLAWMAC -> OpenCore picker -> 'macOS Base System'.
  4. In the installer use Disk Utility to erase the EXTERNAL SSD (APFS), then install.
  5. Never select the internal Windows drive.
"@ -ForegroundColor Green
