// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "../wpa_supplicant/config.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/driver_i.h"
#include "../wpa_supplicant/scan.h"
#include "cmn.h"

static int wpas_get_radio_idx_for_freq(struct wpa_supplicant *wpa_s, int freq,
				       u8 *radio_idx)
{
	struct hostapd_multi_hw_info *multi_hw_info;
	unsigned int num_multi_hws = 0;
	unsigned int i;

	multi_hw_info = wpa_get_multi_hw_info(wpa_s, &num_multi_hws);
	if (!multi_hw_info || !num_multi_hws || !freq)
		return -1;

	for (i = 0; i < num_multi_hws; i++) {
		if (freq >= multi_hw_info[i].start_freq &&
		    freq <= multi_hw_info[i].end_freq) {
			*radio_idx = multi_hw_info[i].hw_idx;
			return 0;
		}
	}

	wpa_printf(MSG_DEBUG, "HE MCS 12/13: no radio_idx found for freq = %d", freq);

	return -1;
}

int wpas_set_he_mcs_12_13_peer_cap_extn(struct wpa_supplicant *wpa_s, int freq)
{
	struct wpa_supplicant_extn *wpas_extn = &wpa_s->wpas_extn;
	u8 radio_idx;

	if (wpas_get_radio_idx_for_freq(wpa_s, freq, &radio_idx))
		return -1;

	if (nl80211_set_he_mcs_12_13_peer_cap_extn(wpa_s->drv_priv, radio_idx,
						   wpas_extn->he_mcs_12_13_peer_cap))
		return -1;

	wpa_printf(MSG_DEBUG,
		   "he_mcs_12_13 peer capability = 0x%04x set to driver successfully for radio_idx = %u",
		   wpas_extn->he_mcs_12_13_peer_cap, radio_idx);

	return 0;
}
