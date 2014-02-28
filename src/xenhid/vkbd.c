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

#include "vkbd.h"
#include "operations.h"
#include "frontend.h"
#include "fdo.h"
#include "reportdescr.h"
#include <store_interface.h>
#include <evtchn_interface.h>
#include <gnttab_interface.h>
#include <hidport.h>
#include <xen.h>
#include "dbg_print.h"
#include "assert.h"

typedef struct _XENHID_KEYBOARD {
    UCHAR   ReportId; // = 1
    UCHAR   Modifiers;
    UCHAR   Keys[6];
} XENHID_KEYBOARD, *PXENHID_KEYBOARD;

typedef struct _XENHID_MOUSE {
    UCHAR   ReportId; // = 2
    UCHAR   Buttons;
    USHORT  X;
    USHORT  Y;
    CHAR    Z;
} XENHID_MOUSE, *PXENHID_MOUSE;

typedef struct _XENHID_VKBD {
    PXENHID_FRONTEND            Frontend;
    KDPC                        Dpc;
    ULONG                       NumInts;
    ULONG                       NumEvts;

    struct xenkbd_page*         Shared;
    PXENBUS_EVTCHN_DESCRIPTOR   Evtchn;
    ULONG                       GrantRef;
    
    XENHID_KEYBOARD             KeyState;
    XENHID_MOUSE                MouState;
    BOOLEAN                     KeyPending;
    BOOLEAN                     MouPending;
} XENHID_VKBD, *PXENHID_VKBD;

static HID_DEVICE_ATTRIBUTES 
Vkbd_DeviceAttributes = {
    sizeof(HID_DEVICE_ATTRIBUTES),
    0xBEEF,     // TODO: Assign proper VendorId
    0xFEED,     // TODO: Assign proper ProductId
    0x0101
};

static UCHAR
Vkbd_ReportDescriptor[] = {
    VKBD_REPORT_DESCRIPTOR  // #defined to the right bytes!
};

static HID_DESCRIPTOR
Vkbd_DeviceDescriptor = {
    sizeof (HID_DESCRIPTOR),
    0x09,       // TODO: check
    0x0101,
    0x00,
    0x01,
    {
        0x22,   // TODO: check
        sizeof(Vkbd_ReportDescriptor)
    }
};

#define VKBD_POOL_TAG       'DKBV'

static FORCEINLINE PVOID
__VkbdAllocate(
    IN  ULONG               Length
    )
{
    PVOID   Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Length, VKBD_POOL_TAG);
    if (Buffer)
        RtlZeroMemory(Buffer, Length);

    return Buffer;
}

static FORCEINLINE VOID
__VkbdFree(
    IN  PVOID               Buffer
    )
{
    ExFreePoolWithTag(Buffer, VKBD_POOL_TAG);
}

#define MOUSE_BUTTON        1
#define KEYBOARD_MODIFIER   2
#define KEYBOARD_KEY        3

