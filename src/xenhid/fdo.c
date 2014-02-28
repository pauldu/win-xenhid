/* Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, 
 * with or without modification, are permitted provided 
 * that the following conditions are met:
 * 
 * *   Redistributions of source code must retain the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer.
 * *   Redistributions in binary form must reproduce the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer in the documentation and/or other 
 *     materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#define INITGUID 1

#include <ntddk.h>
#include <wdmguid.h>
#include <ntstrsafe.h>
#include <hidport.h>
#include <stdlib.h>
#include <util.h>

#include <store_interface.h>
#include <debug_interface.h>
#include <evtchn_interface.h>
#include <gnttab_interface.h>
#include <suspend_interface.h>

#include "driver.h"
#include "fdo.h"
#include "frontend.h"
#include "names.h"
#include "dbg_print.h"
#include "assert.h"

#define MAXIRPCACHE             4
#define MAXNAMELEN              128

typedef enum _DEVICE_PNP_STATE {
    Invalid = 0,
    Present,        // PDO only
    Enumerated,     // PDO only
    Added,          // FDO only
    Started,
    StopPending,
    Stopped,
    RemovePending,
    SurpriseRemovePending,
    Deleted
} DEVICE_PNP_STATE, *PDEVICE_PNP_STATE;

struct _XENHID_FDO {
    PDEVICE_OBJECT              DeviceObject;
    PDEVICE_OBJECT              LowerDeviceObject;

    DEVICE_PNP_STATE            DevicePnpState;
    DEVICE_PNP_STATE            PreviousDevicePnpState;
    DEVICE_POWER_STATE          DevicePowerState;
    SYSTEM_POWER_STATE          SystemPowerState;

    PXENHID_FRONTEND            Frontend;

    BOOLEAN                     Enabled;
    KSPIN_LOCK                  Lock;
    PIRP                        Irps[MAXIRPCACHE];

    PXENBUS_STORE_INTERFACE     StoreInterface;
    PXENBUS_DEBUG_INTERFACE     DebugInterface;
    PXENBUS_EVTCHN_INTERFACE    EvtchnInterface;
    PXENBUS_GNTTAB_INTERFACE    GnttabInterface;
    PXENBUS_SUSPEND_INTERFACE   SuspendInterface;

    PXENBUS_DEBUG_CALLBACK      DebugCallback;
    PXENBUS_SUSPEND_CALLBACK    SuspendCallback;
};

ULONG
FdoSize(
    )
{
    return sizeof(XENHID_FDO);
}

static FORCEINLINE VOID
__FdoSetDevicePnpState(
    IN  PXENHID_FDO         Fdo,
    IN  DEVICE_PNP_STATE    State
    )
{
    // We can never transition out of the deleted state
    ASSERT(Fdo->DevicePnpState != Deleted || State == Deleted);

    Fdo->PreviousDevicePnpState = Fdo->DevicePnpState;
    Fdo->DevicePnpState = State;
}

static FORCEINLINE VOID
__FdoRestoreDevicePnpState(
    IN  PXENHID_FDO         Fdo,
    IN  DEVICE_PNP_STATE    State
    )
{
    if (Fdo->DevicePnpState == State)
        Fdo->DevicePnpState = Fdo->PreviousDevicePnpState;
}

static FORCEINLINE DEVICE_PNP_STATE
__FdoGetDevicePnpState(
    IN  PXENHID_FDO     Fdo
    )
{
    return Fdo->DevicePnpState;
}

static FORCEINLINE VOID
__FdoSetDevicePowerState(
    IN  PXENHID_FDO         Fdo,
    IN  DEVICE_POWER_STATE  State
    )
{
    Fdo->DevicePowerState = State;
}

static FORCEINLINE DEVICE_POWER_STATE
__FdoGetDevicePowerState(
    IN  PXENHID_FDO     Fdo
    )
{
    return Fdo->DevicePowerState;
}

static FORCEINLINE VOID
__FdoSetSystemPowerState(
    IN  PXENHID_FDO         Fdo,
    IN  SYSTEM_POWER_STATE  State
    )
{
    Fdo->SystemPowerState = State;
}

static FORCEINLINE SYSTEM_POWER_STATE
__FdoGetSystemPowerState(
    IN  PXENHID_FDO     Fdo
    )
{
    return Fdo->SystemPowerState;
}

static FORCEINLINE PCHAR
__FdoGetStorePath(
    IN  PXENHID_FDO     Fdo
    )
{
    UNREFERENCED_PARAMETER(Fdo);
    return "device/vkbd/0";
}

PCHAR
FdoGetStorePath(
    IN  PXENHID_FDO     Fdo
    )
{
    return __FdoGetStorePath(Fdo);
}

static FORCEINLINE NTSTATUS
__FdoCache(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    NTSTATUS    status;
    ULONG       Index;
    KIRQL       Irql;

    status = STATUS_DEVICE_NOT_READY;
    if (Fdo->Enabled == FALSE)
        goto done;

    status = STATUS_UNSUCCESSFUL;
    KeAcquireSpinLock(&Fdo->Lock, &Irql);
    for (Index = 0; Index < MAXIRPCACHE; ++Index) {
        if (Fdo->Irps[Index] == NULL) {
            Fdo->Irps[Index] = Irp;
            status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseSpinLock(&Fdo->Lock, Irql);

done:
    return status;
}

static FORCEINLINE PIRP
__FdoUncache(
    IN  PXENHID_FDO     Fdo
    )
{
    KIRQL       Irql;
    ULONG       Index;
    PIRP        Irp;

    Irp = NULL;
    KeAcquireSpinLock(&Fdo->Lock, &Irql);
    for (Index = 0; Index < MAXIRPCACHE; ++Index) {
        if (Fdo->Irps[Index] != NULL) {
            Irp = Fdo->Irps[Index];
            for (; Index < MAXIRPCACHE-1; ++Index) 
                Fdo->Irps[Index] = Fdo->Irps[Index+1];
            Fdo->Irps[MAXIRPCACHE-1] = NULL;
            break;
        }
    }
    KeReleaseSpinLock(&Fdo->Lock, Irql);

    return Irp;
}

NTSTATUS
FdoCompleteRead(
    IN  PXENHID_FDO         Fdo,
    IN  PVOID               Buffer,
    IN  ULONG               Length
    )
{
    NTSTATUS    status;
    PIRP        Irp;
    
    status = STATUS_DEVICE_NOT_READY;
    Irp = __FdoUncache(Fdo);
    if (Irp == NULL)
        goto done;

    // should check buffer sizes and fail if wrong
    RtlCopyMemory(Irp->UserBuffer, Buffer, Length);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = Length;

    status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

done:
    return status;
}

static FORCEINLINE VOID
FdoPauseData(
    IN  PXENHID_FDO     Fdo
    )
{
    Fdo->Enabled = FALSE;

    for (;;) {
        PIRP    Irp = __FdoUncache(Fdo);
        if (Irp == NULL)
            break;

        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static FORCEINLINE VOID
FdoResumeData(
    IN  PXENHID_FDO     Fdo
    )
{
    Fdo->Enabled = TRUE;
}

__drv_functionClass(IO_COMPLETION_ROUTINE)
__drv_sameIRQL
static NTSTATUS
__FdoForwardIrpSynchronously(
    IN  PDEVICE_OBJECT  DeviceObject,
    IN  PIRP            Irp,
    IN  PVOID           Context
    )
{
    PKEVENT             Event = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
FdoForwardIrpSynchronously(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    KEVENT              Event;
    NTSTATUS            status;

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           __FdoForwardIrpSynchronously,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);
    if (status == STATUS_PENDING) {
        (VOID) KeWaitForSingleObject(&Event,
                                     Executive,
                                     KernelMode,
                                     FALSE,
                                     NULL);
        status = Irp->IoStatus.Status;
    } else {
        ASSERT3U(status, ==, Irp->IoStatus.Status);
    }

    Trace("%08x\n", status);

    return status;
}

static VOID
FdoDebugCallback(
    IN  PVOID       Context,
    IN  BOOLEAN     Crashing
    )
{
    ULONG           Index;
    PXENHID_FDO     Fdo = Context;

    UNREFERENCED_PARAMETER(Crashing);

    DEBUG(Printf,
            Fdo->DebugInterface,
            Fdo->DebugCallback,
            "%s\n",
            __FdoGetStorePath(Fdo));

    DEBUG(Printf,
            Fdo->DebugInterface,
            Fdo->DebugCallback,
            "%s\n", 
            Fdo->Enabled ? "WORKING" : "PAUSED");

    for (Index = 0; Index < MAXIRPCACHE; ++Index) {
        DEBUG(Printf,
                Fdo->DebugInterface,
                Fdo->DebugCallback,
                "IRPs[%d] = 0x%p\n",
                Index,
                Fdo->Irps[Index]);
    }
}

static FORCEINLINE NTSTATUS
__FdoD3ToD0(
    IN  PXENHID_FDO Fdo
    )
{
    NTSTATUS        status;

    Trace("====>\n");

    ASSERT3U(KeGetCurrentIrql(), ==, DISPATCH_LEVEL);

    DEBUG(Acquire, Fdo->DebugInterface);

    status = DEBUG(Register,
                   Fdo->DebugInterface,
                   __MODULE__,
                   FdoDebugCallback,
                   Fdo,
                   &Fdo->DebugCallback);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = FrontendEnable(Fdo->Frontend);
    if (!NT_SUCCESS(status))
        goto fail2;

    Trace("<====\n");

    return STATUS_SUCCESS;

fail2:
    Error("fail2\n");

    DEBUG(Deregister, Fdo->DebugInterface, Fdo->DebugCallback);
    Fdo->DebugCallback = NULL;

fail1:
    Error("fail1 (%08x)\n", status);

    DEBUG(Release, Fdo->DebugInterface);

    return status;
}

static FORCEINLINE VOID
__FdoD0ToD3(
    IN  PXENHID_FDO Fdo
    )
{
    Trace("====>\n");

    ASSERT3U(KeGetCurrentIrql(), ==, DISPATCH_LEVEL);

    FrontendDisable(Fdo->Frontend);

    DEBUG(Deregister, Fdo->DebugInterface, Fdo->DebugCallback);
    Fdo->DebugCallback = NULL;

    DEBUG(Release, Fdo->DebugInterface);

    Trace("<====\n");
}

static DECLSPEC_NOINLINE VOID
FdoSuspendCallback(
    IN  PVOID   Argument
    )
{
    PXENHID_FDO Fdo = Argument;
    NTSTATUS    status;

    __FdoD0ToD3(Fdo);

    status = __FdoD3ToD0(Fdo);
    ASSERT(NT_SUCCESS(status));
}

static DECLSPEC_NOINLINE NTSTATUS
FdoD3ToD0(
    IN  PXENHID_FDO Fdo
    )
{
    POWER_STATE     PowerState;
    KIRQL           Irql;
    NTSTATUS        status;

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);
    ASSERT3U(__FdoGetDevicePowerState(Fdo), ==, PowerDeviceD3);

    KeRaiseIrql(DISPATCH_LEVEL, &Irql);

    status = __FdoD3ToD0(Fdo);
    if (!NT_SUCCESS(status))
        goto fail1;

    SUSPEND(Acquire, Fdo->SuspendInterface);
    
    status = SUSPEND(Register,
                     Fdo->SuspendInterface,
                     SUSPEND_CALLBACK_LATE,
                     FdoSuspendCallback,
                     Fdo,
                     &Fdo->SuspendCallback);
    if (!NT_SUCCESS(status))
        goto fail2;

    KeLowerIrql(Irql);

    __FdoSetDevicePowerState(Fdo, PowerDeviceD0);

    PowerState.DeviceState = PowerDeviceD0;
    PoSetPowerState(Fdo->DeviceObject,
                    DevicePowerState,
                    PowerState);

    return STATUS_SUCCESS;

fail2:
    Error("fail2\n");

    SUSPEND(Release, Fdo->SuspendInterface);

    __FdoD0ToD3(Fdo);

fail1:
    Error("fail1 (%08x)\n", status);

    KeLowerIrql(Irql);

    return status;
}

static DECLSPEC_NOINLINE VOID
FdoD0ToD3(
    IN  PXENHID_FDO Fdo
    )
{
    POWER_STATE     PowerState;
    KIRQL           Irql;

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);
    ASSERT3U(__FdoGetDevicePowerState(Fdo), ==, PowerDeviceD0);

    PowerState.DeviceState = PowerDeviceD3;
    PoSetPowerState(Fdo->DeviceObject,
                    DevicePowerState,
                    PowerState);

    __FdoSetDevicePowerState(Fdo, PowerDeviceD3);

    KeRaiseIrql(DISPATCH_LEVEL, &Irql);

    SUSPEND(Deregister,
            Fdo->SuspendInterface,
            Fdo->SuspendCallback);
    Fdo->SuspendCallback = NULL;

    SUSPEND(Release, Fdo->SuspendInterface);

    __FdoD0ToD3(Fdo);

    KeLowerIrql(Irql);
}

static DECLSPEC_NOINLINE NTSTATUS
FdoStartDevice(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    NTSTATUS            status;

    status = FdoForwardIrpSynchronously(Fdo, Irp);
    if (!NT_SUCCESS(status))
        goto fail1;

    __FdoSetSystemPowerState(Fdo, PowerSystemWorking);

    status = FdoD3ToD0(Fdo);
    if (!NT_SUCCESS(status))
        goto fail2;

    FdoResumeData(Fdo);

    __FdoSetDevicePnpState(Fdo, Started);

    status = Irp->IoStatus.Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;

fail2:
    Error("fail2\n");

    __FdoSetSystemPowerState(Fdo, PowerSystemShutdown);

fail1:
    Error("fail1 (%08x)\n", status);

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoQueryStopDevice(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    __FdoSetDevicePnpState(Fdo, StopPending);
    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoCancelStopDevice(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    Irp->IoStatus.Status = STATUS_SUCCESS;

    __FdoRestoreDevicePnpState(Fdo, StopPending);

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoStopDevice(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    FdoD0ToD3(Fdo);

    __FdoSetSystemPowerState(Fdo, PowerSystemShutdown);

    FdoPauseData(Fdo);

    __FdoSetDevicePnpState(Fdo, Stopped);
    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoQueryRemoveDevice(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    __FdoSetDevicePnpState(Fdo, RemovePending);
    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoCancelRemoveDevice(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    __FdoRestoreDevicePnpState(Fdo, RemovePending);

    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoSurpriseRemoval(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    __FdoSetDevicePnpState(Fdo, SurpriseRemovePending);

    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoRemoveDevice(
    IN  PXENHID_FDO Fdo,
    IN  PIRP        Irp
    )
{
    NTSTATUS        status;

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);

    if (__FdoGetDevicePowerState(Fdo) != PowerDeviceD0)
        goto done;

    FdoPauseData(Fdo);

    FdoD0ToD3(Fdo);

    __FdoSetSystemPowerState(Fdo, PowerSystemShutdown);

done:
    __FdoSetDevicePnpState(Fdo, Deleted);

    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    FdoDestroy(Fdo);

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoDispatchPnp(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  StackLocation;
    UCHAR               MinorFunction;
    NTSTATUS            status;

    StackLocation = IoGetCurrentIrpStackLocation(Irp);
    MinorFunction = StackLocation->MinorFunction;

    Trace("====> (%02x:%s)\n",
          MinorFunction, 
          PnpMinorFunctionName(MinorFunction)); 

    switch (StackLocation->MinorFunction) {
    case IRP_MN_START_DEVICE:
        status = FdoStartDevice(Fdo, Irp);
        break;

    case IRP_MN_QUERY_STOP_DEVICE:
        status = FdoQueryStopDevice(Fdo, Irp);
        break;

    case IRP_MN_CANCEL_STOP_DEVICE:
        status = FdoCancelStopDevice(Fdo, Irp);
        break;

    case IRP_MN_STOP_DEVICE:
        status = FdoStopDevice(Fdo, Irp);
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
        status = FdoQueryRemoveDevice(Fdo, Irp);
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        status = FdoSurpriseRemoval(Fdo, Irp);
        break;

    case IRP_MN_REMOVE_DEVICE:
        status = FdoRemoveDevice(Fdo, Irp);
        break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        status = FdoCancelRemoveDevice(Fdo, Irp);
        break;

    default:
        IoSkipCurrentIrpStackLocation(Irp);
        status = IoCallDriver(Fdo->LowerDeviceObject, Irp);
        break;
    }

    Trace("<==== (%02x:%s)(%08x)\n",
          MinorFunction, 
          PnpMinorFunctionName(MinorFunction),
          status); 

    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoDispatchPower(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  StackLocation;
    UCHAR               MinorFunction;
    POWER_STATE_TYPE    PowerType;
    POWER_ACTION        PowerAction;
    POWER_STATE         PowerState;
    NTSTATUS            status;

    StackLocation = IoGetCurrentIrpStackLocation(Irp);
    MinorFunction = StackLocation->MinorFunction;

    if (MinorFunction != IRP_MN_SET_POWER) {
        goto done;
    }

    PowerType = StackLocation->Parameters.Power.Type;
    PowerAction = StackLocation->Parameters.Power.ShutdownType;
    PowerState = StackLocation->Parameters.Power.State;

    if (PowerAction >= PowerActionShutdown) {
        goto done;
    }

    switch (PowerType) {
    case DevicePowerState:
        if (PowerState.DeviceState == PowerDeviceD0)
            FdoResumeData(Fdo);
        else
            FdoPauseData(Fdo);
        break;

    case SystemPowerState:
        if (PowerState.SystemState == PowerSystemShutdown)
            FdoPauseData(Fdo);
        break;

    default:
        break;
    }

done:
    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);
    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoDispatchControl(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  StackLocation;
    ULONG               ControlCode;
    PVOID               Buffer;
    ULONG               InputLength;
    ULONG               OutputLength;
    ULONG_PTR           Information;
    NTSTATUS            status;

    StackLocation = IoGetCurrentIrpStackLocation(Irp);
    Buffer = Irp->UserBuffer;
    ControlCode = StackLocation->Parameters.DeviceIoControl.IoControlCode;
    InputLength = StackLocation->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = StackLocation->Parameters.DeviceIoControl.OutputBufferLength;
    Information = 0;

    switch (ControlCode) {
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        status = FrontendGetDeviceAttributes(Fdo->Frontend, Buffer, OutputLength, &Information);
        break;
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        status = FrontendGetDeviceDescriptor(Fdo->Frontend, Buffer, OutputLength, &Information);
        break;
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
        status = FrontendGetReportDescriptor(Fdo->Frontend, Buffer, OutputLength, &Information);
        break;
    case IOCTL_HID_GET_FEATURE:
        status = FrontendGetFeature(Fdo->Frontend, Buffer, OutputLength, &Information);
        break;
    case IOCTL_HID_SET_FEATURE:
        status = FrontendSetFeature(Fdo->Frontend, Buffer, OutputLength);
        break;
    case IOCTL_HID_WRITE_REPORT:
        status = FrontendWriteReport(Fdo->Frontend, Buffer, OutputLength);
        break;
    case IOCTL_HID_READ_REPORT:
        status = __FdoCache(Fdo, Irp);
        if (status == STATUS_PENDING)
            status = FrontendReadReport(Fdo->Frontend);
        break;

    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    if (status == STATUS_PENDING) {
        IoMarkIrpPending(Irp);
    } else {
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = Information;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    return status;
}

static DECLSPEC_NOINLINE NTSTATUS
FdoDispatchDefault(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    NTSTATUS            status;

    IoSkipCurrentIrpStackLocation(Irp);
    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);

    return status;
}

NTSTATUS
FdoDispatch(
    IN  PXENHID_FDO     Fdo,
    IN  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  StackLocation;
    NTSTATUS            status;

    StackLocation = IoGetCurrentIrpStackLocation(Irp);

    switch (StackLocation->MajorFunction) {
    case IRP_MJ_PNP:
        status = FdoDispatchPnp(Fdo, Irp);
        break;

    case IRP_MJ_POWER:
        status = FdoDispatchPower(Fdo, Irp);
        break;

    case IRP_MJ_DEVICE_CONTROL:
        status = FdoDispatchControl(Fdo, Irp);
        break;

    default:
        status = FdoDispatchDefault(Fdo, Irp);
        break;
    }

    return status;
}

static NTSTATUS
FdoQueryInterface(
    IN  PXENHID_FDO     Fdo,
    IN  const GUID      *ItfGuid,
    IN  USHORT          ItfVersion,
    OUT PVOID*          pInterface
    )
{
    KEVENT              Event;
    IO_STATUS_BLOCK     StatusBlock;
    PIRP                Irp;
    PIO_STACK_LOCATION  StackLocation;
    INTERFACE           Interface;
    NTSTATUS            status;

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    RtlZeroMemory(&StatusBlock, sizeof(IO_STATUS_BLOCK));
    RtlZeroMemory(&Interface, sizeof(INTERFACE));

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       Fdo->LowerDeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &StatusBlock);

    status = STATUS_UNSUCCESSFUL;
    if (Irp == NULL)
        goto fail1;

    StackLocation = IoGetNextIrpStackLocation(Irp);
    StackLocation->MinorFunction = IRP_MN_QUERY_INTERFACE;

    StackLocation->Parameters.QueryInterface.InterfaceType = ItfGuid;
    StackLocation->Parameters.QueryInterface.Size = sizeof (INTERFACE);
    StackLocation->Parameters.QueryInterface.Version = ItfVersion;
    StackLocation->Parameters.QueryInterface.Interface = &Interface;
    
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    status = IoCallDriver(Fdo->LowerDeviceObject, Irp);
    if (status == STATUS_PENDING) {
        (VOID) KeWaitForSingleObject(&Event,
                                     Executive,
                                     KernelMode,
                                     FALSE,
                                     NULL);
        status = StatusBlock.Status;
    }

    if (!NT_SUCCESS(status))
        goto fail2;

    status = STATUS_INVALID_PARAMETER;
    if (Interface.Version != ItfVersion)
        goto fail3;

    *pInterface = Interface.Context;

    return STATUS_SUCCESS;

fail3:
    Error("fail3\n");

fail2:
    Error("fail2\n");

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

NTSTATUS
FdoCreate(
    IN  PDEVICE_OBJECT      DeviceObject
    )
{
    PXENHID_FDO             Fdo = DriverGetFdo(DeviceObject);
    NTSTATUS                status;

    RtlZeroMemory(Fdo, sizeof (XENHID_FDO));

    Fdo->DeviceObject = DeviceObject;
    Fdo->LowerDeviceObject = DriverGetLowerDeviceObject(DeviceObject);
    Fdo->DevicePnpState = Added;
    Fdo->SystemPowerState = PowerSystemShutdown;
    Fdo->DevicePowerState = PowerDeviceD3;

    status = FdoQueryInterface(Fdo,
                                &GUID_STORE_INTERFACE,
                                STORE_INTERFACE_VERSION,
                                (PVOID*)&Fdo->StoreInterface);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = FdoQueryInterface(Fdo,
                                &GUID_DEBUG_INTERFACE,
                                DEBUG_INTERFACE_VERSION,
                                (PVOID*)&Fdo->DebugInterface);
    if (!NT_SUCCESS(status))
        goto fail2;

    status = FdoQueryInterface(Fdo,
                                &GUID_EVTCHN_INTERFACE,
                                EVTCHN_INTERFACE_VERSION,
                                (PVOID*)&Fdo->EvtchnInterface);
    if (!NT_SUCCESS(status))
        goto fail3;

    status = FdoQueryInterface(Fdo,
                                &GUID_GNTTAB_INTERFACE,
                                GNTTAB_INTERFACE_VERSION,
                                (PVOID*)&Fdo->GnttabInterface);
    if (!NT_SUCCESS(status))
        goto fail4;

    status = FdoQueryInterface(Fdo,
                                &GUID_SUSPEND_INTERFACE,
                                SUSPEND_INTERFACE_VERSION,
                                (PVOID*)&Fdo->SuspendInterface);
    if (!NT_SUCCESS(status))
        goto fail5;

    status = FrontendCreate(Fdo, &Fdo->Frontend);
    if (!NT_SUCCESS(status))
        goto fail6;

    KeInitializeSpinLock(&Fdo->Lock);

    Info("%p (%s)\n",
         DeviceObject,
         __FdoGetStorePath(Fdo));

     return STATUS_SUCCESS;

fail6:
    Error("fail6\n");

    Fdo->SuspendInterface = NULL;

fail5:
    Error("fail5\n");

    Fdo->GnttabInterface = NULL;

fail4:
    Error("fail4\n");

    Fdo->EvtchnInterface = NULL;

fail3:
    Error("fail3\n");

    Fdo->DebugInterface = NULL;

fail2:
    Error("fail2\n");

    Fdo->StoreInterface = NULL;

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

VOID
FdoDestroy(
    IN  PXENHID_FDO     Fdo
    )
{
    PDEVICE_OBJECT  DeviceObject = Fdo->DeviceObject;

    ASSERT3U(__FdoGetDevicePnpState(Fdo), ==, Deleted);

    Info("0x%p (%s)\n",
         DeviceObject,
         __FdoGetStorePath(Fdo));

    FrontendDestroy(Fdo->Frontend);
    Fdo->Frontend = NULL;

    Fdo->SuspendInterface = NULL;
    Fdo->GnttabInterface = NULL;
    Fdo->EvtchnInterface = NULL;
    Fdo->DebugInterface = NULL;
    Fdo->StoreInterface = NULL;

    RtlZeroMemory(&Fdo->Lock, sizeof(KSPIN_LOCK));

    Fdo->LowerDeviceObject = NULL;
    Fdo->DeviceObject = NULL;
}

PXENBUS_STORE_INTERFACE
FdoStoreInterface(
    IN  PXENHID_FDO         Fdo
    )
{
    return Fdo->StoreInterface;
}

PXENBUS_EVTCHN_INTERFACE
FdoEvtchnInterface(
    IN  PXENHID_FDO         Fdo
    )
{
    return Fdo->EvtchnInterface;
}

PXENBUS_GNTTAB_INTERFACE
FdoGnttabInterface(
    IN  PXENHID_FDO         Fdo
    )
{
    return Fdo->GnttabInterface;
}

