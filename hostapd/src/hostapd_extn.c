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

enum oper_chan_width hostapd_get_oper_chwidth_from_width_extn(u16 width)
{
	enum oper_chan_width oper_chwidth;

	switch (width) {
	case 320:
		oper_chwidth = CONF_OPER_CHWIDTH_320MHZ;
		break;
	case 160:
		oper_chwidth = CONF_OPER_CHWIDTH_160MHZ;
		break;
	case 80:
		oper_chwidth = CONF_OPER_CHWIDTH_80MHZ;
		break;
	case 40:
	case 20:
		oper_chwidth = CONF_OPER_CHWIDTH_USE_HT;
		break;
	default:
		oper_chwidth = CONF_OPER_CHWIDTH_USE_HT;
	}

	return oper_chwidth;
}

u16 hostapd_get_width_from_oper_chwidth_extn(enum oper_chan_width oper_chwidth,
					     int secondary_channel)
{
	u16 width;

	switch (oper_chwidth) {
	case CONF_OPER_CHWIDTH_320MHZ:
		width = 320;
		break;
	case CONF_OPER_CHWIDTH_160MHZ:
	case CONF_OPER_CHWIDTH_80P80MHZ:
		width = 160;
		break;
	case CONF_OPER_CHWIDTH_80MHZ:
		width = 80;
		break;
	case CONF_OPER_CHWIDTH_USE_HT:
		if (secondary_channel)
			width = 40;
		else
			width = 20;
		break;
	default:
		width = 20;
	}

	return width;
}

int hostapd_validate_mbssid_configuration_extn(struct hostapd_data *hapd)
{
	if (!hapd->conf->mld_ap)
		return 0;

	if (hapd->iconf->mbssid == ENHANCED_MBSSID_ENABLED &&
	    hapd->iface->num_bss > EMA_MLO_BSS_MAX_LIMIT) {
		wpa_printf(MSG_ERROR,
			   "Number of BSS (%zu) exceeds the EMA MLO BSS limit (%d)",
			   hapd->iface->num_bss, EMA_MLO_BSS_MAX_LIMIT);
		return -1;
	}

	if ((hapd->iface->conf->group_size == MULTI_MBSSID_GROUP_SIZE_MAX) &&
	    (hapd->iface->max_mgmt_frm_sz < MGMT_MIN_FRAME_SIZE_REQUIRED_MLO_MBSSID)) {
		wpa_printf(MSG_ERROR,
			   "Invalid MBSSID group size (%u) for MLD AP with mgmt frame size (%d)",
			   hapd->iface->conf->group_size, MGMT_MIN_FRAME_SIZE_REQUIRED_MLO_MBSSID);
		return -1;
	}

	return 0;
}

bool hostapd_is_mesh_vap_extn(struct hostapd_bss_config *conf)
{
	return conf->bss_extn.vap_submode == QCA_WLAN_VENDOR_ATTR_VAP_SUBMODE_MESH;
}

/* Returns the reserved trailing mesh MBSSID group, if any. The slot is
 * retained (and its is_mesh_group marker stays set) across mesh VAP
 * remove so it can be reused on the next mesh VAP add instead of
 * growing multi_mbssid->group[] again. */
struct hostapd_multi_mbssid_group *
hostapd_get_mesh_group_extn(struct hostapd_multi_mbssid *multi_mbssid)
{
	struct hostapd_multi_mbssid_group *group;

	if (!multi_mbssid->group || multi_mbssid->num_mbssid_groups < 1)
		return NULL;

	group = multi_mbssid->group[multi_mbssid->num_mbssid_groups - 1];
	if (group && group->is_mesh_group)
		return group;

	return NULL;
}

bool hostapd_has_mesh_vap_in_group_extn(struct hostapd_data *hapd,
					struct hostapd_multi_mbssid *multi_mbssid)
{
	struct hostapd_multi_mbssid_group *group;

	if (!hapd || !hapd->conf)
		return false;

	if (hapd->iconf->mbssid != MULTI_MBSSID_GROUP_ENABLED)
		return false;

	group = hostapd_get_mesh_group_extn(multi_mbssid);
	if (group) {
		wpa_printf(MSG_DEBUG,
			   "Found mesh MBSSID group %zu (occupied=%d)",
			   (multi_mbssid->num_mbssid_groups - 1), group->num_bss > 0);
		return true;
	}