static FORCEINLINE ULONG
__UsasgeType(
    IN  ULONG               Code,
    OUT PUCHAR              Value
    )
{
#define XENHID_KEY(_Code, _Value, _Type)        \
    case (_Code):   *Value = (_Value);  return (_Type)

    switch (Code) {
        // KEYBOARD KEYS
    XENHID_KEY( 1,      0x29,   KEYBOARD_KEY);
    XENHID_KEY( 2,      0x1E,   KEYBOARD_KEY);
    XENHID_KEY( 3,      0x1F,   KEYBOARD_KEY);
    XENHID_KEY( 4,      0x20,   KEYBOARD_KEY);
    XENHID_KEY( 5,      0x21,   KEYBOARD_KEY);
    XENHID_KEY( 6,      0x22,   KEYBOARD_KEY);
    XENHID_KEY( 7,      0x23,   KEYBOARD_KEY);
    XENHID_KEY( 8,      0x24,   KEYBOARD_KEY);
    XENHID_KEY( 9,      0x25,   KEYBOARD_KEY);
    XENHID_KEY( 10,     0x26,   KEYBOARD_KEY);
    XENHID_KEY( 11,     0x27,   KEYBOARD_KEY);
    XENHID_KEY( 12,     0x2D,   KEYBOARD_KEY);
    XENHID_KEY( 13,     0x2E,   KEYBOARD_KEY);
    XENHID_KEY( 14,     0x2A,   KEYBOARD_KEY);
    XENHID_KEY( 15,     0x2B,   KEYBOARD_KEY);
    XENHID_KEY( 16,     0x14,   KEYBOARD_KEY);
    XENHID_KEY( 17,     0x1A,   KEYBOARD_KEY);
    XENHID_KEY( 18,     0x08,   KEYBOARD_KEY);
    XENHID_KEY( 19,     0x15,   KEYBOARD_KEY);
    XENHID_KEY( 20,     0x17,   KEYBOARD_KEY);
    XENHID_KEY( 21,     0x1C,   KEYBOARD_KEY);
    XENHID_KEY( 22,     0x18,   KEYBOARD_KEY);
    XENHID_KEY( 23,     0x0C,   KEYBOARD_KEY);
    XENHID_KEY( 24,     0x12,   KEYBOARD_KEY);
    XENHID_KEY( 25,     0x13,   KEYBOARD_KEY);
    XENHID_KEY( 26,     0x2F,   KEYBOARD_KEY);
    XENHID_KEY( 27,     0x30,   KEYBOARD_KEY);
    XENHID_KEY( 28,     0x28,   KEYBOARD_KEY);
    XENHID_KEY( 29,     0xE0,   KEYBOARD_KEY);
    XENHID_KEY( 30,     0x04,   KEYBOARD_KEY);
    XENHID_KEY( 31,     0x16,   KEYBOARD_KEY);
    XENHID_KEY( 32,     0x07,   KEYBOARD_KEY);
    XENHID_KEY( 33,     0x09,   KEYBOARD_KEY);
    XENHID_KEY( 34,     0x0A,   KEYBOARD_KEY);
    XENHID_KEY( 35,     0x0B,   KEYBOARD_KEY);
    XENHID_KEY( 36,     0x0D,   KEYBOARD_KEY);
    XENHID_KEY( 37,     0x0E,   KEYBOARD_KEY);
    XENHID_KEY( 38,     0x0F,   KEYBOARD_KEY);
    XENHID_KEY( 39,     0x33,   KEYBOARD_KEY);
    XENHID_KEY( 40,     0x34,   KEYBOARD_KEY);
    XENHID_KEY( 41,     0x35,   KEYBOARD_KEY);
    XENHID_KEY( 42,     0xE1,   KEYBOARD_KEY);
    XENHID_KEY( 43,     0x31,   KEYBOARD_KEY);
    XENHID_KEY( 44,     0x1D,   KEYBOARD_KEY);
    XENHID_KEY( 45,     0x1B,   KEYBOARD_KEY);
    XENHID_KEY( 46,     0x06,   KEYBOARD_KEY);
    XENHID_KEY( 47,     0x19,   KEYBOARD_KEY);
    XENHID_KEY( 48,     0x05,   KEYBOARD_KEY);
    XENHID_KEY( 49,     0x11,   KEYBOARD_KEY);
    XENHID_KEY( 50,     0x10,   KEYBOARD_KEY);
    XENHID_KEY( 51,     0x36,   KEYBOARD_KEY);
    XENHID_KEY( 52,     0x37,   KEYBOARD_KEY);
    XENHID_KEY( 53,     0x38,   KEYBOARD_KEY);
    XENHID_KEY( 54,     0xE5,   KEYBOARD_KEY);
    XENHID_KEY( 55,     0x55,   KEYBOARD_KEY);
    XENHID_KEY( 56,     0xE2,   KEYBOARD_KEY);
    XENHID_KEY( 57,     0x2C,   KEYBOARD_KEY);
    XENHID_KEY( 58,     0x39,   KEYBOARD_KEY);
    XENHID_KEY( 59,     0x3A,   KEYBOARD_KEY);
    XENHID_KEY( 60,     0x3B,   KEYBOARD_KEY);
    XENHID_KEY( 61,     0x3C,   KEYBOARD_KEY);
    XENHID_KEY( 62,     0x3D,   KEYBOARD_KEY);
    XENHID_KEY( 63,     0x3E,   KEYBOARD_KEY);
    XENHID_KEY( 64,     0x3F,   KEYBOARD_KEY);
    XENHID_KEY( 65,     0x40,   KEYBOARD_KEY);
    XENHID_KEY( 66,     0x41,   KEYBOARD_KEY);
    XENHID_KEY( 67,     0x42,   KEYBOARD_KEY);
    XENHID_KEY( 68,     0x43,   KEYBOARD_KEY);
    XENHID_KEY( 69,     0x53,   KEYBOARD_KEY);
    XENHID_KEY( 70,     0x47,   KEYBOARD_KEY);
    XENHID_KEY( 71,     0x5F,   KEYBOARD_KEY);
    XENHID_KEY( 72,     0x60,   KEYBOARD_KEY);
    XENHID_KEY( 73,     0x61,   KEYBOARD_KEY);
    XENHID_KEY( 74,     0x56,   KEYBOARD_KEY);
    XENHID_KEY( 75,     0x5C,   KEYBOARD_KEY);
    XENHID_KEY( 76,     0x5D,   KEYBOARD_KEY);
    XENHID_KEY( 77,     0x5E,   KEYBOARD_KEY);
    XENHID_KEY( 78,     0x57,   KEYBOARD_KEY);
    XENHID_KEY( 79,     0x59,   KEYBOARD_KEY);
    XENHID_KEY( 80,     0x5A,   KEYBOARD_KEY);
    XENHID_KEY( 81,     0x5B,   KEYBOARD_KEY);
    XENHID_KEY( 82,     0x62,   KEYBOARD_KEY);
    XENHID_KEY( 83,     0x63,   KEYBOARD_KEY);
    XENHID_KEY( 85,     0x87,   KEYBOARD_KEY);
    XENHID_KEY( 86,     0x32,   KEYBOARD_KEY);
    //XENHID_KEY( 86,     0x64,   KEYBOARD_KEY);
    XENHID_KEY( 87,     0x44,   KEYBOARD_KEY);
    XENHID_KEY( 88,     0x45,   KEYBOARD_KEY);
    XENHID_KEY( 89,     0x88,   KEYBOARD_KEY);
    XENHID_KEY( 90,     0x89,   KEYBOARD_KEY);
    XENHID_KEY( 91,     0x8A,   KEYBOARD_KEY);
    XENHID_KEY( 92,     0x8B,   KEYBOARD_KEY);
    XENHID_KEY( 93,     0x8C,   KEYBOARD_KEY);
    XENHID_KEY( 94,     0x8D,   KEYBOARD_KEY);
    XENHID_KEY( 96,     0x58,   KEYBOARD_KEY);
    XENHID_KEY( 97,     0xE4,   KEYBOARD_KEY);
    XENHID_KEY( 98,     0x54,   KEYBOARD_KEY);
    XENHID_KEY( 99,     0x46,   KEYBOARD_KEY);
    XENHID_KEY( 100,    0xE6,   KEYBOARD_KEY); // 101
    XENHID_KEY( 102,    0x4A,   KEYBOARD_KEY);
    XENHID_KEY( 103,    0x52,   KEYBOARD_KEY);
    XENHID_KEY( 104,    0x4B,   KEYBOARD_KEY);
    XENHID_KEY( 105,    0x50,   KEYBOARD_KEY);
    XENHID_KEY( 106,    0x4F,   KEYBOARD_KEY);
    XENHID_KEY( 107,    0x4D,   KEYBOARD_KEY);
    XENHID_KEY( 108,    0x51,   KEYBOARD_KEY);
    XENHID_KEY( 109,    0x4E,   KEYBOARD_KEY);
    XENHID_KEY( 110,    0x49,   KEYBOARD_KEY);
    XENHID_KEY( 111,    0x4C,   KEYBOARD_KEY);
    XENHID_KEY( 113,    0x7F,   KEYBOARD_KEY);
    XENHID_KEY( 114,    0x81,   KEYBOARD_KEY);
    XENHID_KEY( 115,    0x80,   KEYBOARD_KEY);
    XENHID_KEY( 116,    0x66,   KEYBOARD_KEY);
    XENHID_KEY( 117,    0x86,   KEYBOARD_KEY);
    XENHID_KEY( 118,    0xD7,   KEYBOARD_KEY);
    XENHID_KEY( 119,    0x48,   KEYBOARD_KEY); // 120
    XENHID_KEY( 121,    0x85,   KEYBOARD_KEY);
    XENHID_KEY( 122,    0x8E,   KEYBOARD_KEY);
    XENHID_KEY( 123,    0x8F,   KEYBOARD_KEY);
    XENHID_KEY( 124,    0x90,   KEYBOARD_KEY);
    XENHID_KEY( 125,    0xE3,   KEYBOARD_KEY);
    XENHID_KEY( 126,    0xE7,   KEYBOARD_KEY);
    XENHID_KEY( 127,    0x65,   KEYBOARD_KEY); // 128, 129, 130
    XENHID_KEY( 131,    0x7A,   KEYBOARD_KEY); // 132
    XENHID_KEY( 133,    0x7C,   KEYBOARD_KEY); // 134
    XENHID_KEY( 135,    0x7D,   KEYBOARD_KEY); // 135, 246
    XENHID_KEY( 137,    0x7B,   KEYBOARD_KEY);
    XENHID_KEY( 138,    0x75,   KEYBOARD_KEY);
    XENHID_KEY( 139,    0x76,   KEYBOARD_KEY); // [140, 178]
    XENHID_KEY( 179,    0xB6,   KEYBOARD_KEY);
    XENHID_KEY( 180,    0xB7,   KEYBOARD_KEY); // 181
    XENHID_KEY( 182,    0x79,   KEYBOARD_KEY);
    XENHID_KEY( 183,    0x68,   KEYBOARD_KEY);
    XENHID_KEY( 184,    0x69,   KEYBOARD_KEY);
    XENHID_KEY( 185,    0x6A,   KEYBOARD_KEY);
    XENHID_KEY( 186,    0x6B,   KEYBOARD_KEY);
    XENHID_KEY( 187,    0x6C,   KEYBOARD_KEY);
    XENHID_KEY( 188,    0x6D,   KEYBOARD_KEY);
    XENHID_KEY( 189,    0x6E,   KEYBOARD_KEY);
    XENHID_KEY( 190,    0x6F,   KEYBOARD_KEY);
    XENHID_KEY( 191,    0x70,   KEYBOARD_KEY);
    XENHID_KEY( 192,    0x71,   KEYBOARD_KEY);
    XENHID_KEY( 193,    0x72,   KEYBOARD_KEY);
    XENHID_KEY( 194,    0x73,   KEYBOARD_KEY);
        // KEYBOARD MODIFIERS
    XENHID_KEY( 0xE0,   0x01,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE1,   0x02,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE2,   0x04,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE3,   0x08,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE4,   0x10,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE5,   0x20,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE6,   0x40,   KEYBOARD_MODIFIER);
    XENHID_KEY( 0xE7,   0x80,   KEYBOARD_MODIFIER);
        // MOUSE
    XENHID_KEY( 0x110,  0x01,   MOUSE_BUTTON);
    XENHID_KEY( 0x111,  0x02,   MOUSE_BUTTON);
    XENHID_KEY( 0x112,  0x04,   MOUSE_BUTTON);
    XENHID_KEY( 0x113,  0x08,   MOUSE_BUTTON);
    XENHID_KEY( 0x114,  0x10,   MOUSE_BUTTON);

    default:    return 0;
    }

#undef XENHID_KEY
}

