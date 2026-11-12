/*
 * SSDT-GFXOFF: keep macOS's framebuffer drivers off the MSI Claw 8 AI+ iGPU (Arc 140V, 8086:64A0, \_SB.PC00.GFX0)
 * so that IntelXeFB.kext can take the IOFramebuffer category once IntelXe.kext has the GPU up. The same trick as the
 * upstream SSDT-ARCOFF for the B580: a "#display" name and an all-ones class code (IONDRVFramebuffer otherwise attaches
 * to the GOP display and keeps it). IntelXeFB writes the VGA class back for IOPresentment.
 *
 * GFX0 already has a _DSM in most Intel DSDTs: config.plist renames it to XDSM (ACPI > Patch, Base \_SB.PC00.GFX0),
 * and this _DSM forwards every non-Darwin call to XDSM. If the rename did not match and GFX0 still has a _DSM, this
 * table fails to load (duplicate object) and macOS keeps the GOP display unaccelerated: a safe failure.
 *
 * Only in the "accel" EFI mode (tools/claw-intelxe.sh, Make-ClawMacUSB.ps1 -SetMode). Without IntelXeFB installed this
 * table leaves macOS without a display.
 */
DefinitionBlock ("", "SSDT", 2, "CLAW", "GFXOFF", 0x00001000)
{
    External (_SB_.PC00.GFX0, DeviceObj)
    External (_SB_.PC00.GFX0.XDSM, MethodObj)

    Scope (\_SB.PC00.GFX0)
    {
        Method (_DSM, 4, NotSerialized)  // _DSM: Device-Specific Method
        {
            If ((CondRefOf (\_OSI) && _OSI ("Darwin")))
            {
                If ((Arg2 == Zero))
                {
                    Return (Buffer (One)
                    {
                         0x03
                    })
                }

                Return (Package (0x06)
                {
                    "name",
                    Buffer (0x09)
                    {
                        "#display"
                    },

                    "IOName",
                    "#display",
                    "class-code",
                    Buffer (0x04)
                    {
                         0xFF, 0xFF, 0xFF, 0xFF
                    }
                })
            }

            If (CondRefOf (\_SB.PC00.GFX0.XDSM))
            {
                Return (\_SB.PC00.GFX0.XDSM (Arg0, Arg1, Arg2, Arg3))
            }

            Return (Buffer (One)
            {
                 0x00
            })
        }
    }
}
