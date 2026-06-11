---
page_type: sample
description: "Demonstrates a UcmTcpciCx-based implementation of Windows USB Type-C Port Controller Interface driver for FUSB302."
languages:
- cpp
products:
- windows
- windows-wdk
---

# FUSB302 USB Type-C Port Controller Driver for Windows

This project contains a native KMDF hardware driver foundation for the
Fairchild/ON Semiconductor FUSB302 USB Type-C controller.

The `Fusb302.cpp` module uses Windows Resource Hub and SPB APIs to communicate
with the controller over I2C. It initializes the chip, handles its GPIO
interrupt through a KMDF interrupt work item, starts DRP toggling, detects
orientation/attach state, and exposes the basic USB PD transmit and hard-reset
operations.

The Linux `fusb302.c` file is reference material only and is not part of the
Windows build.

> FUSB302 is not a TCPCI or UCSI device. A complete Windows connector-manager
> implementation also needs a software TCPM/policy engine that consumes the
> hardware events and reports connector state through UcmCx. The current code
> deliberately does not claim that the original firmware-UCSI sample backend
> can perform that role.

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