static FORCEINLINE BOOLEAN
__UpdateBit(
    IN  PUCHAR              Bits,
    IN  UCHAR               Bit,
    IN  UCHAR               Pressed
    )
{
    if (Pressed) {
        if (*Bits & Bit)
            return FALSE; // no change
        *Bits |= Bit;
        return TRUE;
    } else {
        if ((*Bits & Bit) == 0)
            return FALSE; // no change
        *Bits &= ~Bit;
        return TRUE;
    }
}

static FORCEINLINE BOOLEAN
__UpdateArray(
    IN  PUCHAR              Array,
    IN  ULONG               Size,
    IN  UCHAR               Value,
    IN  UCHAR               Pressed
    )
{
    ULONG   Index;
    if (Pressed) {
        for (Index = 0; Index < Size; ++Index) {
            if (Array[Index] == Value)
                return FALSE; // no change
            if (Array[Index] == 0) {
                Array[Index] = Value;
                return TRUE;
            }
        }
        Array[Size - 1] = Value;
        return TRUE;
    } else {
        for (Index = 0; Index < Size; ++Index) {
            if (Array[Index] == Value) {
                for (; Index < Size - 1; ++Index) {
                    Array[Index] = Array[Index + 1];
                }
                Array[Size - 1] = 0;
                return TRUE;
            }
        }
        return FALSE; // no change
    }
}

