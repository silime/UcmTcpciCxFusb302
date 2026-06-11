#include "Pch.h"
#include <reshub.h>
#include <spb.h>
#include "Fusb302Regs.h"
#include "Fusb302.tmh"

EXTERN_C_START

EVT_WDF_INTERRUPT_ISR Fusb302_EvtInterruptIsr;
EVT_WDF_INTERRUPT_WORKITEM Fusb302_EvtInterruptWorkItem;

EXTERN_C_END

typedef enum _FUSB302_CC_STATUS
{
    Fusb302CcOpen,
    Fusb302CcRa,
    Fusb302CcRd
} FUSB302_CC_STATUS;

static
NTSTATUS
Fusb302_Write(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_reads_bytes_(Length) const UCHAR* Buffer,
    _In_ ULONG Length
    )
{
    WDF_MEMORY_DESCRIPTOR descriptor;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&descriptor, const_cast<PUCHAR>(Buffer), Length);
    return WdfIoTargetSendWriteSynchronously(FusbCtx->SpbIoTarget,
                                              nullptr,
                                              &descriptor,
                                              nullptr,
                                              nullptr,
                                              nullptr);
}

static
NTSTATUS
Fusb302_WriteRegister(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ UCHAR Register,
    _In_ UCHAR Value
    )
{
    UCHAR buffer[] = { Register, Value };
    return Fusb302_Write(FusbCtx, buffer, sizeof(buffer));
}

static
NTSTATUS
Fusb302_ReadRegister(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ UCHAR Register,
    _Out_ PUCHAR Value
    )
{
    SPB_TRANSFER_LIST_AND_ENTRIES(2) sequence;
    WDF_MEMORY_DESCRIPTOR descriptor;

    SPB_TRANSFER_LIST_INIT(&sequence.List, 2);
    sequence.List.Transfers[0] =
        SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionToDevice, 0, &Register, sizeof(Register));
    sequence.List.Transfers[1] =
        SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionFromDevice, 0, Value, sizeof(*Value));

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&descriptor, &sequence, sizeof(sequence));
    return WdfIoTargetSendIoctlSynchronously(FusbCtx->SpbIoTarget,
                                              nullptr,
                                              IOCTL_SPB_EXECUTE_SEQUENCE,
                                              &descriptor,
                                              nullptr,
                                              nullptr,
                                              nullptr);
}

static
NTSTATUS
Fusb302_ReadBlock(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ UCHAR Register,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length
    )
{
    SPB_TRANSFER_LIST_AND_ENTRIES(2) sequence;
    WDF_MEMORY_DESCRIPTOR descriptor;

    SPB_TRANSFER_LIST_INIT(&sequence.List, 2);
    sequence.List.Transfers[0] =
        SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionToDevice, 0, &Register, sizeof(Register));
    sequence.List.Transfers[1] =
        SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionFromDevice, 0, Buffer, Length);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&descriptor, &sequence, sizeof(sequence));
    return WdfIoTargetSendIoctlSynchronously(FusbCtx->SpbIoTarget,
                                              nullptr,
                                              IOCTL_SPB_EXECUTE_SEQUENCE,
                                              &descriptor,
                                              nullptr,
                                              nullptr,
                                              nullptr);
}

static
NTSTATUS
Fusb302_UpdateRegister(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ UCHAR Register,
    _In_ UCHAR ClearMask,
    _In_ UCHAR SetMask
    )
{
    NTSTATUS status;
    UCHAR value;

    status = Fusb302_ReadRegister(FusbCtx, Register, &value);
    if (NT_SUCCESS(status))
    {
        value = (UCHAR)((value & ~ClearMask) | SetMask);
        status = Fusb302_WriteRegister(FusbCtx, Register, value);
    }

    return status;
}

