/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "cmn.h"

void hostapd_iface_init_extn(struct hostapd_iface *iface)
{
	if (!iface)
		return;

	iface->iface_extn.check_hw_blocklist = true;
}

void hostapd_iface_deinit_extn(struct hostapd_iface *iface)
{
	if (!iface)
		return;

	hostapd_free_hw_blocklist_info_extn(iface->iface_extn.hw_blocklist_info,
					    iface->iface_extn.num_hw_blocklist);
	iface->iface_extn.hw_blocklist_info = NULL;
	iface->iface_extn.num_hw_blocklist = 0;
	iface->iface_extn.check_hw_blocklist = false;
}

void wpas_iface_init_extn(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s)
		return;

	wpa_s->wpas_extn.check_hw_blocklist = true;
}

void wpas_iface_deinit_extn(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s)
		return;

	hostapd_free_hw_blocklist_info_extn(wpa_s->wpas_extn.hw_blocklist_info,
					    wpa_s->wpas_extn.num_hw_blocklist);
	wpa_s->wpas_extn.hw_blocklist_info = NULL;
	wpa_s->wpas_extn.num_hw_blocklist = 0;
	wpa_s->wpas_extn.check_hw_blocklist = false;
}