static FORCEINLINE VOID
__Complete(
    IN  PXENHID_FDO         Fdo,
    IN  PBOOLEAN            Pending,
    IN  PVOID               Buffer,
    IN  ULONG               Length
    )
{
    NTSTATUS    status;

    status = FdoCompleteRead(Fdo, Buffer, Length);
    if (!NT_SUCCESS(status))
        *Pending = TRUE;
}

static VOID
__UpdateKeyState(
    IN  PXENHID_VKBD        Vkbd,
    IN  UCHAR               Pressed,
    IN  ULONG               Code
    )
{
    UCHAR   Value;
    
    switch (__UsasgeType(Code, &Value)) {
    case MOUSE_BUTTON:
        if (!__UpdateBit(&Vkbd->MouState.Buttons, Value, Pressed))
            return; // no changes

        __Complete(FrontendGetFdo(Vkbd->Frontend), &Vkbd->MouPending, &Vkbd->MouState, sizeof(XENHID_MOUSE));
        break;

    case KEYBOARD_MODIFIER:
        if (!__UpdateBit(&Vkbd->KeyState.Modifiers, Value, Pressed))
            return; // no changes

        __Complete(FrontendGetFdo(Vkbd->Frontend), &Vkbd->KeyPending, &Vkbd->KeyState, sizeof(XENHID_KEYBOARD));
        break;

    case KEYBOARD_KEY:
        if (!__UpdateArray(Vkbd->KeyState.Keys, 6, Value, Pressed))
            return; // no changes

        __Complete(FrontendGetFdo(Vkbd->Frontend), &Vkbd->KeyPending, &Vkbd->KeyState, sizeof(XENHID_KEYBOARD));
        break;

    default:
        break;
    }
}

