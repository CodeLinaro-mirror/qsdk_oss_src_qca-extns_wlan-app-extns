// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "ap/hostapd.h"
#include "cmn.h"
#include "ap/beacon.h"

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

	/* 11AC requires ieee80211ac to be enabled */
	if (bss->bss_extn.repurpose_mode == REPURPOSE_11AC &&
	    (!conf->ieee80211ac || bss->disable_11ac)) {
		wpa_printf(MSG_ERROR,
			   "repurpose: REPURPOSE_11AC requires ieee80211ac enabled");
		return -1;
	}
#endif /* CONFIG_IEEE80211BE */
	return 0;
}

/**
 * hostapd_is_repurpose_disabled_11ax_extn - Checks if repurpose has disabled
 * 11AX mode on the given BSS or not.
 * @bss: BSS config of the BSS.
 *
 * This helper function checks if repurpose feature had disabled 11ax capability
 * from the BSS or not. If no repurpose_mode configured then it returns false.
 * This does not really check if BSS has ieee80211ax=1 and disable_11ax=0.
 * So, use it along with appropriate mld_ap or ieee80211ax or disable_11ax check
 * from caller when it returns false.
 *
 * Return: True if BSS repurposed to mode lesser than 11ax, false otherwise.
 */
bool
hostapd_is_repurpose_disabled_11ax_extn(const struct hostapd_bss_config *bss)
{
	if (hostapd_is_valid_repurpose_mode_extn(bss->bss_extn.repurpose_mode) &&
	    bss->bss_extn.repurpose_mode < REPURPOSE_11AX)
		return true;

	return false;
}

/**
 * hostapd_is_repurpose_disabled_11be_extn - Checks if repurpose has disabled
 * 11BE mode on the given BSS or not.
 * @bss: BSS config of the BSS.
 *
 * This helper function checks if repurpose feature had disabled 11be capability
 * from the BSS or not. If no repurpose_mode configured then it returns false.
 * This does not really check if BSS has ieee80211be=1 and disable_11be=0.
 * So, use it along with appropriate mld_ap or ieee80211be or disable_11be check
 * from caller when it return false.
 *
 * Return: True if the BSS repurposed to mode lesser than 11be, false otherwise.
 */
bool
hostapd_is_repurpose_disabled_11be_extn(const struct hostapd_bss_config *bss)
{
	if (hostapd_is_valid_repurpose_mode_extn(bss->bss_extn.repurpose_mode) &&
	    bss->bss_extn.repurpose_mode < REPURPOSE_11BE)
		return true;

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

u8 hostapd_get_repurposed_links_bitmap_extn(struct hostapd_data *hapd,
					    u16 *repurposed_links)
{
	struct hostapd_data *lhapd;
	u8 num_repurposed_links = 0;

	if (repurposed_links)
		*repurposed_links = 0;

	for_each_mld_link_include_repurposed(lhapd, hapd) {
		if (hostapd_is_repurpose_disabled_11be_extn(lhapd->conf)) {
			if (repurposed_links)
				*repurposed_links |= BIT(lhapd->mld_link_id);
			num_repurposed_links++;
		}
	}

	return num_repurposed_links;
}

int
hostapd_link_remove_repurposed_bss_extn(struct hostapd_data *hapd,
					u32 removal_type)
{
	struct hostapd_data *tx_hapd = NULL;
	struct hostapd_iface *iface = hapd->iface;
	struct hostapd_iface **tmp;
	struct hapd_interfaces *interfaces = iface->interfaces;
	unsigned int i;

	if (removal_type == HAPD_LINK_DISABLE) {
		hostapd_disable_bss(hapd, 0, AP_EVENT_DISABLED);
		goto refresh_beacon;
	}

	if (iface->num_bss == 1) {
		for (i = 0; i < interfaces->count; i++) {
			if (interfaces->iface[i] == iface) {
				hostapd_interface_deinit_free(iface);
				os_remove_in_array(interfaces->iface,
						   interfaces->count,
						   sizeof(struct hostapd_iface *),
						   i);
				interfaces->count--;
				tmp = os_realloc_array(interfaces->iface,
						       interfaces->count,
						       sizeof(struct hostapd_iface *));
				if (!tmp)
					return -1;
				interfaces->iface = tmp;
				break;
			}
		}
	} else {
		for (i = 0; i < iface->conf->num_bss; i++) {
			if (iface->bss[i] == hapd)
				break;
		}

		/* Shouldn't happen */
		if (i >= iface->conf->num_bss) {
			wpa_printf(MSG_ERROR, "Wrong hapd is provided\n");
			return -1;
		}

		/* Store tx_hapd to update MBSSID beacon as hapd will be
		 * freed by hostapd_remove_bss() */
		tx_hapd = hostapd_mbssid_get_tx_bss(hapd);
		if (tx_hapd == hapd)
			tx_hapd = NULL;

		hostapd_remove_bss(iface, i);

		if (tx_hapd)
			ieee802_11_update_beacon_mbssid(tx_hapd);
	}

refresh_beacon:
	/* Refresh all the partner beacons */
	if (interfaces->count > 0)
		hostapd_refresh_all_iface_beacons(interfaces->iface[0]);

	return 0;
}

static const char *
hostapd_repurpose_mode_str_extn(
	enum repurpose_mode mode)
{
	switch (mode) {
	case REPURPOSE_11AC:
		return "11ac";
	case REPURPOSE_11AX:
		return "11ax";
	case REPURPOSE_11BE:
		return "11be";
	default:
		return "invalid";
	}
}

int
hostapd_validate_mbssid_group_repurpose_mode_extn(struct hostapd_data *hapd)
{
	struct hostapd_data *txbss;

	txbss = hostapd_mbssid_get_tx_bss(hapd);
	if (!txbss || !txbss->conf) {
		wpa_printf(MSG_ERROR,
			   "BSS %s of MBSSID group txbss or txbss->conf is NULL",
			   hapd->conf->iface);
		return -1;
	}

	if (hapd == txbss)
		return 0;

	if (txbss->conf->bss_extn.repurpose_mode == hapd->conf->bss_extn.repurpose_mode)
		return 0;

	wpa_printf(MSG_ERROR,
		   "Repurpose mode mismatch txbss: %s and non tx bss: %s txbss mode %s non tx bss mode %s",
		   txbss->conf->iface, hapd->conf->iface,
		   hostapd_repurpose_mode_str_extn(txbss->conf->bss_extn.repurpose_mode),
		   hostapd_repurpose_mode_str_extn(hapd->conf->bss_extn.repurpose_mode));
	return -1;
}

struct hostapd_data *
hostapd_get_non_repurposed_link_of_mld_extn(struct hostapd_data *hapd)
{
	struct hostapd_data *link_hapd;

	/* Use for_each_mld_link as it loops only non-repurposed links */
	for_each_mld_link(link_hapd, hapd) {
		return link_hapd;
	}

	return NULL;
}
