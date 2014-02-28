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

#include "frontend.h"
#include "driver.h"
#include "fdo.h"
#include "operations.h"
#include "vkbd.h"
#include "dbg_print.h"
#include "assert.h"
#include <xen.h>
#include <stdlib.h>

struct _XENHID_FRONTEND {
    PXENHID_FDO             Fdo;
    BOOLEAN                 Connected;

    PCHAR                   BackendPath;
    USHORT                  BackendDomain;

    XENHID_OPERATIONS       Operations;
    PXENHID_CONTEXT         Context;

    PXENBUS_STORE_INTERFACE StoreInterface;
};

#define FRONTEND_POOL_TAG   'DIHX'

static FORCEINLINE PVOID
__FrontendAllocate(
    IN  ULONG                   Size
    )
{
    PVOID   Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, FRONTEND_POOL_TAG);
    if (Buffer)
        RtlZeroMemory(Buffer, Size);

    return Buffer;
}

static FORCEINLINE VOID
__FrontendFree(
    IN  PVOID                   Buffer
    )
{
    ExFreePoolWithTag(Buffer, FRONTEND_POOL_TAG);
}

static FORCEINLINE PCHAR
XenbusStateName(
    IN  XenbusState             State
    )
{
    switch (State) {
    case XenbusStateUnknown:        return "Unknown";
    case XenbusStateInitialising:   return "Initialising";
    case XenbusStateInitWait:       return "InitWait";
    case XenbusStateInitialised:    return "Initialised";
    case XenbusStateConnected:      return "Connected";
    case XenbusStateClosing:        return "Closing";
    case XenbusStateClosed:         return "Closed";
    case XenbusStateReconfiguring:  return "Reconfiguring";
    case XenbusStateReconfigured:   return "Reconfigured";
    default:                        return "<UNKNOWN>";
    }
}

NTSTATUS
FrontendCreate(
    IN  PXENHID_FDO             Fdo,
    OUT PXENHID_FRONTEND*       Frontend
    )
{
    NTSTATUS    status;

    status = STATUS_NO_MEMORY;
    *Frontend = __FrontendAllocate(sizeof(XENHID_FRONTEND));
    if (*Frontend == NULL)
        goto fail1;

    (*Frontend)->Fdo = Fdo;
    (*Frontend)->StoreInterface = FdoStoreInterface(Fdo);

    return STATUS_SUCCESS;

fail1:
    Error("fail1 (%08x)\n", status);
    return status;
}

VOID
FrontendDestroy(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    Frontend->Fdo = NULL;
    Frontend->Connected = FALSE;
    Frontend->StoreInterface = NULL;

    Trace("(%s)-%u\n", Frontend->BackendPath, Frontend->BackendDomain);

    if (Frontend->BackendPath)
        __FrontendFree(Frontend->BackendPath);
    Frontend->BackendPath = NULL;
    Frontend->BackendDomain = 0;

    __FrontendFree(Frontend);
}

static FORCEINLINE NTSTATUS
__FrontendSetState(
    IN  PXENHID_FRONTEND        Frontend,
    IN  XenbusState             State
    )
{
    return STORE(Printf, 
                Frontend->StoreInterface, 
                NULL, 
                FdoGetStorePath(Frontend->Fdo), 
                "state", 
                "%u", 
                (ULONG)State);
}

static NTSTATUS
__FrontendWaitState(
    IN  PXENHID_FRONTEND        Frontend,
    IN OUT XenbusState*         State
    )
{
    KEVENT                  Event;
    PXENBUS_STORE_WATCH     Watch;
    LARGE_INTEGER           Start;
    ULONGLONG               TimeDelta;
    LARGE_INTEGER           Timeout;
    XenbusState             Old = *State;
    NTSTATUS                status;

    Trace("%s: ====> (%s)\n", Frontend->BackendPath, XenbusStateName(*State));

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    status = STORE(Watch,
                   Frontend->StoreInterface,
                   Frontend->BackendPath,
                   "state",
                   &Event,
                   &Watch);
    if (!NT_SUCCESS(status))
        goto fail1;

    KeQuerySystemTime(&Start);
    TimeDelta = 0;

    Timeout.QuadPart = 0;

    while (*State == Old && TimeDelta < 120000) {
        ULONG           Attempt;
        PCHAR           Buffer;
        LARGE_INTEGER   Now;

        Attempt = 0;
        while (++Attempt < 1000) {
            status = KeWaitForSingleObject(&Event,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           &Timeout);
            if (status != STATUS_TIMEOUT)
                break;

            // We are waiting for a watch event at DISPATCH_LEVEL so
            // it is our responsibility to poll the store ring.
            STORE(Poll,
                  Frontend->StoreInterface);

            KeStallExecutionProcessor(1000);   // 1ms
        }

        KeClearEvent(&Event);

        status = STORE(Read,
                       Frontend->StoreInterface,
                       NULL,
                       Frontend->BackendPath,
                       "state",
                       &Buffer);
        if (!NT_SUCCESS(status))
            goto fail2;

        *State = (XenbusState)strtol(Buffer, NULL, 10);

        STORE(Free,
              Frontend->StoreInterface,
              Buffer);

        KeQuerySystemTime(&Now);

        TimeDelta = (Now.QuadPart - Start.QuadPart) / 10000ull;
    }

    status = STATUS_UNSUCCESSFUL;
    if (*State == Old)
        goto fail3;

    (VOID) STORE(Unwatch,
                 Frontend->StoreInterface,
                 Watch);

    Trace("%s: <==== (%s)\n", Frontend->BackendPath, XenbusStateName(*State));

    return STATUS_SUCCESS;

fail3:
    Error("fail3\n");

fail2:
    Error("fail2\n");

    (VOID) STORE(Unwatch,
                 Frontend->StoreInterface,
                 Watch);

fail1:
    Error("fail1 (%08x)\n", status);
                   
    return status;
}