static FORCEINLINE LONG
__Limit(
    IN  LONG                Val,
    IN  LONG                Min,
    IN  LONG                Max
    )
{
    if (Val < Min)  return Min;
    if (Val > Max)  return Max;
                    return Val;
}

static VOID
__UpdateMouState(
    IN  PXENHID_VKBD        Vkbd,
    IN  LONG                X,
    IN  LONG                Y,
    IN  LONG                Z
    )
{
    NTSTATUS    status;
    USHORT      x = (USHORT)__Limit(X, 0, 32767);
    USHORT      y = (USHORT)__Limit(Y, 0, 32767);
    CHAR        z = (CHAR)  __Limit(Z, -127, 127);

    if (x == Vkbd->MouState.X &&
        y == Vkbd->MouState.Y &&
        z == Vkbd->MouState.Z)
        return; // no changes

    Vkbd->MouState.X = x;
    Vkbd->MouState.Y = y;
    Vkbd->MouState.Z = z;

    status = FdoCompleteRead(FrontendGetFdo(Vkbd->Frontend), &Vkbd->MouState, sizeof(XENHID_MOUSE));
    if (!NT_SUCCESS(status))
        Vkbd->MouPending = TRUE;
}

static VOID
VkbdEvent(
    IN  PXENHID_VKBD        Vkbd,
    IN  union xenkbd_in_event*  Event
    )
{
    switch (Event->type) {
    case XENKBD_TYPE_KEY:
        __UpdateKeyState(Vkbd, Event->key.pressed, Event->key.keycode);
        break;
    case XENKBD_TYPE_POS:
        __UpdateMouState(Vkbd, Event->pos.abs_x, Event->pos.abs_y, Event->pos.rel_z);
        break;
    default:
        break;
    }
}

