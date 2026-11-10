/* GMKtec M8 (Ryzen 5 PRO 6650H): 12 enabled CPUs as ACPI0007 (\_SB.PLTF.P000..). Legacy Processor objects, MADT IDs 1..12, Darwin only. */
DefinitionBlock ("", "SSDT", 2, "M8", "CpuPlugA", 0x00001000)
{
    Scope (\_SB)
    {
        Processor (CP00, 0x01, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP01, 0x02, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP02, 0x03, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP03, 0x04, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP04, 0x05, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP05, 0x06, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP06, 0x07, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP07, 0x08, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP08, 0x09, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP09, 0x0A, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP0A, 0x0B, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
        Processor (CP0B, 0x0C, 0x00000000, 0x00) { Method (_STA, 0, NotSerialized) { If (_OSI ("Darwin")) { Return (0x0F) } Return (Zero) } }
    }
    Method (\_SB.CP00._DSM, 4, NotSerialized)
    {
        If ((Arg2 == Zero)) { Return (Buffer (One) { 0x03 }) }
        Return (Package (0x02) { "plugin-type", One })
    }
}