static
NTSTATUS
Fusb302_OpenSpbTarget(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ ULONG IdLowPart,
    _In_ ULONG IdHighPart
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    DECLARE_UNICODE_STRING_SIZE(resourcePath, RESOURCE_HUB_PATH_CHARS);

    status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&resourcePath, IdLowPart, IdHighPart);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = FusbCtx->WdfDevice;

    status = WdfIoTargetCreate(FusbCtx->WdfDevice, &attributes, &FusbCtx->SpbIoTarget);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams,
                                                &resourcePath,
                                                GENERIC_READ | GENERIC_WRITE);
    openParams.ShareAccess = 0;
    return WdfIoTargetOpen(FusbCtx->SpbIoTarget, &openParams);
}

static
NTSTATUS
Fusb302_SetPolarityAndPull(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ FUSB302_POLARITY Polarity,
    _In_ BOOLEAN Source
    )
{
    NTSTATUS status;
    UCHAR switches0;
    UCHAR switches1;

    switches0 = Source ? 0 : (FUSB_REG_SWITCHES0_CC1_PD_EN | FUSB_REG_SWITCHES0_CC2_PD_EN);
    if (Polarity == Fusb302PolarityCc1)
    {
        switches0 |= FUSB_REG_SWITCHES0_MEAS_CC1;
        switches0 |= Source ? FUSB_REG_SWITCHES0_CC1_PU_EN : 0;
        switches1 = FUSB_REG_SWITCHES1_TXCC1_EN;
    }
    else
    {
        switches0 |= FUSB_REG_SWITCHES0_MEAS_CC2;
        switches0 |= Source ? FUSB_REG_SWITCHES0_CC2_PU_EN : 0;
        switches1 = FUSB_REG_SWITCHES1_TXCC2_EN;
    }

    status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_SWITCHES0, switches0);
    if (NT_SUCCESS(status))
    {
        status = Fusb302_UpdateRegister(FusbCtx,
                                        FUSB_REG_SWITCHES1,
                                        FUSB_REG_SWITCHES1_TXCC1_EN | FUSB_REG_SWITCHES1_TXCC2_EN,
                                        switches1);
    }

    if (NT_SUCCESS(status))
    {
        FusbCtx->Polarity = Polarity;
        FusbCtx->Attached = TRUE;
        if (Source)
            status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_MEASURE, 38);
    }
    if (NT_SUCCESS(status))
    {
        status = Fusb302_UpdateRegister(FusbCtx,
                                        FUSB_REG_MASK,
                                        FUSB_REG_MASK_BC_LVL | FUSB_REG_MASK_COMP_CHNG,
                                        Source ? FUSB_REG_MASK_BC_LVL : FUSB_REG_MASK_COMP_CHNG);
    }

    return status;
}

static
NTSTATUS
Fusb302_GetSourceCcStatus(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ FUSB302_POLARITY Polarity,
    _Out_ FUSB302_CC_STATUS* CcStatus
    )
{
    NTSTATUS status;
    UCHAR switches0;
    UCHAR status0 = 0;

    switches0 = Polarity == Fusb302PolarityCc1
        ? FUSB_REG_SWITCHES0_CC1_PU_EN | FUSB_REG_SWITCHES0_MEAS_CC1
        : FUSB_REG_SWITCHES0_CC2_PU_EN | FUSB_REG_SWITCHES0_MEAS_CC2;

    status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_SWITCHES0, switches0);
    if (NT_SUCCESS(status))
        status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_MEASURE, 38);
    if (NT_SUCCESS(status))
    {
        KeStallExecutionProcessor(50);
        status = Fusb302_ReadRegister(FusbCtx, FUSB_REG_STATUS0, &status0);
    }
    if (!NT_SUCCESS(status))
        return status;
    if (status0 & FUSB_REG_STATUS0_COMP)
    {
        *CcStatus = Fusb302CcOpen;
        return STATUS_SUCCESS;
    }

    status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_MEASURE, 4);
    if (NT_SUCCESS(status))
    {
        KeStallExecutionProcessor(50);
        status = Fusb302_ReadRegister(FusbCtx, FUSB_REG_STATUS0, &status0);
    }
    if (NT_SUCCESS(status))
        *CcStatus = (status0 & FUSB_REG_STATUS0_COMP) ? Fusb302CcRd : Fusb302CcRa;
    return status;
}