static VOID
VkbdPoll(
    IN  PXENHID_VKBD        Vkbd
    )
{
    for (;;) {
        ULONG   Cons;
        ULONG   Prod;

        KeMemoryBarrier();

        Cons = Vkbd->Shared->in_cons;
        Prod = Vkbd->Shared->in_prod;

        KeMemoryBarrier();

        if (Cons == Prod)
            break;

        while (Cons != Prod) {
            union xenkbd_in_event*  evt;

            evt = &XENKBD_IN_RING_REF(Vkbd->Shared, Cons);
            ++Cons;

            VkbdEvent(Vkbd, evt);
        }

        KeMemoryBarrier();

        Vkbd->Shared->in_cons = Cons;
    }
}

KDEFERRED_ROUTINE VkbdDpc;

VOID
VkbdDpc(
    IN  PKDPC               Dpc,
    IN  PVOID               Context,
    IN  PVOID               Argument1,
    IN  PVOID               Argument2
    )
{
    PXENHID_VKBD    Vkbd = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    VkbdPoll(Vkbd);
}

KSERVICE_ROUTINE    VkbdInterrupt;

BOOLEAN
VkbdInterrupt(
    IN  PKINTERRUPT         Interrupt,
    IN  PVOID               Context
    )
{
    PXENHID_VKBD    Vkbd = Context;

    UNREFERENCED_PARAMETER(Interrupt);

    ++Vkbd->NumInts;
    if (KeInsertQueueDpc(&Vkbd->Dpc, NULL, NULL))
        ++Vkbd->NumEvts;

    return TRUE;
}
    
static NTSTATUS
Vkbd_Create(
    IN  PXENHID_FRONTEND            Frontend,
    OUT PXENHID_CONTEXT*            Context
    )
{
    NTSTATUS        status;
    PXENHID_VKBD    Vkbd;

    status = STATUS_NO_MEMORY;
    Vkbd = __VkbdAllocate(sizeof(XENHID_VKBD));
    if (Vkbd == NULL)
        goto fail1;

    Vkbd->Frontend = Frontend;
    Vkbd->KeyState.ReportId = 1;
    Vkbd->MouState.ReportId = 2;
    KeInitializeDpc(&Vkbd->Dpc, VkbdDpc, Vkbd);

    *Context = (PXENHID_CONTEXT)Vkbd;
    return STATUS_SUCCESS;

fail1:
    Error("fail1 (%08x)\n", status);
    return status;
}

static VOID
Vkbd_Destroy(
    IN  PXENHID_CONTEXT             Context
    )
{
    PXENHID_VKBD    Vkbd = (PXENHID_VKBD)Context;

    Vkbd->Frontend = NULL;
    RtlZeroMemory(&Vkbd->KeyState, sizeof(XENHID_KEYBOARD));
    RtlZeroMemory(&Vkbd->MouState, sizeof(XENHID_MOUSE));
    RtlZeroMemory(&Vkbd->Dpc, sizeof(KDPC));
    Vkbd->NumInts = Vkbd->NumEvts = 0;

    ASSERT(IsZeroMemory(Context, sizeof(XENHID_VKBD)));
    __VkbdFree(Context);
}

static FORCEINLINE PFN_NUMBER
__Pfn(
    IN  PVOID                       Buffer
    )
{
    return (PFN_NUMBER)(ULONG_PTR)(MmGetPhysicalAddress(Buffer).QuadPart >> PAGE_SHIFT);
}

