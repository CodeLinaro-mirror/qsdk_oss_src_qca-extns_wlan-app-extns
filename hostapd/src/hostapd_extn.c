// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/defs.h"
#include "drivers/driver.h"
#include "ap/hostapd.h"
#include "cmn.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"

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
