/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "includes.h"
#include <netlink/genl/genl.h>
#include "common.h"
#include "common/qca-vendor.h"
#include "drivers/driver_nl80211.h"
#include "ap/hostapd.h"
#include "ap/hw_features.h"
#include "reg_extn.h"

void hostapd_query_hw_blocklist_extn(struct hostapd_iface *iface,
				     struct hostapd_data *hapd)
{
	struct hostapd_multi_hw_info *hw_info = NULL;
	int radio_idx = -1;
	int ret;

	if (!iface || !hapd)
		return;

	if (!hapd->driver || !hapd->drv_priv ||
	    !hapd->driver->is_6ghz_hw_blocked_chans_supported ||
	    !hapd->driver->fetch_hw_blocked_chans)
		return;

	if (!hapd->driver->is_6ghz_hw_blocked_chans_supported(hapd->drv_priv))
		return;

	if (iface->freq && iface->num_multi_hws && iface->multi_hw_info)
		hw_info = hostapd_get_current_hw_info(iface, iface->freq);

	if (iface->freq && hw_info) {
		radio_idx = hw_info->hw_idx;
		wpa_printf(MSG_DEBUG,
			   "Query HW blocklist for freq=%d hw_idx=%d",
			   iface->freq, radio_idx);
	} else if (iface->freq) {
		wpa_printf(MSG_DEBUG,
			   "Unable to map freq=%d to hw_idx, query HW blocklist for all radios",
			   iface->freq);
	} else {
		wpa_printf(MSG_DEBUG,
			   "Frequency is unset, query HW blocklist for all radios");
	}

	ret = hapd->driver->fetch_hw_blocked_chans(hapd->drv_priv,
						      radio_idx);
	if (ret) {
		wpa_printf(MSG_DEBUG,
			   "Failed to fetch HW blocklist channels (radio_idx=%d ret=%d)",
			   radio_idx, ret);
		return;
	}
}

static void hw_blocklist_free_entry_extn(
	struct hostapd_hw_blocklist_info *hw_blocklist_info)
{
	u32 i;

	if (!hw_blocklist_info)
		return;

	for (i = 0; i < hw_blocklist_info->num_pwr_modes; i++) {
		os_free(hw_blocklist_info->pwr_modes[i].fb_chans);
		os_free(hw_blocklist_info->pwr_modes[i].pc_chans);
	}
	os_free(hw_blocklist_info->pwr_modes);
	hw_blocklist_info->pwr_modes = NULL;
	hw_blocklist_info->num_pwr_modes = 0;
}

void hostapd_free_hw_blocklist_info_extn(
	struct hostapd_hw_blocklist_info *hw_blocklist_info,
	unsigned int num_hw_blocklist)
{
	unsigned int i;

	if (!hw_blocklist_info)
		return;

	for (i = 0; i < num_hw_blocklist; i++)
		hw_blocklist_free_entry_extn(&hw_blocklist_info[i]);

	os_free(hw_blocklist_info);
}

static int hw_blocklist_copy_entry_extn(
	struct hostapd_hw_blocklist_info *dst,
	const struct hostapd_hw_blocklist_info *src)
{
	u32 i;

	os_memset(dst, 0, sizeof(*dst));
	dst->hw_idx = src->hw_idx;

	if (!src->num_pwr_modes || !src->pwr_modes)
		return 0;

	dst->pwr_modes = os_zalloc(src->num_pwr_modes * sizeof(*dst->pwr_modes));
	if (!dst->pwr_modes)
		return -ENOMEM;

	for (i = 0; i < src->num_pwr_modes; i++) {
		const struct hostapd_hw_blocklist_pwr_mode *src_mode;
		struct hostapd_hw_blocklist_pwr_mode *dst_mode;

		src_mode = &src->pwr_modes[i];
		dst_mode = &dst->pwr_modes[i];
		dst_mode->pwr_mode_id = src_mode->pwr_mode_id;
		dst_mode->num_fb_chans = src_mode->num_fb_chans;
		dst_mode->num_pc_chans = src_mode->num_pc_chans;

		if (src_mode->num_fb_chans && src_mode->fb_chans) {
			dst_mode->fb_chans = os_memdup(
				src_mode->fb_chans,
				src_mode->num_fb_chans *
					sizeof(*src_mode->fb_chans));
			if (!dst_mode->fb_chans)
				goto fail;
		}

		if (src_mode->num_pc_chans && src_mode->pc_chans) {
			dst_mode->pc_chans = os_memdup(
				src_mode->pc_chans,
				src_mode->num_pc_chans *
					sizeof(*src_mode->pc_chans));
			if (!dst_mode->pc_chans)
				goto fail;
		}
	}

	dst->num_pwr_modes = src->num_pwr_modes;
	return 0;

fail:
	hw_blocklist_free_entry_extn(dst);
	return -ENOMEM;
}