static NTSTATUS
Vkbd_Connect(
    IN  PXENHID_CONTEXT             Context
    )
{
    NTSTATUS        status;
    PXENHID_VKBD    Vkbd = (PXENHID_VKBD)Context;
    PXENHID_FDO     Fdo = FrontendGetFdo(Vkbd->Frontend);

    status = STATUS_NO_MEMORY;
    Vkbd->Shared = __VkbdAllocate(PAGE_SIZE);
    if (Vkbd->Shared == NULL)
        goto fail1;

    status = GNTTAB(Get, FdoGnttabInterface(Fdo), &Vkbd->GrantRef);
    if (!NT_SUCCESS(status))
        goto fail2;

    status = GNTTAB(PermitForeignAccess, 
                    FdoGnttabInterface(Fdo), 
                    Vkbd->GrantRef, 
                    FrontendGetBackendDomain(Vkbd->Frontend), 
                    GNTTAB_ENTRY_FULL_PAGE, 
                    __Pfn(Vkbd->Shared), 
                    FALSE);
    if (!NT_SUCCESS(status))
        goto fail3;

    status = STATUS_UNSUCCESSFUL;
    Vkbd->Evtchn = EVTCHN(Open, 
                        FdoEvtchnInterface(Fdo),
                        EVTCHN_UNBOUND,
                        VkbdInterrupt,
                        Vkbd,
                        FrontendGetBackendDomain(Vkbd->Frontend),
                        TRUE);
    if (Vkbd->Evtchn == NULL)
        goto fail4;
    
    return STATUS_SUCCESS;

fail4:
    GNTTAB(RevokeForeignAccess, FdoGnttabInterface(Fdo), Vkbd->GrantRef);
fail3:
    GNTTAB(Put, FdoGnttabInterface(Fdo), Vkbd->GrantRef);
    Vkbd->GrantRef = 0;
fail2:
    __VkbdFree(Vkbd->Shared);
    Vkbd->Shared = NULL;
fail1:
    return status;
}

static NTSTATUS
Vkbd_WriteStore(
    IN  PXENHID_CONTEXT             Context,
    IN  PXENBUS_STORE_TRANSACTION   Transaction
    )
{
    NTSTATUS        status;
    ULONG           Port;
    PXENHID_VKBD    Vkbd = (PXENHID_VKBD)Context;
    PXENHID_FDO     Fdo = FrontendGetFdo(Vkbd->Frontend);

    Port = EVTCHN(Port, FdoEvtchnInterface(Fdo), Vkbd->Evtchn);

    status = STORE(Printf, 
                    FdoStoreInterface(Fdo), 
                    Transaction,
                    FdoGetStorePath(Fdo),
                    "evtchn",
                    "%u",
                    Port);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = STORE(Printf,
                    FdoStoreInterface(Fdo),
                    Transaction,
                    FdoGetStorePath(Fdo),
                    "gnttab",
                    "%u",
                    Vkbd->GrantRef);
    if (!NT_SUCCESS(status))
        goto fail2;

    return STATUS_SUCCESS;

fail2:
fail1:
    return status;
}

static VOID 
Vkbd_Disconnect(
    IN  PXENHID_CONTEXT             Context
    )
{
    PXENHID_VKBD    Vkbd = (PXENHID_VKBD)Context;
    PXENHID_FDO     Fdo = FrontendGetFdo(Vkbd->Frontend);

    KeFlushQueuedDpcs();

    EVTCHN(Close, FdoEvtchnInterface(Fdo), Vkbd->Evtchn);
    Vkbd->Evtchn = NULL;

    GNTTAB(RevokeForeignAccess, FdoGnttabInterface(Fdo), Vkbd->GrantRef);
    GNTTAB(Put, FdoGnttabInterface(Fdo), Vkbd->GrantRef);
    Vkbd->GrantRef = 0;

    __VkbdFree(Vkbd->Shared);
    Vkbd->Shared = NULL;
}

static VOID
Vkbd_DebugCallback(
    IN  PXENHID_CONTEXT             Context, 
    IN  PXENBUS_DEBUG_INTERFACE     DebugInterface,
    IN  PXENBUS_DEBUG_CALLBACK      DebugCallback
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(DebugInterface);
    UNREFERENCED_PARAMETER(DebugCallback);
}

static NTSTATUS
Vkbd_GetDeviceAttributes(
    IN  PXENHID_CONTEXT             Context,
    IN  PVOID                       Buffer,
    IN  ULONG                       Length,
    OUT PULONG_PTR                  Information
    )
{
    UNREFERENCED_PARAMETER(Context);

    if (Length < sizeof(Vkbd_DeviceAttributes))
        return STATUS_INVALID_BUFFER_SIZE;

    RtlCopyMemory(Buffer, &Vkbd_DeviceAttributes, sizeof(Vkbd_DeviceAttributes));
    *Information = sizeof(Vkbd_DeviceAttributes);
    return STATUS_SUCCESS;
}

