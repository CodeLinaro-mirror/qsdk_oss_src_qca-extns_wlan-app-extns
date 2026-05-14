// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/defs.h"
#include "drivers/driver.h"
#include "common/hw_features_common.h"
#include "ap/hostapd.h"
#include "ap/hw_features.h"
#include "cmn.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"
#include "ap/sta_info.h"

/**
 * check_40mhz_2g4_bss_snr_below_threshold_extn - Check if a BSS should be
 * ignored in the 40 MHz 2.4 GHz coexistence check due to low SNR
 * @bss: Scan result entry to evaluate
 * @extn_args: Extension arguments carrying the configured SNR threshold
 *
 * Returns true if @bss->snr is below the configured threshold and the entry
 * should be skipped (i.e., treated as non-interfering). Logs a debug message
 * when a BSS is filtered out.
 *
 * Return: true if the BSS should be ignored, false otherwise.
 */
bool check_40mhz_2g4_bss_snr_below_threshold_extn(
	const struct wpa_scan_res *bss,
	const struct check_40mhz_2g4_extn_args *extn_args)
{
	int snr;

	if (!extn_args || !bss)
		return false;

	snr = bss->level - bss->noise;

	if (snr >= extn_args->threshold)
		return false;

	wpa_printf(MSG_DEBUG,
		   "check_40mhz_2g4: ignoring BSS " MACSTR
		   " freq=%d snr=%d (threshold=%u)",
		   MAC2STR(bss->bssid), bss->freq,
		   snr, extn_args->threshold);
	return true;
}

/**
 * hostapd_rssi_to_snr_extn - Convert a received frame's RSSI to SNR
 * @hapd: Pointer to hostapd data (used to read the lowest observed noise floor)
 * @ssi_signal: Signal level (dBm) of the received frame
 *
 * Computes SNR as ssi_signal - lowest_nf. If no noise floor measurement is
 * available (lowest_nf == 0), ssi_signal is returned directly as a proxy.
 *
 * Return: SNR in dB, or ssi_signal if noise floor is unavailable.
 */
int hostapd_rssi_to_snr_extn(struct hostapd_data *hapd, int ssi_signal)
{
	if (!hapd || !hapd->iface)
		return ssi_signal;

	return ssi_signal - DEFAULT_NOISE_FLOOR_2GHZ;
}

/**
 * hostapd_2040_coex_action_snr_below_threshold_extn - Check if a 20/40 MHz
 * coexistence action frame should be ignored due to low signal level
 * @hapd: Pointer to hostapd data (used to read obss_rx_snr_threshold)
 * @ssi_signal: Signal level (dBm) of the received action frame
 *
 * Returns true if the action frame's signal level is below the configured
 * obss_rx_snr_threshold and the frame should be silently discarded.
 *
 * Return: true if the action frame should be ignored, false otherwise.
 */
bool hostapd_2040_coex_action_snr_below_threshold_extn(
	struct hostapd_data *hapd, int rssi)
{
	u8 threshold;
	int snr;

	if (!hapd || !hapd->iconf)
		return false;

	threshold = hapd->iconf->conf_extn.obss_rx_snr_threshold;
        snr = hostapd_rssi_to_snr_extn(hapd, rssi);

        if ((u8)snr >= threshold)
		return false;

	wpa_printf(MSG_DEBUG,
		   "2040 coex: ignoring action frame snr=%d"
		   " (obss_rx_snr_threshold=%d)",
		   snr, threshold);
	return true;
}

/**
 * hostapd_ht40_intolerant_snr_below_threshold_extn - Check if a station's
 * assoc frame SNR is too low to honour its HT 40 MHz Intolerant indication
 * @hapd: Pointer to hostapd data (used to read obss_rx_snr_threshold)
 * @sta: Station that set the HT_CAP_INFO_40MHZ_INTOLERANT bit
 *
 * Returns true if the station's assoc frame SNR is below the configured
 * obss_rx_snr_threshold, meaning the ht40_intolerant_add() call should be
 * skipped for this station.
 *
 * Return: true if ht40_intolerant_add() should be skipped, false otherwise.
 */
bool hostapd_ht40_intolerant_snr_below_threshold_extn(
	struct hostapd_data *hapd, struct sta_info *sta)
{
	int threshold;

	if (!hapd || !hapd->iconf || !sta)
		return false;

	threshold = hapd->iconf->conf_extn.obss_rx_snr_threshold;
	if (!threshold)
		return false;

	if (sta->sta_extn.assoc_snr >= threshold)
		return false;

	wpa_printf(MSG_DEBUG,
		   "HT: Skipping ht40_intolerant_add for STA " MACSTR
		   " assoc_snr=%d (obss_rx_snr_threshold=%d)",
		   MAC2STR(sta->addr), sta->sta_extn.assoc_snr, threshold);
	return true;
}

int hostapd_validate_mbssid_group_size_extn(struct hostapd_data *hapd)
{
	if (hapd->conf->mld_ap &&
	    (hapd->iface->conf->group_size == MULTI_MBSSID_GROUP_SIZE_MAX) &&
	    (hapd->iface->max_mgmt_frm_sz < MGMT_MIN_FRAME_SIZE_REQUIRED_MLO_MBSSID)) {
		wpa_printf(MSG_ERROR,
			   "Invalid MBSSID group size (%u) for MLD AP with mgmt frame size (%d)",
			   hapd->iface->conf->group_size, MGMT_MIN_FRAME_SIZE_REQUIRED_MLO_MBSSID);
		return -1;
	}

	return 0;
}

