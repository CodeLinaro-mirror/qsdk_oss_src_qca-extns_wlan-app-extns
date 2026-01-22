/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "includes.h"
#include "utils/common.h"
#include "utils/os.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "ap/ap_drv_ops.h"
#include "ap/dfs.h"
#include "ap/hw_features.h"
#include "ap/beacon.h"
#include "drivers/driver.h"
#include "common/ieee802_11_defs.h"
#include "common/defs.h"
#include "hostapd_rptr_extn.h"

/**
 * hostapd_csa_bitmap_update_extn - Track CSA completion and notify supplicant
 * @iface: Hostapd interface whose links are undergoing CSA/CAC
 * @freq: Operating frequency used when all links complete the CSA/CAC
 *
 * Update the per-link CSA bitmap in the extended configuration and, when
 * all bits are cleared, emit a completion event towards wpa_supplicant.
 */
void hostapd_csa_bitmap_update_extn(struct hostapd_iface *iface, int freq)
{
	int j;
	if (!iface || !iface->conf)
		return;

	if (iface->iface_extn.csa_bitmap) {
		for (j = 0; j < MAX_NUM_MLD_LINKS; j++) {
			if (iface->iface_extn.csa_bitmap & (1 << j)) {
				iface->iface_extn.csa_bitmap &= ~(1 << j);
				break;
			}
		}
		if (!iface->iface_extn.csa_bitmap) {
			wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO,
				"Revd CSA/CAC completion, notify wpa_supplicant");
#ifdef CONFIG_HOSTAPD_SRC_DIR
			hostapd_ucode_chsw_comp_ev_notify(iface->bss[0], freq);
#endif
		}
	}
}

/**
 * hostapd_ml_acs_check_and_notify - Track ACS completion and notify supplicant
 * @iface: Hostapd interface whose links are undergoing CSA/CAC
 * @status: ACS status
 *
 * Check if all ML partner links have completed ACS, and if so sends
 * notification to wpa_supplicant to start Repeater STA scan.
 */
void hostapd_ml_acs_check_and_notify(struct hostapd_iface *iface, bool status)
{
	struct hostapd_data *hapd = iface->bss[0];
	int i, num_ap_with_acs_done = 0;

	wpa_printf(MSG_DEBUG, "ACS: Checking ACS status across all ML partner links");

	if (status)
		iface->iface_extn.acs_success = true;
	else
		iface->iface_extn.acs_failed = true;

	num_ap_with_acs_done++;

	/* Check all other ML partner interfaces */
	for (i = 0; i < hapd->iface->interfaces->count; i++) {
		struct hostapd_iface *h =
			hapd->iface->interfaces->iface[i];
		struct hostapd_data *h_hapd = h->bss[0];

		if (h == hapd->iface) {
			wpa_printf(MSG_DEBUG, "ACS: Continue as same iface");
			continue;
		}

		if (hostapd_is_ml_partner(hapd, h_hapd)) {
			/* Count interfaces that have status (success or failure) */
			if (h_hapd->iface->iface_extn.acs_success ||
			    h_hapd->iface->iface_extn.acs_failed) {
				num_ap_with_acs_done++;
				wpa_printf(MSG_DEBUG, "ACS: ML partner %s has ACS done (status=%d)",
					   h_hapd->conf->iface,
				           h_hapd->iface->iface_extn.acs_success
					   ? h_hapd->iface->iface_extn.acs_success
					   : h_hapd->iface->iface_extn.acs_failed);
			}
		}
	}

	wpa_printf(MSG_DEBUG, "ACS: Total ML partners with ACS done: %d, Total interfaces: %zu",
			num_ap_with_acs_done, hapd->iface->interfaces->count);

	/* If all ML partner links have completed ACS, send notification */
	if (num_ap_with_acs_done == hapd->iface->interfaces->count) {
		wpa_printf(MSG_DEBUG, "ACS: All ML partner links have completed ACS, sending notification");
#ifdef CONFIG_HOSTAPD_SRC_DIR
		hostapd_ucode_notify_acs_completed(iface, 1);
#endif
	}
}

/**
 * uc_hostapd_compare_channel_params_extn - Compare desired vs current channel
 * @conf: Pointer to hostapd configuration
 * @freq_params: Desired frequency and channel width parameters
 * @freq: Primary operating frequency
 *
 * Compare current channel configuration (frequency, bandwidth, center
 * frequencies and puncturing bitmap) against the desired parameters.
 * When they match, the caller can skip channel switch announcement (CSA)
 * and directly notify completion.
 *
 * Return: true if the configurations are equivalent, false otherwise.
 */