static
NTSTATUS
Fusb302_HandleToggleDone(
    _In_ PFUSB302_CONTEXT FusbCtx
    )
{
    NTSTATUS status;
    UCHAR status1a;
    UCHAR toggleState;
    BOOLEAN source;
    FUSB302_POLARITY polarity;
    FUSB302_CC_STATUS cc1 = Fusb302CcOpen;
    FUSB302_CC_STATUS cc2 = Fusb302CcOpen;

    status = Fusb302_ReadRegister(FusbCtx, FUSB_REG_STATUS1A, &status1a);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    toggleState = (status1a >> FUSB_REG_STATUS1A_TOGSS_POS) & FUSB_REG_STATUS1A_TOGSS_MASK;
    switch (toggleState)
    {
    case FUSB_REG_STATUS1A_TOGSS_SRC1:
        source = TRUE;
        polarity = Fusb302PolarityCc1;
        break;
    case FUSB_REG_STATUS1A_TOGSS_SRC2:
        source = TRUE;
        polarity = Fusb302PolarityCc2;
        break;
    case FUSB_REG_STATUS1A_TOGSS_SNK1:
        source = FALSE;
        polarity = Fusb302PolarityCc1;
        break;
    case FUSB_REG_STATUS1A_TOGSS_SNK2:
        source = FALSE;
        polarity = Fusb302PolarityCc2;
        break;
    default:
        return Fusb302_StartToggling(FusbCtx, FusbCtx->PortMode);
    }

    status = Fusb302_UpdateRegister(FusbCtx,
                                    FUSB_REG_CONTROL2,
                                    FUSB_REG_CONTROL2_TOGGLE,
                                    0);
    if (NT_SUCCESS(status) && source)
        status = Fusb302_GetSourceCcStatus(FusbCtx, Fusb302PolarityCc1, &cc1);
    if (NT_SUCCESS(status) && source)
        status = Fusb302_GetSourceCcStatus(FusbCtx, Fusb302PolarityCc2, &cc2);
    if (NT_SUCCESS(status) && source)
    {
        if (cc1 == Fusb302CcRd && cc2 != Fusb302CcRd)
            polarity = Fusb302PolarityCc1;
        else if (cc2 == Fusb302CcRd && cc1 != Fusb302CcRd)
            polarity = Fusb302PolarityCc2;
        else
            return Fusb302_StartToggling(FusbCtx, FusbCtx->PortMode);
    }
    if (NT_SUCCESS(status))
    {
        status = Fusb302_SetPolarityAndPull(FusbCtx, polarity, source);
    }

    TRACE_INFO(TRACE_FLAG_FUSB302,
               "[Device: 0x%p] attached, polarity=%s, role=%s",
               FusbCtx->WdfDevice,
               polarity == Fusb302PolarityCc1 ? "CC1" : "CC2",
               source ? "source" : "sink");
    return status;
}

NTSTATUS
Fusb302_Initialize(
    _In_ PFUSB302_CONTEXT FusbCtx
    )
{
    WDF_OBJECT_ATTRIBUTES attributes;

    FusbCtx->SpbIoTarget = WDF_NO_HANDLE;
    FusbCtx->Interrupt = WDF_NO_HANDLE;
    FusbCtx->PortMode = Fusb302PortModeDualRole;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = FusbCtx->WdfDevice;
    return WdfWaitLockCreate(&attributes, &FusbCtx->SpbLock);
}