static NTSTATUS
__FrontendUpdatePaths(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    NTSTATUS        status;
    PCHAR           Buffer;
    ULONG           Length;

    status = STORE(Read, Frontend->StoreInterface, NULL,
                    FdoGetStorePath(Frontend->Fdo), "backend", &Buffer);
    if (!NT_SUCCESS(status))
        goto fail1;

    Length = (ULONG)strlen(Buffer);
    if (Frontend->BackendPath)
        __FrontendFree(Frontend->BackendPath);
    Frontend->BackendPath = __FrontendAllocate((Length + 1) * sizeof(CHAR));
    if (Frontend->BackendPath == NULL)
        goto fail2;

    RtlCopyMemory(Frontend->BackendPath, Buffer, Length * sizeof(CHAR));
    STORE(Free, Frontend->StoreInterface, Buffer);

    status = STORE(Read, Frontend->StoreInterface, NULL,
                    FdoGetStorePath(Frontend->Fdo), "backend-id", &Buffer);
    if (!NT_SUCCESS(status))
        goto fail3;

    Frontend->BackendDomain = (USHORT)strtoul(Buffer, NULL, 10);
    STORE(Free, Frontend->StoreInterface, Buffer);

    return STATUS_SUCCESS;

fail3:
    Error("fail3\n");
fail2:
    Error("fail2\n");
fail1:
    Error("fail1 (%08x)\n", status);
    return status;
}

static NTSTATUS
__FrontendClose(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    NTSTATUS    status;
    XenbusState State = XenbusStateUnknown;

    status = __FrontendUpdatePaths(Frontend);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = __FrontendSetState(Frontend, XenbusStateClosing);
    if (!NT_SUCCESS(status))
        goto fail2;

    do {
        status = __FrontendWaitState(Frontend, &State);
        if (!NT_SUCCESS(status))
            goto fail3;
    } while (State != XenbusStateClosing && State != XenbusStateClosed);

    status = __FrontendSetState(Frontend, XenbusStateClosed);
    if (!NT_SUCCESS(status))
        goto fail4;

    do {
        status = __FrontendWaitState(Frontend, &State);
        if (!NT_SUCCESS(status))
            goto fail5;
    } while (State != XenbusStateClosed);

    return STATUS_SUCCESS;

fail5:
    Error("fail5\n");
fail4:
    Error("fail4\n");
fail3:
    Error("fail3\n");
fail2:
    Error("fail2\n");
fail1:
    Error("fail1 (%08x)\n", status);
    return status;
}