bool
uc_hostapd_compare_channel_params_extn(struct hostapd_config *conf,
				       struct hostapd_freq_params freq_params,
				       int freq)
{
	enum oper_chan_width oper_chwidth;
	int bandwidth;
	int centr_freq_seg0_idx;
	int centr_freq_seg1_idx;
	int center_freq1;
	int center_freq2;
	u8 op_class, channel;

	oper_chwidth = hostapd_get_oper_chwidth(conf);
	if (oper_chwidth == CONF_OPER_CHWIDTH_80MHZ)
		bandwidth = 80;
	else if (oper_chwidth == CONF_OPER_CHWIDTH_160MHZ ||
		oper_chwidth == CONF_OPER_CHWIDTH_80P80MHZ)
		bandwidth = 160;
	else if (oper_chwidth == CONF_OPER_CHWIDTH_320MHZ)
		bandwidth = 320;
	else if (conf->secondary_channel)
		bandwidth = 40;
	else
		bandwidth = 20;

	centr_freq_seg0_idx = hostapd_get_oper_centr_freq_seg0_idx(conf);
	centr_freq_seg1_idx = hostapd_get_oper_centr_freq_seg1_idx(conf);
	ieee80211_freq_to_channel_ext(freq, 0, 1, &op_class, &channel);

	/* Handle 2.4GHz band differently */
	if (freq >= 2412 && freq <= 2472) {
		if (bandwidth == 40) {
			int offset_mhz = (channel <= 7) ? 10 : -10;
			center_freq1 = freq + offset_mhz;
		} else {
			center_freq1 = freq;
		}
		center_freq2 = 0;
	} else {
		center_freq1 = ieee80211_chan_to_freq(NULL, op_class, centr_freq_seg0_idx);
		center_freq2 = ieee80211_chan_to_freq(NULL, op_class, centr_freq_seg1_idx);
		center_freq1 = (center_freq1 == -1) ? 0 : center_freq1;
		center_freq2 = (center_freq2 == -1) ? 0 : center_freq2;
	}

	if ((freq_params.freq == freq) &&
	    (freq_params.bandwidth == bandwidth) &&
	    (freq_params.center_freq1 == center_freq1) &&
	    (freq_params.center_freq2 == center_freq2) &&
	    (freq_params.punct_bitmap == conf->punct_bitmap)) {
		return true;
	} else {
		return false;
	}
}

/**
 * uc_hostapd_iface_switch_channel_extn - Perform CSA with repeater extensions
 * @iface: Hostapd interface on which to switch channel
 * @is_dfs: Whether the target channel is DFS and may require CAC
 * @wpa_state: Optional pointer to current wpa_supplicant state string
 * @csa: Pointer to CSA settings describing requested channel parameters
 *
 * Decide whether a channel switch announcement (CSA) is needed based on
 * current vs requested channel settings. When needed, perform CSA on all
 * BSSes of the interface and track completion in PRE_CONNECT using a bitmap.
 * If no change is required, optionally notify wpa_supplicant of completion.
 *
 * Return: 0 on success, or negative error code on failure from
 *         hostapd_switch_channel().
 */
int uc_hostapd_iface_switch_channel_extn(struct hostapd_iface *iface,
                                        bool is_dfs, char *wpa_state,
                                        struct csa_settings *csa)
{
	struct hostapd_config *conf = iface->conf;
	int i, ret = 0;
	bool pre_connect = false;

	if (wpa_state)
		wpa_printf(MSG_INFO, "wpa_state: %s", wpa_state);

	pre_connect = (wpa_state && os_strcmp(wpa_state, "PRE_CONNECT") == 0);

#ifdef CONFIG_HOSTAPD_SRC_DIR
	/* Apply skip_cac only in PRE_CONNECT when conf->skip_cac is enabled */
	if (conf->conf_extn.skip_cac && pre_connect)
		csa->freq_params.skip_cac = is_dfs;
#endif

	/* If channel params differ, perform CSA and track per-BSS completion */
	if (!uc_hostapd_compare_channel_params_extn(conf, csa->freq_params, iface->freq)) {
		for (i = 0; i < iface->num_bss; i++) {
			ret = hostapd_switch_channel(iface->bss[i], csa);
			if (ret) {
				wpa_printf(MSG_ERROR, "Channel switch failed"
					   " ret = %d", ret);
#ifdef CONFIG_HOSTAPD_SRC_DIR
				if (pre_connect) {
					hostapd_ucode_chsw_comp_ev_notify(
							iface->bss[0],
							csa->freq_params.freq);
				}
#endif
				return ret;
			}
			/* Track CSA per link using bitmap in PRE_CONNECT */
			if (pre_connect)
				iface->iface_extn.csa_bitmap |= BIT(i);
		}
	} else {
		/* No CSA needed; notify supplicant only in PRE_CONNECT */
		wpa_printf(MSG_INFO, "AP is already UP in same channel");
		if (pre_connect) {
#ifdef CONFIG_HOSTAPD_SRC_DIR
			hostapd_ucode_chsw_comp_ev_notify(iface->bss[0],
							  csa->freq_params.freq);
#endif
		}
	}

	/* Return 0 on success path, negative on failure consistent with caller */
	return ret;
}
