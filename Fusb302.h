#pragma once

typedef enum _FUSB302_POLARITY
{
    Fusb302PolarityCc1,
    Fusb302PolarityCc2
} FUSB302_POLARITY;

typedef enum _FUSB302_PORT_MODE
{
    Fusb302PortModeSink,
    Fusb302PortModeSource,
    Fusb302PortModeDualRole
} FUSB302_PORT_MODE;

typedef struct _FUSB302_PD_MESSAGE
{
    UINT16 Header;
    UINT32 Payload[7];
} FUSB302_PD_MESSAGE, *PFUSB302_PD_MESSAGE;

typedef struct _FUSB302_CONTEXT
{
    WDFDEVICE WdfDevice;
    WDFIOTARGET SpbIoTarget;
    WDFWAITLOCK SpbLock;
    WDFINTERRUPT Interrupt;
    BOOLEAN Prepared;
    BOOLEAN Powered;
    BOOLEAN VbusPresent;
    BOOLEAN Attached;
    FUSB302_POLARITY Polarity;
    FUSB302_PORT_MODE PortMode;
    UCHAR DeviceId;
} FUSB302_CONTEXT, *PFUSB302_CONTEXT;

EXTERN_C_START

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_Initialize(
    _In_ PFUSB302_CONTEXT FusbCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_PrepareHardware(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
Fusb302_ReleaseHardware(
    _In_ PFUSB302_CONTEXT FusbCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_PowerOn(
    _In_ PFUSB302_CONTEXT FusbCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
Fusb302_PowerOff(
    _In_ PFUSB302_CONTEXT FusbCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_StartToggling(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ FUSB302_PORT_MODE PortMode
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_TransmitPdMessage(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _In_ const FUSB302_PD_MESSAGE* Message
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_ReceivePdMessage(
    _In_ PFUSB302_CONTEXT FusbCtx,
    _Out_ PFUSB302_PD_MESSAGE Message
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Fusb302_SendHardReset(
    _In_ PFUSB302_CONTEXT FusbCtx
    );

EXTERN_C_END