static NTSTATUS
__FrontendConnect(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    NTSTATUS    status;
    XenbusState State = XenbusStateUnknown;

    status = __FrontendUpdatePaths(Frontend);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = Frontend->Operations.Connect(Frontend->Context);
    if (!NT_SUCCESS(status))
        goto fail2;

    for (;;) {
        PXENBUS_STORE_TRANSACTION   Transaction;

        status = STORE(TransactionStart, 
                        Frontend->StoreInterface, 
                        &Transaction);
        if (!NT_SUCCESS(status))
            break;

        status = Frontend->Operations.WriteStore(Frontend->Context, Transaction);
        if (!NT_SUCCESS(status))
            goto abort;

        status = STORE(TransactionEnd, Frontend->StoreInterface, Transaction, TRUE);
        if (status == STATUS_RETRY)
            continue;
        break;

abort:
        (VOID) STORE(TransactionEnd, Frontend->StoreInterface, Transaction, FALSE);
    }
    if (!NT_SUCCESS(status))
        goto fail3;

    status = __FrontendSetState(Frontend, XenbusStateConnected);
    if (!NT_SUCCESS(status))
        goto fail4;

    status = __FrontendWaitState(Frontend, &State);
    if (!NT_SUCCESS(status))
        goto fail5;

    status = STATUS_INVALID_PARAMETER;
    if (State != XenbusStateConnected)
        goto fail6;
    
    return STATUS_SUCCESS;

fail6:
    Error("fail6\n");
fail5:
    Error("fail5\n");
fail4:
    Error("fail4\n");
fail3:
    Error("fail3\n");
    Frontend->Operations.Disconnect(Frontend->Context);
fail2:
    Error("fail2\n");
fail1:
    Error("fail1 (%08x)\n", status);
    return status;
}

NTSTATUS
FrontendEnable(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    PCHAR       Buffer;
    ULONG       Protocol;
    NTSTATUS    status;

    ASSERT(Frontend->Connected == FALSE);

    STORE(Acquire, Frontend->StoreInterface);

    status = __FrontendClose(Frontend);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = STORE(Read, 
                    Frontend->StoreInterface, 
                    NULL, 
                    FdoGetStorePath(Frontend->Fdo),
                    "protocol",
                    &Buffer);
    if (NT_SUCCESS(status)) {
        Protocol = (ULONG)strtoul(Buffer, NULL, 10);
        STORE(Free, Frontend->StoreInterface, Buffer);
    } else {
        Protocol = 0;
    }
    switch (Protocol) {
    case 0:
        // VKBD
        status = VkbdInitialize(&Frontend->Operations);
        break;

    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    if (!NT_SUCCESS(status))
        goto fail2;
    
    status = Frontend->Operations.Create(Frontend, &Frontend->Context);
    if (!NT_SUCCESS(status))
        goto fail3;

    status = __FrontendConnect(Frontend);
    if (!NT_SUCCESS(status))
        goto fail4;

    Frontend->Connected = TRUE;
    return STATUS_SUCCESS;

fail4:
    Error("fail4\n");
    Frontend->Operations.Destroy(Frontend->Context);
    Frontend->Context = NULL;
fail3:
    Error("fail3\n");
    RtlZeroMemory(&Frontend->Operations, sizeof(XENHID_OPERATIONS));
fail2:
    Error("fail2\n");
fail1:
    Error("fail1 (%08x)\n", status);
    STORE(Release, Frontend->StoreInterface);
    return status;
}

VOID
FrontendDisable(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    ASSERT(Frontend->Connected == TRUE);

    Frontend->Operations.Disconnect(Frontend->Context);
    
    Frontend->Operations.Destroy(Frontend->Context);
    Frontend->Context = NULL;
    
    RtlZeroMemory(&Frontend->Operations, sizeof(XENHID_OPERATIONS));

    (VOID) __FrontendClose(Frontend);

    STORE(Release, Frontend->StoreInterface);

    Frontend->Connected = FALSE;
}

NTSTATUS
FrontendGetDeviceAttributes(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.GetDeviceAttributes(Frontend->Context, Buffer, Length, Information);
}

NTSTATUS
FrontendGetDeviceDescriptor(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.GetDeviceDescriptor(Frontend->Context, Buffer, Length, Information);
}

NTSTATUS
FrontendGetReportDescriptor(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.GetReportDescriptor(Frontend->Context, Buffer, Length, Information);
}

NTSTATUS
FrontendGetFeature(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.GetFeature(Frontend->Context, Buffer, Length, Information);
}

NTSTATUS
FrontendSetFeature(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.SetFeature(Frontend->Context, Buffer, Length);
}

NTSTATUS
FrontendWriteReport(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.WriteReport(Frontend->Context, Buffer, Length);
}

NTSTATUS
FrontendReadReport(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    if (!Frontend->Connected)
        return STATUS_DEVICE_NOT_READY;

    ASSERT3P(Frontend->Context, !=, NULL);

    return Frontend->Operations.ReadReport(Frontend->Context);
}

NTSTATUS
FrontendCompleteRead(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length
    )
{
    return FdoCompleteRead(Frontend->Fdo, Buffer, Length);
}

PXENHID_FDO
FrontendGetFdo(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    return Frontend->Fdo;
}

USHORT
FrontendGetBackendDomain(
    IN  PXENHID_FRONTEND        Frontend
    )
{
    return Frontend->BackendDomain;
}
