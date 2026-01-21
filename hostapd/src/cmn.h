/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CMN_H
#define CMN_H

struct hostapd_config;
struct sta_info;
struct hostapd_iface;
struct nl_msg;
struct hostapd_channel_data;
enum ieee80211_op_mode;
struct hostapd_sta_add_params;
enum bw_type;
struct hostapd_freq_params;
struct hostapd_data;
struct ieee802_11_elems;
enum oper_chan_width;
enum chan_width;
struct wpa_ctrl;
struct hostapd_bss_config;
struct i802_bss;
enum wpa_event_type;
struct nlattr;
struct wpa_ctrl;
union wpa_event_data;

struct ieee80211_240mhz_vendor_oper_extn {
	u8 ccfs1;
	u8 ccfs0;
	u16 punct_bitmap;
	u16 is5ghz240mhz          :1,
	    bfmess320mhz          :3,
	    numsound320mhz        :3,
	    nonofdmaulmumimo320mhz:1,
	    mubfmr320mhz          :1;
	u8 mcs_map_320mhz[3];
} STRUCT_PACKED;

struct ieee80211_240mhz_params_extn {
	struct ieee80211_240mhz_vendor_oper_extn *eht_240mhz_capab;
	size_t eht_240mhz_capab_len;
};

struct  hostapd_sta_add_params_extn {
	struct ieee80211_240mhz_params_extn params_240mhz;
};

struct sta_info_extn {
	struct ieee80211_240mhz_params_extn params_240mhz;
};

/**
 * struct esp_update_event - Data for EVENT_ESP_UPDATE
 * @link_id: Link for which ESP airtime update was received
 * @airtime: Airtime fraction computed in the firmware
 */
struct esp_update_event {
	u8 link_id;
	u8 airtime;
};

union wpa_event_data_extn {
	struct esp_update_event esp_update_event;
};

struct ieee802_11_elems_extn {
	const u8 *eht_240mhz_capab;
	u8 eht_240mhz_capab_len;
};

struct hostapd_config_extn {
	/* Add Per-radio configuration for extn here */

	/* Manages RNR advertisement of 6 GHz BSS information for both
	 * in-band and out-of-band for each frame type includes Beacon,
	 * Probe Response and FILS discovery frame.
	 */
	u8 rnr_6ghz_colocated_enable;
	bool rnr_ess_colocated_en;
	bool rnr_6ghz_override;
};

struct hostapd_bss_config_extn {
	/* Add Per-BSS configuration for extn here */
};

struct esp_extn {
	u8 airtime;
	u8 ppdu_dur;
	u8 ba_window;
	u8 enable;
	u32 computed_airtime;
};

struct hostapd_iface_extn {
	struct esp_extn esp;
};

#ifndef CONFIG_QCN_EXTN

static inline void
hostapd_get_oper_center_freq_seg_extn(struct hostapd_config *conf,
				      u8 *oper_centr_freq_seg0_idx,
				      u8 *oper_centr_freq_seg1_idx,
				      enum oper_chan_width *oper_chwidth)
{
	return;
}

static inline u8
hostapd_set_legacy_oper_centr_freq_seg0_extn(struct hostapd_config *conf,
					     u8 oper_centr_freq_seg0_idx)
{
	return oper_centr_freq_seg0_idx;
}

static inline int
hostapd_modify_n_chans_for_240mhz_extn(struct hostapd_iface *iface,
				       int n_chans)
{
	return n_chans;
}

static inline int
hostapd_modify_supported_op_class_for_240mhz_extn(int freq,
						  enum oper_chan_width ch_width,
						  u8 *op_class)
{
	return -1;
}

static inline void
hostapd_modify_buflen_for_240mhz_extn(size_t *buflen,
				      struct hostapd_data *hapd)
{
	return;
}

static inline int
hostapd_get_n_chans_and_frequency_extn(enum oper_chan_width oper_chwidth,
				       int cf1,
				       int *n_chans,
				       int *frequency)
{
	return -1;
}

static inline int hostapd_get_dfs_half_chwidth_extn(enum chan_width width)
{
	return 0;
}

static inline int
hostapd_dfs_get_allowed_channels_extn(int n_chans,
				      int *is_allowed,
				      unsigned int *allowed_no)
{
	return -1;
}

static inline int
hostapd_dfs_adjust_center_freq_extn(int oper_chwidth,
				    short chan,
				    u8 *oper_centr_freq_seg0_idx,
				    u8 *oper_centr_freq_seg1_idx)
{
	return -1;
}

static inline int
hostapd_get_bw_and_startchan_for_240mhz_extn(enum oper_chan_width
					     eht_oper_chwidth,
					     u8 eht_oper_centr_freq_seg0_idx,
					     u16 *bw, u8 *start_chan)
{
	return -1;
}

static inline u8 *
hostapd_eid_vendor_240mhz_extn(struct hostapd_data *hapd, u8 *eid,
			       int opmode)
{
	return eid;
}

static inline u16
hostapd_copy_sta_eht_240mhz_cap_extn(struct hostapd_data *hapd,
				     struct sta_info *sta,
				     int opmode,
				     struct ieee802_11_elems_extn *elems_extn)
{
	return 0;
}

