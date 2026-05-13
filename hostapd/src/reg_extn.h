/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef REG_EXTN_H
#define REG_EXTN_H

#include "includes.h"
#include "common/qca-vendor.h"

struct hostapd_iface;
struct hostapd_data;
struct hostapd_hw_blocklist_info;
struct wpa_supplicant;

/**
 * struct hostapd_hw_blocklist_fb_chan - HW blocklist full-bandwidth channel
 * @max_bw: Maximum blocked bandwidth encoded as enum nl80211_chan_width
 * @center_freq: Center frequency in MHz
 * @pri20_bitmap: Primary 20 MHz bitmap
 */
struct hostapd_hw_blocklist_fb_chan {
	u32 max_bw;
	u16 center_freq;
	u16 pri20_bitmap;
};

/**
 * struct hostapd_hw_blocklist_pc_chan - HW blocklist punctured channel
 * @max_bw: Maximum blocked bandwidth encoded as enum nl80211_chan_width
 * @center_freq: Center frequency in MHz
 * @punc_pat_bmap: Bitmap of puncture patterns where bit N maps to
 *	puncture pattern index N for the selected @max_bw
 */
struct hostapd_hw_blocklist_pc_chan {
	u32 max_bw;
	u16 center_freq;
	u32 punc_pat_bmap;
};

/**
 * struct hostapd_hw_blocklist_pwr_mode - HW blocklist data for one power mode
 * @pwr_mode_id: 6 GHz AP power mode ID (0=LPI, 1=SP, 2=VLP)
 * @num_fb_chans: Number of full-bandwidth channels
 * @num_pc_chans: Number of punctured channels
 * @fb_chans: Full-bandwidth channel list
 * @pc_chans: Punctured channel list
 */
struct hostapd_hw_blocklist_pwr_mode {
	u8 pwr_mode_id;
	u32 num_fb_chans;
	u32 num_pc_chans;
	struct hostapd_hw_blocklist_fb_chan *fb_chans;
	struct hostapd_hw_blocklist_pc_chan *pc_chans;
};

/**
 * struct hostapd_hw_blocklist_info - HW blocklist info for one radio
 * @hw_idx: Hardware index
 * @num_pwr_modes: Number of power mode entries
 * @pwr_modes: Per-power-mode blocklist information
 */
struct hostapd_hw_blocklist_info {
	u8 hw_idx;
	u32 num_pwr_modes;
	struct hostapd_hw_blocklist_pwr_mode *pwr_modes;
};

void hostapd_event_hw_blocklist_notify_extn(struct hostapd_data *hapd,
		const struct hostapd_hw_blocklist_info *hw_blocklist_info);

void wpas_event_hw_blocklist_notify_extn(
	struct wpa_supplicant *wpa_s,
	const struct hostapd_hw_blocklist_info *hw_blocklist_info);

#endif /* REG_EXTN_H */
