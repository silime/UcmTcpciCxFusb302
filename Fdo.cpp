/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    Fdo.cpp

Abstract:

    FDO callbacks, functions, and types.

Environment:

    Kernel-mode only.

--*/

#include "Pch.h"
#include "Fdo.tmh"

EXTERN_C_START

EVT_WDF_DEVICE_PREPARE_HARDWARE Fdo_EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE Fdo_EvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY Fdo_EvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT Fdo_EvtDeviceD0Exit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT Fdo_EvtDeviceSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART Fdo_EvtDeviceSelfManagedIoRestart;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fdo_Initialize (
    _In_ PFDO_CONTEXT FdoCtx
    );

EXTERN_C_END

#pragma alloc_text(PAGE, Fdo_Create)
#pragma alloc_text(PAGE, Fdo_Initialize)
#pragma alloc_text(PAGE, Fdo_EvtDevicePrepareHardware)
#pragma alloc_text(PAGE, Fdo_EvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, Fdo_EvtDeviceD0Entry)
#pragma alloc_text(PAGE, Fdo_EvtDeviceD0Exit)
#pragma alloc_text(PAGE, Fdo_EvtDeviceSelfManagedIoInit)
#pragma alloc_text(PAGE, Fdo_EvtDeviceSelfManagedIoRestart)


_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fdo_Create (
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    PFDO_CONTEXT fdoCtx;
    WDFDEVICE wdfDevice;
    NTSTATUS status;

    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = Fdo_EvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = Fdo_EvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = Fdo_EvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = Fdo_EvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = Fdo_EvtDeviceSelfManagedIoInit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoRestart = Fdo_EvtDeviceSelfManagedIoRestart;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WdfDeviceInitSetPowerPageable(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FDO_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &wdfDevice);
    if (!NT_SUCCESS(status))
    {
        TRACE_ERROR(TRACE_FLAG_FDO, "[DeviceInit: 0x%p] WdfDeviceCreate failed - %!STATUS!", DeviceInit, status);
        goto Exit;
    }

    fdoCtx = Fdo_GetContext(wdfDevice);
    fdoCtx->WdfDevice = wdfDevice;

    status = Fdo_Initialize(fdoCtx);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    fdoCtx->Fusb302Ctx.WdfDevice = wdfDevice;
    status = Fusb302_Initialize(&fdoCtx->Fusb302Ctx);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    TRACE_INFO(TRACE_FLAG_FDO, "[Device: 0x%p] Device created", wdfDevice);

Exit:

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fdo_Initialize (
    _In_ PFDO_CONTEXT FdoCtx
    )
{
    WDFDEVICE device;
    WDF_DEVICE_STATE deviceState;

    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    device = FdoCtx->WdfDevice;

    //
    // ACPI-enumerated devices are not disableable by default. Override that,
    // since there is no need for that restriction.
    //

    WDF_DEVICE_STATE_INIT(&deviceState);
    deviceState.NotDisableable = WdfFalse;
    WdfDeviceSetDeviceState(device, &deviceState);

    TRACE_INFO(TRACE_FLAG_FDO, "[Device: 0x%p] FDO initialized", device);

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return STATUS_SUCCESS;
}


NTSTATUS
Fdo_EvtDevicePrepareHardware (
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    PFDO_CONTEXT fdoCtx;

    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    fdoCtx = Fdo_GetContext(Device);
    status = Fusb302_PrepareHardware(&fdoCtx->Fusb302Ctx,
                                     ResourcesRaw,
                                     ResourcesTranslated);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    TRACE_INFO(TRACE_FLAG_FDO, "[Device: 0x%p] Prepare hardware completed", Device);

Exit:

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return status;
}


NTSTATUS
Fdo_EvtDeviceReleaseHardware (
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PFDO_CONTEXT fdoCtx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    fdoCtx = Fdo_GetContext(Device);

    Fusb302_ReleaseHardware(&fdoCtx->Fusb302Ctx);

    TRACE_INFO(TRACE_FLAG_FDO, "[Device: 0x%p] Release hardware completed", Device);

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return STATUS_SUCCESS;
}


NTSTATUS
Fdo_EvtDeviceD0Entry (
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    NTSTATUS status;
    PFDO_CONTEXT fdoCtx;

    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    fdoCtx = Fdo_GetContext(Device);

    TRACE_INFO(TRACE_FLAG_FDO, "[Device: 0x%p] Entering D0 from %!WDF_POWER_DEVICE_STATE!", Device, PreviousState);

    status = Fusb302_PowerOn(&fdoCtx->Fusb302Ctx);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

Exit:

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return status;
}


NTSTATUS
Fdo_EvtDeviceD0Exit (
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PFDO_CONTEXT fdoCtx;

    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    fdoCtx = Fdo_GetContext(Device);

    TRACE_INFO(TRACE_FLAG_FDO, "[Device: 0x%p] Exiting D0 to %!WDF_POWER_DEVICE_STATE!", Device, TargetState);

    Fusb302_PowerOff(&fdoCtx->Fusb302Ctx);

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return STATUS_SUCCESS;
}


NTSTATUS
Fdo_EvtDeviceSelfManagedIoInit (
    _In_ WDFDEVICE Device
    )
{
    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    UNREFERENCED_PARAMETER(Device);

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return STATUS_SUCCESS;
}


NTSTATUS
Fdo_EvtDeviceSelfManagedIoRestart (
    _In_ WDFDEVICE Device
    )
{
    PAGED_CODE();

    TRACE_FUNC_ENTRY(TRACE_FLAG_FDO);

    UNREFERENCED_PARAMETER(Device);

    TRACE_FUNC_EXIT(TRACE_FLAG_FDO);

    return STATUS_SUCCESS;
}