	return false;
}

/* If mesh vap is the 1st vap to come up, adjust the num_mbssid_group to
 * include the mesh group and assign the last group for the mesh vap. */
void hostapd_mesh_mbssid_reserve_group_extn(struct hostapd_multi_mbssid *multi_mbssid,
					    u8 max_bssid_indicator,
					    u8 *group_index, u64 *prefix_mask)
{
	multi_mbssid->num_mbssid_groups++;

	*group_index = multi_mbssid->num_mbssid_groups - 1;

	wpa_printf(MSG_INFO, "Mesh vap detected, assigning to last group %d",
		   *group_index);

	*prefix_mask = UINT64_MAX << max_bssid_indicator;
}

/* Reject only if the mesh MBSSID group already has an active mesh VAP. A
 * reserved-but-emptied slot, retained after a previous mesh VAP removal,
 * is meant to be reused rather than rejected. */
bool hostapd_mesh_mbssid_reject_duplicate_extn(struct hostapd_data *hapd,
					       struct hostapd_multi_mbssid *multi_mbssid)
{
	struct hostapd_multi_mbssid_group *mesh_group =
		hostapd_get_mesh_group_extn(multi_mbssid);

	if (mesh_group && mesh_group->num_bss) {
		wpa_printf(MSG_ERROR,
			   "Failed to add %s: Mesh vap MBSSID group exists already",
			   hapd->conf->iface);
		return true;
	}

	return false;
}

/* If mesh VAP is being added and the group array was allocated before the
 * mesh VAP existed, reuse a previously retained empty mesh slot, or
 * reallocate to accommodate a new last group for the mesh VAP. The
 * reallocation case handles AP VAPs being brought up first, then the mesh
 * VAP being added later. Returns -1 on allocation failure. */
int hostapd_mesh_mbssid_grow_or_reuse_group_extn(struct hostapd_data *hapd,
						 struct hostapd_multi_mbssid *multi_mbssid,
						 u8 max_bssid_indicator,
						 u8 *group_index, u64 *prefix_mask)
{
	struct hostapd_multi_mbssid_group *mesh_group =
		hostapd_get_mesh_group_extn(multi_mbssid);
	struct hostapd_multi_mbssid_group **new_group;

	if (mesh_group) {
		/* A previous mesh VAP remove left this slot allocated but
		 * empty; reuse it instead of growing the group array again. */
		*group_index = mesh_group->group_id;
		*prefix_mask = UINT64_MAX << max_bssid_indicator;
		wpa_printf(MSG_INFO, "Mesh vap detected: %s, reusing retained group %d",
			   hapd->conf->iface, *group_index);
		return 0;
	}

	new_group = os_realloc_array(multi_mbssid->group,
				     multi_mbssid->num_mbssid_groups + 1,
				     sizeof(struct hostapd_multi_mbssid_group *));

	if (!new_group) {
		wpa_printf(MSG_ERROR, "Failed to allocate MBSSID group for mesh VAP");
		return -1;
	}

	multi_mbssid->num_mbssid_groups++;
	*group_index = multi_mbssid->num_mbssid_groups - 1;
	wpa_printf(MSG_INFO, "Mesh vap detected: %s, assigning to last group %d",
		   hapd->conf->iface, *group_index);

	*prefix_mask = UINT64_MAX << max_bssid_indicator;
	multi_mbssid->group = new_group;
	multi_mbssid->group[multi_mbssid->num_mbssid_groups - 1] = NULL;

	return 0;
}

void hostapd_mesh_mbssid_mark_group_extn(struct hostapd_data *hapd,
					 struct hostapd_multi_mbssid_group *group)
{
	group->is_mesh_group = true;
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

void hostapd_override_ht_capabilities_extn(const struct hostapd_data *hapd,
					   struct ieee80211_ht_capabilities *cap)
{
	if (!hapd || !hapd->conf || !cap)
		return;

	if (hapd->conf->bss_extn.ht40_intol.is_overridden) {
		if (hapd->conf->bss_extn.ht40_intol.value)
			cap->ht_capabilities_info |= HT_CAP_INFO_40MHZ_INTOLERANT;
		else
			cap->ht_capabilities_info &= ~HT_CAP_INFO_40MHZ_INTOLERANT;
	}
}