static int hw_blocklist_update_list_extn(
	struct hostapd_hw_blocklist_info **hw_blocklist_info,
	unsigned int *num_hw_blocklist,
	const struct hostapd_hw_blocklist_info *new_entry)
{
	struct hostapd_hw_blocklist_info *new_hw_blocklist_info;
	struct hostapd_hw_blocklist_info copy;
	unsigned int i;
	int ret;

	if (!hw_blocklist_info || !num_hw_blocklist || !new_entry)
		return -EINVAL;

	ret = hw_blocklist_copy_entry_extn(&copy, new_entry);
	if (ret)
		return ret;

	for (i = 0; i < *num_hw_blocklist; i++) {
		if ((*hw_blocklist_info)[i].hw_idx == copy.hw_idx) {
			hw_blocklist_free_entry_extn(&(*hw_blocklist_info)[i]);
			(*hw_blocklist_info)[i] = copy;
			return 0;
		}
	}

	new_hw_blocklist_info = os_realloc_array(
		*hw_blocklist_info, *num_hw_blocklist + 1,
		sizeof(*new_hw_blocklist_info));
	if (!new_hw_blocklist_info) {
		hw_blocklist_free_entry_extn(&copy);
		return -ENOMEM;
	}

	*hw_blocklist_info = new_hw_blocklist_info;
	(*hw_blocklist_info)[*num_hw_blocklist] = copy;
	(*num_hw_blocklist)++;
	return 0;
}

static void hostapd_reg_dump_hw_blocklist_extn(struct hostapd_iface *iface)
{
	const char *ifname = "unknown";
	unsigned int i, j, k;

	if (!iface)
		return;

	if (iface->num_bss > 0 && iface->bss && iface->bss[0] &&
	    iface->bss[0]->conf && iface->bss[0]->conf->iface[0] != '\0')
		ifname = iface->bss[0]->conf->iface;

	wpa_printf(MSG_DEBUG,
		   "HW blocklist dump on %s: radios=%u",
		   ifname, iface->iface_extn.num_hw_blocklist);

	if (!iface->iface_extn.hw_blocklist_info || !iface->iface_extn.num_hw_blocklist)
		return;

	for (i = 0; i < iface->iface_extn.num_hw_blocklist; i++) {
		const struct hostapd_hw_blocklist_info *info;

		info = &iface->iface_extn.hw_blocklist_info[i];
		wpa_printf(MSG_DEBUG,
			   "HW blocklist[%u]: hw_idx=%u pwr_modes=%u",
			   i, info->hw_idx, info->num_pwr_modes);

		for (j = 0; j < info->num_pwr_modes; j++) {
			const struct hostapd_hw_blocklist_pwr_mode *mode;

			mode = &info->pwr_modes[j];
			wpa_printf(MSG_DEBUG,
				   "HW blocklist[%u]: pwr_mode[%u]=%u fb_chans=%u pc_chans=%u",
				   i, j, mode->pwr_mode_id,
				   mode->num_fb_chans, mode->num_pc_chans);

			for (k = 0; k < mode->num_fb_chans; k++) {
				const struct hostapd_hw_blocklist_fb_chan *fb;

				fb = &mode->fb_chans[k];
				wpa_printf(MSG_DEBUG,
					   "HW blocklist[%u]: FB[%u] pri20_bitmap=0x%x center_freq=%u max_bw=%u",
					   i, k, fb->pri20_bitmap,
					   fb->center_freq, fb->max_bw);
			}

			for (k = 0; k < mode->num_pc_chans; k++) {
				const struct hostapd_hw_blocklist_pc_chan *pc;

				pc = &mode->pc_chans[k];
				wpa_printf(MSG_DEBUG,
					   "HW blocklist[%u]: PC[%u] center_freq=%u max_bw=%u puncture_pattern_bitmap=0x%x",
					   i, k, pc->center_freq,
					   pc->max_bw, pc->punc_pat_bmap);
			}
		}
	}
}

void hostapd_event_hw_blocklist_notify_extn(
	struct hostapd_data *hapd,
	const struct hostapd_hw_blocklist_info *hw_blocklist_info)
{
	if (!hapd || !hapd->iface || !hw_blocklist_info)
		return;

	if (hw_blocklist_update_list_extn(
		    &hapd->iface->iface_extn.hw_blocklist_info,
		    &hapd->iface->iface_extn.num_hw_blocklist,
		    hw_blocklist_info)) {
		wpa_printf(MSG_ERROR,
			   "Failed to store HW blocklist info for hw_idx=%u",
			   hw_blocklist_info->hw_idx);
		return;
	}

	wpa_printf(MSG_DEBUG,
		   "Stored HW blocklist info for hw_idx=%u on %s (modes=%u, total_hw=%u)",
		   hw_blocklist_info->hw_idx, hapd->conf->iface,
		   hw_blocklist_info->num_pwr_modes,
		   hapd->iface->iface_extn.num_hw_blocklist);

	hostapd_reg_dump_hw_blocklist_extn(hapd->iface);
}