NTSTATUS
Fusb302_PrepareHardware(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status = STATUS_DEVICE_CONFIGURATION_ERROR;
    ULONG count = WdfCmResourceListGetCount(ResourcesTranslated);
    BOOLEAN spbFound = FALSE;
    BOOLEAN interruptFound = FALSE;

    for (ULONG index = 0; index < count; ++index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw = WdfCmResourceListGetDescriptor(ResourcesRaw, index);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR translated = WdfCmResourceListGetDescriptor(ResourcesTranslated, index);

        if (raw->Type == CmResourceTypeConnection &&
            raw->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            raw->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
        {
            status = Fusb302_OpenSpbTarget(FusbCtx,
                                           raw->u.Connection.IdLowPart,
                                           raw->u.Connection.IdHighPart);
            if (!NT_SUCCESS(status))
            {
                return status;
            }
            spbFound = TRUE;
        }
        else if (translated->Type == CmResourceTypeInterrupt)
        {
            WDF_INTERRUPT_CONFIG config;
            WDF_INTERRUPT_CONFIG_INIT(&config, Fusb302_EvtInterruptIsr, nullptr);
            config.InterruptRaw = raw;
            config.InterruptTranslated = translated;
            config.EvtInterruptWorkItem = Fusb302_EvtInterruptWorkItem;

            status = WdfInterruptCreate(FusbCtx->WdfDevice,
                                        &config,
                                        WDF_NO_OBJECT_ATTRIBUTES,
                                        &FusbCtx->Interrupt);
            if (!NT_SUCCESS(status))
            {
                return status;
            }
            interruptFound = TRUE;
        }
    }

    if (!spbFound || !interruptFound)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    FusbCtx->Prepared = TRUE;
    return STATUS_SUCCESS;
}

VOID
Fusb302_ReleaseHardware(
    _In_ PFUSB302_CONTEXT FusbCtx
    )
{
    if (FusbCtx->SpbIoTarget != WDF_NO_HANDLE)
    {
        WdfIoTargetClose(FusbCtx->SpbIoTarget);
        WdfObjectDelete(FusbCtx->SpbIoTarget);
        FusbCtx->SpbIoTarget = WDF_NO_HANDLE;
    }

    if (FusbCtx->Interrupt != WDF_NO_HANDLE)
    {
        WdfObjectDelete(FusbCtx->Interrupt);
        FusbCtx->Interrupt = WDF_NO_HANDLE;
    }

    FusbCtx->Prepared = FALSE;
}

NTSTATUS
Fusb302_PowerOn(
    _In_ PFUSB302_CONTEXT FusbCtx
    )
{
    NTSTATUS status;
    UCHAR status0 = 0;

    WdfWaitLockAcquire(FusbCtx->SpbLock, nullptr);

    status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_RESET, FUSB_REG_RESET_SW_RESET);
    if (NT_SUCCESS(status))
        status = Fusb302_UpdateRegister(FusbCtx, FUSB_REG_CONTROL3, 0,
                                        FUSB_REG_CONTROL3_AUTO_RETRY | FUSB_REG_CONTROL3_N_RETRIES_3);
    if (NT_SUCCESS(status))
        status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_MASK, 0x7F);
    if (NT_SUCCESS(status))
        status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_MASKA, 0xFF);
    if (NT_SUCCESS(status))
        status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_MASKB, 0xFF);
    if (NT_SUCCESS(status))
        status = Fusb302_UpdateRegister(FusbCtx, FUSB_REG_CONTROL0, FUSB_REG_CONTROL0_INT_MASK, 0);
    if (NT_SUCCESS(status))
        status = Fusb302_WriteRegister(FusbCtx, FUSB_REG_POWER, FUSB_REG_POWER_PWR_ALL);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadRegister(FusbCtx, FUSB_REG_DEVICE_ID, &FusbCtx->DeviceId);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadRegister(FusbCtx, FUSB_REG_STATUS0, &status0);
    if (NT_SUCCESS(status))
    {
        FusbCtx->VbusPresent = !!(status0 & FUSB_REG_STATUS0_VBUSOK);
        FusbCtx->Powered = TRUE;
        status = Fusb302_StartToggling(FusbCtx, Fusb302PortModeDualRole);
    }

    WdfWaitLockRelease(FusbCtx->SpbLock);

    TRACE_INFO(TRACE_FLAG_FUSB302,
               "[Device: 0x%p] FUSB302 device id=0x%02x, VBUS=%u",
               FusbCtx->WdfDevice,
               FusbCtx->DeviceId,
               FusbCtx->VbusPresent);
    return status;
}