static NTSTATUS
Vkbd_GetDeviceDescriptor(
    IN  PXENHID_CONTEXT             Context,
    IN  PVOID                       Buffer,
    IN  ULONG                       Length,
    OUT PULONG_PTR                  Information
    )
{
    UNREFERENCED_PARAMETER(Context);

    if (Length < sizeof(Vkbd_DeviceDescriptor))
        return STATUS_INVALID_BUFFER_SIZE;

    RtlCopyMemory(Buffer, &Vkbd_DeviceDescriptor, sizeof(Vkbd_DeviceDescriptor));
    *Information = sizeof(Vkbd_DeviceDescriptor);
    return STATUS_SUCCESS;
}

static NTSTATUS
Vkbd_GetReportDescriptor(
    IN  PXENHID_CONTEXT             Context,
    IN  PVOID                       Buffer,
    IN  ULONG                       Length,
    OUT PULONG_PTR                  Information
    )
{
    UNREFERENCED_PARAMETER(Context);

    if (Length < sizeof(Vkbd_ReportDescriptor))
        return STATUS_INVALID_BUFFER_SIZE;

    RtlCopyMemory(Buffer, Vkbd_ReportDescriptor, sizeof(Vkbd_ReportDescriptor));
    *Information = sizeof(Vkbd_ReportDescriptor);
    return STATUS_SUCCESS;
}

static NTSTATUS
Vkbd_GetFeature(
    IN  PXENHID_CONTEXT             Context,
    IN  PVOID                       Buffer,
    IN  ULONG                       Length,
    OUT PULONG_PTR                  Information
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Information);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
Vkbd_SetFeature(
    IN  PXENHID_CONTEXT             Context,
    IN  PVOID                       Buffer,
    IN  ULONG                       Length
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
Vkbd_WriteReport(
    IN  PXENHID_CONTEXT             Context,
    IN  PVOID                       Buffer,
    IN  ULONG                       Length
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return STATUS_NOT_SUPPORTED;
}

static FORCEINLINE NTSTATUS
__Check(
    IN  PXENHID_FRONTEND    Frontend,
    IN  PBOOLEAN            Pending,
    IN  PVOID               State,
    IN  ULONG               Length
    )
{
    NTSTATUS    status;

    status = STATUS_PENDING;
    if (*Pending == TRUE) {
        status = FrontendCompleteRead(Frontend, State, Length);
        if (status == STATUS_SUCCESS) {
            *Pending = FALSE;
        }
    }

    return status;
}

static NTSTATUS
Vkbd_ReadReport(
    IN  PXENHID_CONTEXT             Context
    )
{
    NTSTATUS        status;
    PXENHID_VKBD    Vkbd = (PXENHID_VKBD)Context;

    status = __Check(Vkbd->Frontend,
                    &Vkbd->KeyPending,
                    &Vkbd->KeyState,
                    sizeof(XENHID_KEYBOARD));
    if (status != STATUS_PENDING)
        return status;

    status = __Check(Vkbd->Frontend,
                    &Vkbd->MouPending,
                    &Vkbd->MouState,
                    sizeof(XENHID_MOUSE));
    if (status != STATUS_PENDING)
        return status;

    return STATUS_PENDING;
}

static XENHID_OPERATIONS Vkbd_Operations = {
    Vkbd_Create,
    Vkbd_Destroy,
    Vkbd_Connect,
    Vkbd_WriteStore,
    Vkbd_Disconnect,
    Vkbd_DebugCallback,
    Vkbd_GetDeviceAttributes,
    Vkbd_GetDeviceDescriptor,
    Vkbd_GetReportDescriptor,
    Vkbd_GetFeature,
    Vkbd_SetFeature,
    Vkbd_WriteReport,
    Vkbd_ReadReport
};

NTSTATUS
VkbdInitialize(
    OUT PXENHID_OPERATIONS  Operations
    )
{
    *Operations = Vkbd_Operations;
    return STATUS_SUCCESS;
}


