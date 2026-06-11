---
page_type: sample
description: "Demonstrates a UcmTcpciCx-based implementation of Windows USB Type-C Port Controller Interface driver for FUSB302."
languages:
- cpp
products:
- windows
- windows-wdk
---

# FUSB302 USB Type-C Port Controller Driver (Based on UcmTcpciCx)

This project is a hardware-specific implementation of a Windows USB Type-C Connector Manager client driver, adapted from the Windows Driver Kit (WDK) **UcmTcpciCxSample**. 

It replaces the simulated layer with real-world hardware integration for the **Fairchild/ON Semi FUSB302** Type-C Controller. It implements a software TCPM (Type-C Port Manager) state machine within the KMDF driver to handle CC line detection, USB PD (Power Delivery) negotiation, and power role swapping under Windows.

## Hardware Topology & Resources (Rockchip Platform)

The driver relies on the ACPI subsystem to discover and communicate with the FUSB302 IC. Based on the Rockchip DTS specification, the resources are routed as follows:

* **Bus Interface:** I2C6 (7-bit Slave Address: `0x22` or `0x23` depending on hardware pull-up, typically `0x22`)
* **Interrupt Line (INT):** `\_SB.GPI0` -> Pin `27` (DTS: `&gpio0 RK_PD3`, `IRQ_TYPE_LEVEL_LOW`)

---

## ACPI (ASL) Configuration

To allow Windows to enumerate the FUSB302 device and pass the correct I2C/GPIO resources to this driver, add the following device definition into your platform's DSDT/SSDT (under the `\_SB.I2C6` scope).

```asl
Scope (\_SB.I2C6)
{
    Device (FUSB)
    {
        Name (_HID, "FUSB302")          // Hardware ID matched by the INF
        Name (_UID, One)
        Name (_DEP, Package () { \_SB.GPI0, \_SB.I2C6 }) // Dependencies

        Method (_CRS, 0, Serialized) // Current Resource Settings
        {
            Name (RBUF, ResourceTemplate ()
            {
                // 1. I2C Connection: 400Khz, Address 0x22 (Change to 0x23 if needed)
                I2cSerialBusV2 (0x0022, ControllerInitiated, 0x000186A0,
                    AddressingMode7Bit, "\\_SB.I2C6",
                    0x00, ResourceConsumer, , Exclusive)

                // 2. Interrupt GPIO: GPIO0_D3 (Global Pin 27), Level-Triggered, Active Low
                GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
                    "\\_SB.GPI0", 0x00, ResourceConsumer, ,
                    )
                    { 27 } 
            })
            Return (RBUF)
        }
    }
}
```