VOID
Fusb302_PowerOff(
    _In_ PFUSB302_CONTEXT FusbCtx
    )
{
    WdfWaitLockAcquire(FusbCtx->SpbLock, nullptr);
    (void)Fusb302_UpdateRegister(FusbCtx, FUSB_REG_CONTROL0, 0, FUSB_REG_CONTROL0_INT_MASK);
    (void)Fusb302_WriteRegister(FusbCtx, FUSB_REG_POWER, FUSB_REG_POWER_PWR_LOW);
    FusbCtx->Powered = FALSE;
    FusbCtx->Attached = FALSE;
    WdfWaitLockRelease(FusbCtx->SpbLock);
}

NTSTATUS
Fusb302_StartToggling(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ FUSB302_PORT_MODE PortMode
    )
{
    NTSTATUS status;
    UCHAR mode;

    switch (PortMode)
    {
    case Fusb302PortModeSink:
        mode = FUSB_REG_CONTROL2_MODE_UFP;
        break;
    case Fusb302PortModeSource:
        mode = FUSB_REG_CONTROL2_MODE_DFP;
        break;
    default:
        mode = FUSB_REG_CONTROL2_MODE_DRP;
        break;
    }

    status = Fusb302_UpdateRegister(FusbCtx,
                                    FUSB_REG_CONTROL2,
                                    FUSB_REG_CONTROL2_TOGGLE | FUSB_REG_CONTROL2_MODE_MASK,
                                    mode);
    if (NT_SUCCESS(status))
    {
        status = Fusb302_UpdateRegister(FusbCtx,
                                        FUSB_REG_MASKA,
                                        FUSB_REG_MASKA_TOGDONE,
                                        0);
    }
    if (NT_SUCCESS(status))
    {
        status = Fusb302_UpdateRegister(FusbCtx,
                                        FUSB_REG_CONTROL2,
                                        0,
                                        FUSB_REG_CONTROL2_TOGGLE);
    }
    if (NT_SUCCESS(status))
    {
        FusbCtx->PortMode = PortMode;
        FusbCtx->Attached = FALSE;
    }
    return status;
}

NTSTATUS
Fusb302_TransmitPdMessage(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ const FUSB302_PD_MESSAGE* Message
    )
{
    UCHAR buffer[40];
    ULONG position = 0;
    ULONG payloadBytes = ((Message->Header >> 12) & 0x07) * sizeof(UINT32);
    ULONG messageBytes = sizeof(Message->Header) + payloadBytes;

    if (messageBytes > 0x1F)
        return STATUS_INVALID_BUFFER_SIZE;

    buffer[position++] = FUSB_REG_FIFOS;
    buffer[position++] = FUSB302_TKN_SYNC1;
    buffer[position++] = FUSB302_TKN_SYNC1;
    buffer[position++] = FUSB302_TKN_SYNC1;
    buffer[position++] = FUSB302_TKN_SYNC2;
    buffer[position++] = (UCHAR)(FUSB302_TKN_PACKSYM | messageBytes);
    RtlCopyMemory(&buffer[position], &Message->Header, sizeof(Message->Header));
    position += sizeof(Message->Header);
    RtlCopyMemory(&buffer[position], Message->Payload, payloadBytes);
    position += payloadBytes;
    buffer[position++] = FUSB302_TKN_JAMCRC;
    buffer[position++] = FUSB302_TKN_EOP;
    buffer[position++] = FUSB302_TKN_TXOFF;
    buffer[position++] = FUSB302_TKN_TXON;

    WdfWaitLockAcquire(FusbCtx->SpbLock, nullptr);
    NTSTATUS status = Fusb302_Write(FusbCtx, buffer, position);
    WdfWaitLockRelease(FusbCtx->SpbLock);
    return status;
}

