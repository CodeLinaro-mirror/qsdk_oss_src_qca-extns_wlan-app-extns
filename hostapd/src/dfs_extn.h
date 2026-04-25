/* SPDX-License-Identifier: BSD-3-Clause */
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*/

#ifndef DFS_EXTN_H
#define DFS_EXTN_H

#ifndef CONFIG_QCN_EXTN
#else

/* Channel Switch Announcement element (WLAN_EID_CHANNEL_SWITCH) fixed layout. */
#define IEEE80211_CSA_IE_MIN_LEN 3
#define IEEE80211_CSA_IE_MODE_OFFSET 2
#define IEEE80211_CSA_IE_NEW_CHANNEL_OFFSET 3
#define IEEE80211_CSA_IE_COUNT_OFFSET 4
#define IEEE80211_CSA_IE_TOTAL_LEN 5

/* NOL IE structure for uplink CSA */
struct dfs_nol_ie_info {
	u32 freq;              /* Center frequency in MHz */
	u32 bandwidth;         /* Bandwidth in MHz (20, 40, 80, 160, 320) */
	u16 subchan_bitmap;    /* Bitmap of affected 20MHz subchannels */
};

/* NOL IE list for multiple radar detections */
struct dfs_nol_ie_list {
	struct dfs_nol_ie_info *entries;
	size_t count;
};

int dfs_prepare_nol_ie_bitmap(struct hostapd_iface *iface, int freq,
			      int chan_width, int cf1, int cf2,
			      u16 radar_bitmap,
			      struct dfs_nol_ie_info *nol_info);
int dfs_process_nol_ie_bitmap(struct hostapd_iface *iface,
			      struct dfs_nol_ie_list *nol_list);
int dfs_decode_nol_ie(const u8 *ie, size_t ie_len,
		      struct dfs_nol_ie_list *nol_list);
void dfs_free_nol_ie_list(struct dfs_nol_ie_list *nol_list);
int dfs_encode_nol_ie(struct dfs_nol_ie_list *nol_list, u8 *buf,
		      size_t buf_len);
int hostapd_dfs_restart_channel_extn(struct hostapd_iface *iface);
bool hostapd_is_backhaul_sta_configured(struct hostapd_iface *iface);
void hostapd_trigger_backhaul_sta_disconnect(void *eloop_data, void *user_data);
void hostapd_rcsa_trigger_channal_change(void *eloop_data, void *user_data);
void hostapd_trigger_rcsa_tx(void *eloop_data, void *user_data);
bool hostapd_rcsa_tx_bh_enabled(struct hostapd_iface *iface);
void hostapd_rcsa_handle_csa_timeout(struct hostapd_iface *iface);
void hostapd_set_rcsa_inprogress(struct hostapd_iface *iface, bool value);

/**
 * dfs_get_ch_flags_extn - Compute DFS random channel selection flags
 * @cswopts: Channel Switch Options bitmap from hostapd/wpa_supplicant config
 *
 * Always sets DFS_RANDOM_CH_FLAG_NO_CURR_OPE_CH.
 * Sets DFS_RANDOM_CH_FLAG_NO_DFS_CH when CSwOpts requests it.
 *
 * Returns the ch_flags bitmask to pass to dfs_get_valid_channel().
 */
unsigned int dfs_get_ch_flags_extn(unsigned int cswopts);

/**
 * dfs_chan_skip_by_flags_extn - Check whether a channel should be skipped
 * during random DFS channel selection
 * @iface: Pointer to hostapd_iface
 * @chan: Candidate channel to evaluate
 * @flags: DFS_RANDOM_CH_FLAG_* bitmask from dfs_get_ch_flags_extn()
 *
 * Evaluates the two flag-driven skip conditions introduced by the enhanced
 * random channel selection algorithm:
 *
 *   DFS_RANDOM_CH_FLAG_NO_CURR_OPE_CH: skip @chan if it is one of the 20 MHz
 *     sub-channels that make up the current operating channel.  The set of
 *     sub-channels is determined by calling dfs_get_start_chan_idx() and
 *     dfs_get_used_n_chans() and then iterating over
 *     mode->channels[start_chan_idx + i].
 *
 *   DFS_RANDOM_CH_FLAG_NO_DFS_CH: skip @chan if it is a DFS/radar channel.
 *
 * Returns: true if @chan should be skipped, false otherwise.
 */
bool dfs_chan_skip_by_flags_extn(struct hostapd_iface *iface,
				 struct hostapd_channel_data *chan,
				 unsigned int flags);

#endif /* CONFIG_QCN_EXTN */
#endif /* DFS_EXTN_H */
