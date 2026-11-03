#Requires -Version 5.1
<# Run on Windows on the Claw: prints GPU PCI IDs so ClawXe2's IOPCIMatch can be confirmed. #>
Get-PnpDevice -Class Display -PresentOnly | ForEach-Object {
    $hw = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_HardwareIds).Data | Select-Object -First 1
    $drv = Get-CimInstance Win32_VideoController | Where-Object PNPDeviceID -eq $_.InstanceId
    [pscustomobject]@{
        Name     = $_.FriendlyName
        HwId     = $hw
        DeviceId = if ($hw -match 'DEV_([0-9A-F]{4})') { '0x' + $Matches[1].ToLower() } else { '?' }
        Driver   = $drv.DriverVersion
        Matched  = $hw -match 'VEN_8086&DEV_(6420|64A0|64B0)'
    }
} | Format-List
