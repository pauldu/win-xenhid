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

#ifndef _XENHID_OPERATIONS_H
#define _XENHID_OPERATIONS_H

#include <ntddk.h>
#include <store_interface.h>
#include <debug_interface.h>
#include "frontend.h"

typedef void*       PXENHID_CONTEXT;

typedef struct _XENHID_OPERATIONS {
    // lifecycle
    NTSTATUS    (*Create)(PXENHID_FRONTEND, PXENHID_CONTEXT*);
    VOID        (*Destroy)(PXENHID_CONTEXT);

    NTSTATUS    (*Connect)(PXENHID_CONTEXT);
    NTSTATUS    (*WriteStore)(PXENHID_CONTEXT, PXENBUS_STORE_TRANSACTION);
    VOID        (*Disconnect)(PXENHID_CONTEXT);
    VOID        (*DebugCallback)(PXENHID_CONTEXT, PXENBUS_DEBUG_INTERFACE, PXENBUS_DEBUG_CALLBACK);

    // HID operations
    NTSTATUS    (*GetDeviceAttributes)(PXENHID_CONTEXT, PVOID, ULONG, PULONG_PTR);
    NTSTATUS    (*GetDeviceDescriptor)(PXENHID_CONTEXT, PVOID, ULONG, PULONG_PTR);
    NTSTATUS    (*GetReportDescriptor)(PXENHID_CONTEXT, PVOID, ULONG, PULONG_PTR);
    NTSTATUS    (*GetFeature)(PXENHID_CONTEXT, PVOID, ULONG, PULONG_PTR);
    NTSTATUS    (*SetFeature)(PXENHID_CONTEXT, PVOID, ULONG);
    NTSTATUS    (*WriteReport)(PXENHID_CONTEXT, PVOID, ULONG);
    NTSTATUS    (*ReadReport)(PXENHID_CONTEXT);
} XENHID_OPERATIONS, *PXENHID_OPERATIONS;

#endif  // _XENHID_OPERATIONS_H
