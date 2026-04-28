// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "ap/dfs.h"
#include "esp.h"
#include "dcs.h"
#include "dfs_extn.h"
#include "reg_extn.h"


struct hostapd_freq_params;

int hostapd_wpa_event_extn(void *ctx, enum wpa_event_type event,
			   union wpa_event_data *data)
{
	struct hostapd_data *hapd = ctx;

	if (hapd == NULL || !data)
		return -EINVAL;

	switch (event) {
	case EVENT_ESP_UPDATE:
		hostapd_update_esp_params_extn(hapd, data);
		break;
	case EVENT_DCS_INTF:
		hostapd_dcs_intf_event_extn(hapd, data);
		break;
	case EVENT_HW_BLOCKED_CHANS_NOTIFY:
		hostapd_event_hw_blocklist_notify_extn(hapd,
			&data->event_data_extn.hw_blocklist_info);
		break;
	case EVENT_SCAN_RESULTS_EXTN:
		hostapd_cbs_handle_scan_complete(hapd, data);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static inline bool
hostapd_csa_target_has_unavailable_channel_extn(struct hostapd_data *hapd)
{
	struct hostapd_iface *iface = hapd->iface;
	struct hostapd_freq_params *freq_params = &hapd->cs_freq_params;
	enum chan_width chan_width;
	int dfs_range = 0;

	switch (freq_params->bandwidth) {
	case 40:
		chan_width = CHAN_WIDTH_40;
		break;
	case 80:
		if (freq_params->center_freq2)
			chan_width = CHAN_WIDTH_80P80;
		else
			chan_width = CHAN_WIDTH_80;
		break;
	case 160:
		chan_width = CHAN_WIDTH_160;
		break;
	case 320:
		chan_width = CHAN_WIDTH_320;
		break;
	case 0:
	case 20:
	default:
		chan_width = CHAN_WIDTH_20;
		break;
	}

	if (freq_params->center_freq1)
		dfs_range += hostapd_is_dfs_overlap(iface, chan_width,
						    freq_params->center_freq1);
	else
		dfs_range += hostapd_is_dfs_overlap(iface, chan_width,
						    freq_params->freq);

	if (freq_params->center_freq2)
		dfs_range += hostapd_is_dfs_overlap(iface, chan_width,
						    freq_params->center_freq2);

	if (!dfs_range)
		return false;

	return hostapd_dfs_csa_target_has_unavailable_channel(iface, freq_params,
							      chan_width);
}


bool hostapd_handle_csa_target_unavailable_extn(struct hostapd_data *hapd,
						int freq, int finished)
{
	if (!finished || !hapd->csa_in_progress ||
	    freq != hapd->cs_freq_params.freq ||
	    !hostapd_csa_target_has_unavailable_channel_extn(hapd))
		return false;

	wpa_printf(MSG_INFO,
		   "CSA target %d became unavailable, selecting a fresh channel",
		   freq);
	hostapd_dfs_restart_channel_extn(hapd->iface);

	return true;
}

int wpa_supplicant_event_extn(struct wpa_supplicant *wpa_s,
			      enum wpa_event_type event,
			      union wpa_event_data *data)
{
	if (!wpa_s || !data)
		return -EINVAL;

	switch (event) {
	case EVENT_HW_BLOCKED_CHANS_NOTIFY:
		wpas_event_hw_blocklist_notify_extn(
			wpa_s, &data->event_data_extn.hw_blocklist_info);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
