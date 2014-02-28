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

#ifndef _XENHID_FRONTEND_H
#define _XENHID_FRONTEND_H

typedef struct _XENHID_FRONTEND     XENHID_FRONTEND, *PXENHID_FRONTEND;

#include "driver.h"

extern NTSTATUS
FrontendCreate(
    IN  PXENHID_FDO             Fdo,
    OUT PXENHID_FRONTEND*       Frontend
    );

extern VOID
FrontendDestroy(
    IN  PXENHID_FRONTEND        Frontend
    );

extern NTSTATUS
FrontendEnable(
    IN  PXENHID_FRONTEND        Frontend
    );

extern VOID
FrontendDisable(
    IN  PXENHID_FRONTEND        Frontend
    );

extern NTSTATUS
FrontendGetDeviceAttributes(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    );

extern NTSTATUS
FrontendGetDeviceDescriptor(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    );

extern NTSTATUS
FrontendGetReportDescriptor(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    );

extern NTSTATUS
FrontendGetFeature(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length,
    OUT PULONG_PTR              Information
    );

extern NTSTATUS
FrontendSetFeature(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length
    );

extern NTSTATUS
FrontendWriteReport(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length
    );

extern NTSTATUS
FrontendReadReport(
    IN  PXENHID_FRONTEND        Frontend
    );

extern NTSTATUS
FrontendCompleteRead(
    IN  PXENHID_FRONTEND        Frontend,
    IN  PVOID                   Buffer,
    IN  ULONG                   Length
    );

extern PXENHID_FDO
FrontendGetFdo(
    IN  PXENHID_FRONTEND        Frontend
    );

extern USHORT
FrontendGetBackendDomain(
    IN  PXENHID_FRONTEND        Frontend
    );

#endif  // _XENHID_FRONTEND_H
