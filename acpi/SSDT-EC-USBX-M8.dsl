/*
 * GMKtec M8: macOS wants a device named EC; the real one is \_SB.PCI0.SBRG.EC0
 * (left alone, ECEnabler + SMCBatteryManager use it). Fake EC + USBX power properties, Darwin only.
 */
DefinitionBlock ("", "SSDT", 2, "M8", "EcUsbx", 0x00001000)
{
    External (_SB_.PCI0.SBRG, DeviceObj)

    Scope (\_SB.PCI0.SBRG)
    {
        Device (EC)
        {
            Name (_HID, "ACID0001")
            Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) }
        }
    }

    Scope (\_SB)
    {
        Device (USBX)
        {
            Name (_ADR, Zero)
            Method (_DSM, 4, NotSerialized)
            {
                If ((Arg2 == Zero)) { Return (Buffer (One) { 0x03 }) }
                Return (Package (0x08)
                {
                    "kUSBSleepPowerSupply", 0x13EC,
                    "kUSBSleepPortCurrentLimit", 0x0834,
                    "kUSBWakePowerSupply", 0x13EC,
                    "kUSBWakePortCurrentLimit", 0x0834
                })
            }
            Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) }
        }
    }
}