static inline void
hostapd_get_eht_240mhz_cap_extn(struct hostapd_data *hapd,
				struct sta_info_extn *sta_extn,
				struct ieee80211_240mhz_vendor_oper_extn *dest)
{
	return;
}

static inline void hostapd_sta_os_free_extn(struct sta_info_extn *sta_extn)
{
	return;
}

static inline int
ieee802_11_parse_vendor_specific_eht_240mhz_cap_extn(struct ieee802_11_elems
						     *elems,
						     unsigned int oui_flag,
						     const u8 *pos,
						     size_t elen)
{
	return -1;
}

static inline int
ieee802_11_parse_vendor_specific_elems_extn(struct ieee802_11_elems *elems,
					    unsigned int oui_flag,
					    const u8 *pos, size_t elen)
{
	return -1;
}

static inline void
hostapd_copy_sta_add_params_extn(struct hostapd_sta_add_params_extn
				 *params_extn,
				 struct sta_info_extn *sta_extn)
{
	return;
}

static inline void
wpa_driver_nl80211_sta_add_extn(void *priv,
				struct hostapd_sta_add_params
				*params)
{
	return;
}

static inline int
hostapd_drv_fetch_and_set_vendor_bssid_extn(struct hostapd_data *hapd)
{
	return -1;
}

static inline void
hostapd_free_bss_index_extn(struct hostapd_data *hapd)
{
	return;
}

static inline bool
hostapd_dfs_get_valid_punc_bitmap_extn(int chan_freq,
				       u16 punct_bitmap,
				       int center_freq,
				       int half_width)
{
	return false;
}

static inline int
hostapd_find_dfs_range_extn(struct hostapd_iface *iface,
			    enum chan_width bandwidth,
			    struct hostapd_freq_params *freq_params)
{
	return -1;
}

static inline int
hostapd_is_dfs_overlap_extn(struct hostapd_iface *iface,
			    enum chan_width width,
			    int center_freq, u16 punct_bitmap)
{
	return -1;
}

static inline void
hostapd_modify_supported_op_class_for_320mhz_extn(int freq,
						  u8 *op_class)
{
	return;
}

static inline bool
hostapd_skip_rnr_6ghz_colocated_extn(struct hostapd_data *hapd, u32 type)
{
	return false;
}

static inline bool
hostapd_rnr_colocated_ess_indication_extn(struct hostapd_data *hapd, u32 type)
{
	return false;
}

static inline int
hostapd_ctrl_iface_receive_process_extn(struct hostapd_data *hapd,
					char *buf, char *reply,
					int reply_size,
					struct sockaddr_storage *from,
					socklen_t fromlen, int *reply_len)
{
	return -EOPNOTSUPP;
}

static inline void
hostapd_config_defaults_extn(struct hostapd_config *conf)
{
	return;
}

static inline int
hostapd_config_fill_extn(struct hostapd_config *conf,
			 struct hostapd_bss_config *bss,
			 const char *buf, char *pos, int line)
{
	return -EOPNOTSUPP;
}

static inline int
hostapd_ctrl_iface_set_extn(struct hostapd_data *hapd, char *cmd, char *value)
{
	return -EOPNOTSUPP;
}

static inline int
hostapd_ctrl_iface_status_extn(struct hostapd_data *hapd, char *buf,
			       size_t buflen, size_t curr_len)
{
	return curr_len;
}

static inline int
nl80211_vendor_event_qca_extn(struct i802_bss *bss,
			      u32 subcmd, u8 *data, size_t len)
{
	return -1;
}

static inline int
hostapd_wpa_event_extn(void *ctx, int event,
		       union wpa_event_data *data)
{
	return -1;
}

static inline int
qca_nl80211_handle_wifi_config_evt_extn(struct i802_bss *bss,
					u8 *data, size_t len)
{
	return -1;
}

static inline
u8 * hostapd_eid_esp_extn(struct hostapd_data *hapd, u8 *eid, size_t len)
{
       return eid;
}

static inline
size_t hostapd_esp_ie_len_extn(struct hostapd_data *hapd)
{
       return 0;
}

#else

void hostapd_get_oper_center_freq_seg_extn(struct hostapd_config *conf,
					   u8 *oper_centr_freq_seg0_idx,
					   u8 *oper_centr_freq_seg1_idx,
					   enum oper_chan_width *oper_chwidth);
u8 hostapd_set_legacy_oper_centr_freq_seg0_extn(struct hostapd_config *conf,
						u8 oper_centr_freq_seg0_idx);
int
hostapd_acs_update_puncturing_bitmap_extn(struct hostapd_config *conf,
					  u16 bw,
					  struct hostapd_channel_data *chan);
int hostapd_modify_n_chans_for_240mhz_extn(struct hostapd_iface *iface,
					   int n_chans);
int
hostapd_modify_supported_op_class_for_240mhz_extn(int freq,
						  enum oper_chan_width ch_width,
						  u8 *op_class);
void hostapd_modify_buflen_for_240mhz_extn(size_t *buflen,
					   struct hostapd_data *hapd);
