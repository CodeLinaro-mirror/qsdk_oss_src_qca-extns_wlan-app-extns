/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef WPA_SUPPLICANT_RPTR_EXTN_H
#define WPA_SUPPLICANT_RPTR_EXTN_H

#include "cmn.h"

/* QCN Vendor Specific IE for 5G 320MHz (240MHz) support */
#define QCN_IE_VENDOR_TYPE                      0x8cfdf001
#define QCN_OUI_TYPE                            0x01

void wpa_bss_check_5g_320mhz_vendor_ie_extn(struct wpa_supplicant *wpa_s,
					    struct wpa_bss *bss);

#endif /* WPA_SUPPLICANT_RPTR_EXTN_H */