NTSTATUS
Fusb302_ReceivePdMessage(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _Out_ PFUSB302_PD_MESSAGE Message
    )
{
    NTSTATUS status;
    UCHAR token;
    UCHAR crc[4];
    ULONG payloadBytes;

    RtlZeroMemory(Message, sizeof(*Message));
    WdfWaitLockAcquire(FusbCtx->SpbLock, nullptr);

    status = Fusb302_ReadRegister(FusbCtx, FUSB_REG_FIFOS, &token);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadBlock(FusbCtx, FUSB_REG_FIFOS, (PUCHAR)&Message->Header, sizeof(Message->Header));

    payloadBytes = ((Message->Header >> 12) & 0x07) * sizeof(UINT32);
    if (NT_SUCCESS(status) && payloadBytes > sizeof(Message->Payload))
        status = STATUS_INVALID_BUFFER_SIZE;
    if (NT_SUCCESS(status) && payloadBytes != 0)
        status = Fusb302_ReadBlock(FusbCtx, FUSB_REG_FIFOS, (PUCHAR)Message->Payload, payloadBytes);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadBlock(FusbCtx, FUSB_REG_FIFOS, crc, sizeof(crc));

    WdfWaitLockRelease(FusbCtx->SpbLock);
    return status;
}

NTSTATUS
Fusb302_SendHardReset(
    _In_ PFUSB302_CONTEXT FusbCtx
    )
{
    WdfWaitLockAcquire(FusbCtx->SpbLock, nullptr);
    NTSTATUS status = Fusb302_UpdateRegister(FusbCtx,
                                             FUSB_REG_CONTROL3,
                                             0,
                                             FUSB_REG_CONTROL3_SEND_HARDRESET);
    WdfWaitLockRelease(FusbCtx->SpbLock);
    return status;
}

BOOLEAN
Fusb302_EvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageId
    )
{
    UNREFERENCED_PARAMETER(MessageId);
    return WdfInterruptQueueWorkItemForIsr(Interrupt);
}

VOID
Fusb302_EvtInterruptWorkItem(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject
    )
{
    UNREFERENCED_PARAMETER(AssociatedObject);

    WDFDEVICE device = WdfInterruptGetDevice(Interrupt);
    PFUSB302_CONTEXT fusbCtx = &Fdo_GetContext(device)->Fusb302Ctx;
    UCHAR interrupt = 0;
    UCHAR interruptA = 0;
    UCHAR interruptB = 0;
    UCHAR status0 = 0;

    WdfWaitLockAcquire(fusbCtx->SpbLock, nullptr);
    NTSTATUS status = Fusb302_ReadRegister(fusbCtx, FUSB_REG_INTERRUPT, &interrupt);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadRegister(fusbCtx, FUSB_REG_INTERRUPTA, &interruptA);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadRegister(fusbCtx, FUSB_REG_INTERRUPTB, &interruptB);
    if (NT_SUCCESS(status))
        status = Fusb302_ReadRegister(fusbCtx, FUSB_REG_STATUS0, &status0);

    if (NT_SUCCESS(status) && (interrupt & FUSB_REG_INTERRUPT_VBUSOK))
        fusbCtx->VbusPresent = !!(status0 & FUSB_REG_STATUS0_VBUSOK);

    if (NT_SUCCESS(status) && (interruptA & FUSB_REG_INTERRUPTA_TOGDONE))
        status = Fusb302_HandleToggleDone(fusbCtx);

    if (NT_SUCCESS(status) && (interrupt & FUSB_REG_INTERRUPT_COMP_CHNG) &&
        (status0 & FUSB_REG_STATUS0_COMP))
    {
        fusbCtx->Attached = FALSE;
        status = Fusb302_StartToggling(fusbCtx, fusbCtx->PortMode);
    }

    if (NT_SUCCESS(status) && (interrupt & FUSB_REG_INTERRUPT_BC_LVL) &&
        ((status0 & FUSB_REG_STATUS0_BC_LVL_MASK) == 0))
    {
        fusbCtx->Attached = FALSE;
        status = Fusb302_StartToggling(fusbCtx, fusbCtx->PortMode);
    }

    WdfWaitLockRelease(fusbCtx->SpbLock);

    TRACE_INFO(TRACE_FLAG_FUSB302,
               "[Device: 0x%p] IRQ=%02x A=%02x B=%02x status0=%02x status=%!STATUS!",
               device,
               interrupt,
               interruptA,
               interruptB,
               status0,
               status);
}