#ifdef HOSTAPD
bool hostapd_regdom_channel_supported(struct hostapd_iface *iface,
				      struct hostapd_channel_data *chan)
{
	if (!chan)
		return false;

	if (!chan_in_current_hw_info(iface->current_hw_info, chan))
		return false;

	if ((chan->flag & HOSTAPD_CHAN_DISABLED) && !is_6ghz_freq(chan->freq))
		return false;

	if ((chan->flag & HOSTAPD_CHAN_NO_IR) && !is_6ghz_freq(chan->freq))
		return false;

	return true;
}

bool hostapd_is_iface_regdom_supported(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *chan;
	enum hostapd_hw_mode hw_mode;
	struct hostapd_hw_modes *mode;
	int oper_freq, i, j;

	if (!iface->hw_features || !iface->num_hw_features ||
	    !iface->current_hw_info)
		return false;

	oper_freq = iface->freq;
	if (!oper_freq && iface->conf->channel > 0)
		oper_freq = hostapd_hw_get_freq(iface->bss[0], iface->conf->channel);

	if (oper_freq > 0) {
		hw_mode = iface->current_mode ? iface->current_mode->mode :
			  iface->conf->hw_mode;
		chan = hw_get_channel_freq(hw_mode, oper_freq, NULL,
					   iface->hw_features,
					   iface->num_hw_features);
		return hostapd_regdom_channel_supported(iface, chan);
	}

	for (i = 0; i < iface->num_hw_features; i++) {
		mode = &iface->hw_features[i];
		if (mode->mode != iface->conf->hw_mode)
			continue;

		for (j = 0; j < mode->num_channels; j++) {
			if (hostapd_regdom_channel_supported(iface,
							     &mode->channels[j]))
				return true;
		}
	}

	return false;
}

struct hostapd_channel_data *
hostapd_regdom_first_supported_channel(struct hostapd_iface *iface)
{
	struct hostapd_hw_modes *mode;
	int i, j;

	if (!iface->hw_features || !iface->num_hw_features)
		return NULL;

	for (i = 0; i < iface->num_hw_features; i++) {
		mode = &iface->hw_features[i];
		if (mode->mode != iface->conf->hw_mode)
			continue;

		for (j = 0; j < mode->num_channels; j++) {
			if (hostapd_regdom_channel_supported(iface,
							     &mode->channels[j]))
				return &mode->channels[j];
		}
	}

	return NULL;
}

int hostapd_regdom_move_iface_to_supported_channel(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *chan;
	u8 op_class = 0;
	u8 op_chan = 0;

	chan = hostapd_regdom_first_supported_channel(iface);
	if (!chan)
		return -1;

	iface->freq = chan->freq;
	iface->conf->channel = chan->chan;

	if (ieee80211_freq_to_channel_ext(chan->freq,
					  iface->conf->secondary_channel,
					  hostapd_get_oper_chwidth(iface->conf),
					  &op_class, &op_chan) != NUM_HOSTAPD_MODES &&
	    op_chan == chan->chan)
		iface->conf->op_class = op_class;

	if (hostapd_set_current_hw_info(iface, iface->freq))
		return -1;

	wpa_printf(MSG_INFO,
		   "REGDOM: Interface %s moving to supported channel %u (%d MHz)",
		   iface->conf->bss[0]->iface, iface->conf->channel, iface->freq);

	return 0;
}

void hostapd_regdom_force_disable_iface(struct hostapd_iface *iface,
						 const char *reason)
{
	if (iface->state != HAPD_IFACE_ENABLED &&
	    iface->state != HAPD_IFACE_NO_IR &&
	    !iface->is_regdom_forced_down)
		return;

	iface->is_regdom_forced_down = true;
	wpa_printf(MSG_INFO,
		   "REGDOM: Disabling interface %s (%s)",
		   iface->conf->bss[0]->iface, reason);

	if (iface->state == HAPD_IFACE_ENABLED)
		hostapd_set_no_ir_state(iface);
}

int hostapd_regdom_restore_iface(struct hostapd_iface *iface)
{
	bool pending_reenable;
	int ret;

	if (!iface->is_regdom_forced_down)
		return 0;

	if (iface->state == HAPD_IFACE_ENABLED) {
		iface->is_regdom_forced_down = false;
		return 0;
	}

	if (iface->state == HAPD_IFACE_NO_IR) {
		pending_reenable = hostapd_check_reenable_bss(iface);

		ret = hostapd_no_ir_channel_list_updated(iface);
		if (ret)
			return ret;

		if (iface->state != HAPD_IFACE_NO_IR) {
			iface->is_regdom_forced_down = false;
			return 0;
		}

		if (pending_reenable && hostapd_check_reenable_bss(iface)) {
			hostapd_enable_pending_bss(iface);

			if (iface->state == HAPD_IFACE_ENABLED ||
			    !hostapd_check_reenable_bss(iface))
				iface->is_regdom_forced_down = false;
		}

		return 0;
	}

	if (iface->state == HAPD_IFACE_ENABLED)
		iface->is_regdom_forced_down = false;

	return 0;
}
#endif
