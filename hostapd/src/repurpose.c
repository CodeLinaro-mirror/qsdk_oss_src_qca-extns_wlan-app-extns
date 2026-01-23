// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "ap/hostapd.h"
#include "cmn.h"

int
hostapd_config_check_bss_repurpose_mode_extn(const struct hostapd_config *conf,
					     const struct hostapd_bss_config *bss)
{
#ifdef CONFIG_IEEE80211BE
	if (!bss->bss_extn.repurpose_mode)
		return 0;

	/* Disallow repurpose_mode on non-MLD BSS */
	if (!bss->mld_ap) {
		wpa_printf(MSG_ERROR,
			   "repurpose_mode only allowed on link BSS of AP MLD");
		return -1;
	}

	/* Disallow when 11be/11ax are disabled by config */
	if (!conf->ieee80211be || bss->disable_11be ||
	    !conf->ieee80211ax || bss->disable_11ax) {
		wpa_printf(MSG_ERROR,
			   "repurpose_mode not allowed when 11be/11ax disabled");
		return -1;
	}

	/* 11AX can't be disabled on 6 GHz through repurpose */
	if (is_6ghz_op_class(conf->op_class) &&
	    hostapd_is_repurpose_disabled_11ax_extn(bss)) {
		wpa_printf(MSG_ERROR,
			   "repurpose: Can't disable 11AX on 6GHz BSS");
		return -1;
	}
#endif /* CONFIG_IEEE80211BE */
	return 0;
}

bool
hostapd_is_repurpose_disabled_11ax_extn(const struct hostapd_bss_config *bss)
{
#ifdef CONFIG_IEEE80211BE
	if (bss->mld_ap &&
	    hostapd_is_valid_repurpose_mode_extn(bss->bss_extn.repurpose_mode) &&
	    bss->bss_extn.repurpose_mode < REPURPOSE_11AX)
		return true;
#endif /* CONFIG_IEEE80211BE */
	return false;
}

bool
hostapd_is_repurpose_disabled_11be_extn(const struct hostapd_bss_config *bss)
{
#ifdef CONFIG_IEEE80211BE
	if (bss->mld_ap &&
	    hostapd_is_valid_repurpose_mode_extn(bss->bss_extn.repurpose_mode) &&
	    bss->bss_extn.repurpose_mode < REPURPOSE_11BE)
		return true;
#endif /* CONFIG_IEEE80211BE */
	return false;
}

#ifdef CONFIG_IEEE80211BE
int wpa_driver_nl80211_vendor_cmd_notify_link_repurpose(void *priv, u8 link_id);
#endif /* CONFIG_IEEE80211BE */

int
hostapd_drv_notify_link_repurpose_extn(struct hostapd_data *hapd, u8 link_id)
{
#ifdef CONFIG_IEEE80211BE
	if (!hapd || !hapd->driver || !hapd->drv_priv)
		return 0;

	return wpa_driver_nl80211_vendor_cmd_notify_link_repurpose(hapd->drv_priv,
								   link_id);
#else
	return 0;
#endif /* CONFIG_IEEE80211BE */
}
