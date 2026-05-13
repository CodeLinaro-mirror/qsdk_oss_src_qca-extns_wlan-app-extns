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
#endif /* CONFIG_QCN_EXTN */
#endif /* DFS_EXTN_H */