int hostapd_dfs_get_start_chan_idx_extn(struct hostapd_iface *iface);
int hostapd_get_n_chans_and_frequency_extn(enum oper_chan_width oper_chwidth,
					   int cf1,
					   int *n_chans,
					   int *frequency);
int hostapd_get_dfs_half_chwidth_extn(enum chan_width width);
int hostapd_dfs_get_allowed_channels_extn(int n_chans,
					  int *is_allowed,
					  unsigned int *allowed_no);
int hostapd_dfs_adjust_center_freq_extn(int oper_chwidth,
					short chan,
					u8 *oper_centr_freq_seg0_idx,
					u8 *oper_centr_freq_seg1_idx);
int
hostapd_get_bw_and_startchan_for_240mhz_extn(enum oper_chan_width
					     eht_oper_chwidth,
					     u8 eht_oper_centr_freq_seg0_idx,
					     u16 *bw, u8 *start_chan);
u8 * hostapd_eid_vendor_240mhz_extn(struct hostapd_data *hapd, u8 *eid,
				    enum ieee80211_op_mode opmode);
u16
hostapd_copy_sta_eht_240mhz_cap_extn(struct hostapd_data *hapd,
				     struct sta_info *sta,
				     enum ieee80211_op_mode opmode,
				     struct ieee802_11_elems_extn *elems_extn);
void hostapd_get_eht_240mhz_cap_extn(struct hostapd_data *hapd,
				     struct sta_info_extn *sta_extn,
				     struct ieee80211_240mhz_vendor_oper_extn
				     *dest);
void hostapd_sta_os_free_extn(struct sta_info_extn *sta_extn);
int
ieee802_11_parse_vendor_specific_eht_240mhz_cap_extn(struct ieee802_11_elems
						     *elems,
						     unsigned int oui_flag,
						     const u8 *pos,
						     size_t elen);
int ieee802_11_parse_vendor_specific_elems_extn(struct ieee802_11_elems *elems,
						unsigned int oui_flag,
						const u8 *pos, size_t elen);
void hostapd_copy_sta_add_params_extn(struct hostapd_sta_add_params_extn
				      *params_extn,
				      struct sta_info_extn *sta_extn);
void wpa_driver_nl80211_sta_add_extn(void *priv,
				     struct hostapd_sta_add_params *params);
int hostapd_drv_fetch_and_set_vendor_bssid_extn(struct hostapd_data *hapd);
void hostapd_free_bss_index_extn(struct hostapd_data *hapd);
bool hostapd_dfs_get_valid_punc_bitmap_extn(int chan_freq,
					    u16 punct_bitmap,
					    int center_freq,
					    int half_width);
int hostapd_find_dfs_range_extn(struct hostapd_iface *iface,
				enum chan_width bandwidth,
				struct hostapd_freq_params *freq_params);
int hostapd_is_dfs_overlap_extn(struct hostapd_iface *iface,
				enum chan_width width,
				int center_freq, u16 punct_bitmap);
void hostapd_modify_supported_op_class_for_320mhz_extn(int freq, u8 *op_class);
bool hostapd_skip_rnr_6ghz_colocated_extn(struct hostapd_data *hapd, u32 type);
bool hostapd_rnr_6ghz_override_extn(struct hostapd_data *hapd);
bool hostapd_rnr_colocated_ess_indication_extn(struct hostapd_data *hapd);
int
hostapd_ctrl_iface_receive_process_extn(struct hostapd_data *hapd,
					char *buf, char *reply,
					int reply_size,
					struct sockaddr_storage *from,
					socklen_t fromlen, int *reply_len);
void
hostapd_config_defaults_extn(struct hostapd_config *conf);
int
hostapd_config_fill_extn(struct hostapd_config *conf,
			 struct hostapd_bss_config *bss,
			 const char *buf, char *pos, int line);
int nl80211_vendor_event_qca_extn(struct i802_bss *bss,
				  u32 subcmd, u8 *data, size_t len);
int hostapd_ctrl_iface_set_extn(struct hostapd_data *hapd, char *cmd, char *value);
int hostapd_wpa_event_extn(void *ctx, enum wpa_event_type event,
			   union wpa_event_data *data);
int hostapd_ctrl_iface_status_extn(struct hostapd_data *hapd, char *buf,
				   size_t buflen, size_t curr_len);
#ifdef HOSTAPD
struct hostapd_data *
switch_link_hapd(struct hostapd_data *hapd, int link_id);
#else
static inline struct hostapd_data *
switch_link_hapd(struct hostapd_data *hapd, int link_id)
{
    return hapd;
}
#endif
int qca_nl80211_handle_wifi_config_evt_extn(struct i802_bss *bss,
					    u8 *data, size_t len);
size_t hostapd_esp_ie_len_extn(struct hostapd_data *hapd);
u8 * hostapd_eid_esp_extn(struct hostapd_data *hapd, u8 *eid, size_t len);
size_t hostapd_esp_ie_len_extn(struct hostapd_data *hapd);

#endif /* CONFIG_QCN_EXTN */
#endif /* CMN_H */
