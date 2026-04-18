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
