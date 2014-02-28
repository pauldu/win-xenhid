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

#ifndef _XENHID_REPORTDESCR_H
#define _XENHID_REPORTDESCR_H

#define VKBD_REPORT_DESCRIPTOR      \
    0x05, 0x01,         /* USAGE_PAGE (Generic Desktop)                    */ \
    0x09, 0x06,         /* USAGE (Keyboard)                                */ \
    0xa1, 0x01,         /* COLLECTION (Application)                        */ \
    0x85, 0x01,         /*   REPORT_ID (1)                                 */ \
    0x05, 0x07,         /*   USAGE_PAGE (Keyboard)                         */ \
    0x19, 0xe0,         /*   USAGE_MINIMUM (Keyboard LeftControl)          */ \
    0x29, 0xe7,         /*   USAGE_MAXIMUM (Keyboard Right GUI)            */ \
    0x15, 0x00,         /*   LOGICAL_MINIMUM (0)                           */ \
    0x25, 0x01,         /*   LOGICAL_MAXIMUM (1)                           */ \
    0x75, 0x01,         /*   REPORT_SIZE (1)                               */ \
    0x95, 0x08,         /*   REPORT_COUNT (8)                              */ \
    0x81, 0x02,         /*   INPUT (Data,Var,Abs)                          */ \
    0x95, 0x01,         /*   REPORT_COUNT (1)                              */ \
    0x75, 0x08,         /*   REPORT_SIZE (8)                               */ \
    0x81, 0x03,         /*   INPUT (Cnst,Var,Abs)                          */ \
    0x95, 0x06,         /*   REPORT_COUNT (6)                              */ \
    0x75, 0x08,         /*   REPORT_SIZE (8)                               */ \
    0x15, 0x00,         /*   LOGICAL_MINIMUM (0)                           */ \
    0x25, 0x65,         /*   LOGICAL_MAXIMUM (101)                         */ \
    0x05, 0x07,         /*   USAGE_PAGE (Keyboard)                         */ \
    0x19, 0x00,         /*   USAGE_MINIMUM (Reserved (no event indicated)) */ \
    0x29, 0x65,         /*   USAGE_MAXIMUM (Keyboard Application)          */ \
    0x81, 0x00,         /*   INPUT (Data,Ary,Abs)                          */ \
    0xc0,               /* END_COLLECTION                                  */ \
    0x05, 0x01,         /* USAGE_PAGE (Generic Desktop)                    */ \
    0x09, 0x02,         /* USAGE (Mouse)                                   */ \
    0xa1, 0x01,         /* COLLECTION (Application)                        */ \
    0x85, 0x02,         /*   REPORT_ID (2)                                 */ \
    0x09, 0x01,         /*   USAGE (Pointer)                               */ \
    0xa1, 0x00,         /*   COLLECTION (Physical)                         */ \
    0x05, 0x09,         /*     USAGE_PAGE (Button)                         */ \
    0x19, 0x01,         /*     USAGE_MINIMUM (Button 1)                    */ \
    0x29, 0x05,         /*     USAGE_MAXIMUM (Button 5)                    */ \
    0x15, 0x00,         /*     LOGICAL_MINIMUM (0)                         */ \
    0x25, 0x01,         /*     LOGICAL_MAXIMUM (1)                         */ \
    0x95, 0x05,         /*     REPORT_COUNT (5)                            */ \
    0x75, 0x01,         /*     REPORT_SIZE (1)                             */ \
    0x81, 0x02,         /*     INPUT (Data,Var,Abs)                        */ \
    0x95, 0x01,         /*     REPORT_COUNT (1)                            */ \
    0x75, 0x03,         /*     REPORT_SIZE (3)                             */ \
    0x81, 0x03,         /*     INPUT (Cnst,Var,Abs)                        */ \
    0x05, 0x01,         /*     USAGE_PAGE (Generic Desktop)                */ \
    0x09, 0x30,         /*     USAGE (X)                                   */ \
    0x09, 0x31,         /*     USAGE (Y)                                   */ \
    0x16, 0x00, 0x00,   /*     LOGICAL_MINIMUM (0)                         */ \
    0x26, 0xff, 0x7f,   /*     LOGICAL_MAXIMUM (32767)                     */ \
    0x75, 0x10,         /*     REPORT_SIZE (16)                            */ \
    0x95, 0x02,         /*     REPORT_COUNT (2)                            */ \
    0x81, 0x02,         /*     INPUT (Data,Var,Abs)                        */ \
    0x09, 0x38,         /*     USAGE (Z)                                   */ \
    0x15, 0x81,         /*     LOGICAL_MINIMUM (-127)                      */ \
    0x25, 0x7f,         /*     LOGICAL_MAXIMUM (127)                       */ \
    0x75, 0x08,         /*     REPORT_SIZE (8)                             */ \
    0x95, 0x01,         /*     REPORT_COUNT (1)                            */ \
    0x81, 0x06,         /*     INPUT (Data,Var,Rel)                        */ \
    0xc0,               /*   END_COLLECTION                                */ \
    0xc0                /* END_COLLECTION                                  */

#endif // _XENHID_REPORTDESCR_H

