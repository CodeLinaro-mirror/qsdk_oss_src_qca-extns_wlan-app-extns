/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifdef UBUS_SUPPORT
#include <libubus.h>
#undef ARRAY_SIZE
#endif
#include "includes.h"
#include "utils/common.h"
#include "utils/os.h"
#include "utils/eloop.h"
#include "common/hw_features_common.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/sme.h"
#include "../wpa_supplicant/config.h"
#include "../wpa_supplicant/ubus.h"
#include "../wpa_supplicant/bss.h"
#include "../wpa_supplicant/scan.h"
#include "../wpa_supplicant/driver_i.h"
#include "wpa_supplicant_rptr_extn.h"
#include "240mhz.h"
#include "qcn_ie_extn.h"

#define SME_PRE_CONNECT_TIMEOUT 5

/**
 * wpa_ctrl_get_freq_list_extn - Get configured frequency list for interface
 * @wpa_s: Pointer to wpa_supplicant interface
 * @reply: Caller-provided buffer to hold space-separated frequency list
 * @reply_size: Size of @reply in bytes
 *
 * Serialize the configured frequency list into a space-separated string and
 * append a trailing newline. The list is taken from wpa_s->conf->freq_list
 * when present.
 *
 * Return: Number of bytes written to @reply (excluding terminating NUL),
 *         or 0 if there is no configured list.
 */
int wpa_ctrl_get_freq_list_extn(struct wpa_supplicant *wpa_s, char *reply, int reply_size)
{
	if (wpa_s->conf && wpa_s->conf->freq_list) {
		const int *f = wpa_s->conf->freq_list;
		char *pos = reply;
		char *end = reply + reply_size - 2;
		int n;

		pos[0] = '\0';
		for (; *f && pos < end; f++) {
			n = os_snprintf(pos, end - pos, "%s%d",
					(pos == reply) ? "" : " ", *f);
			if (os_snprintf_error(end - pos, n))
				break;
			pos += n;
		}
		if (pos == reply)
			return 0; /* No entries */
		*pos++ = '\n';
		return pos - reply;
	}
	return 0; /* No configured list */
}

/**
 * wpa_ctrl_chan_sw_finished_notify_extn - Handle channel switch completion
 * @wpa_s: Pointer to wpa_supplicant interface
 * @buf: Control interface buffer containing CHAN_SW_FINISHED_NOTIFY
 * @reply: Unused output buffer (reserved for future use)
 * @reply_size: Size of @reply in bytes (unused)
 *
 * Parse the frequency from the notification, decrement the outstanding
 * pre-connect counter, and schedule the cached SME connect work when all
 * pending operations have completed.
 *
 * Return: 0 on success or -1 if the frequency cannot be parsed.
 */
int wpa_ctrl_chan_sw_finished_notify_extn(struct wpa_supplicant *wpa_s, const char *buf, char *reply, int reply_size)
{
	const char *p;
	int freq;
	(void)reply;
	(void)reply_size;

	p = strstr(buf, "freq");
	if (!p)
		return -1;
	p += strlen("freq");
	while (*p == ' ')
		p++;

	if (*p != '=')
		p = strchr(p, '=');
	if (!p)
		return -1;

	p++;
	freq = atoi(p);
	wpa_s->pre_connect_cnt--;
	_wpa_msg(wpa_s, MSG_INFO,
		 "SME: RECVD - CHAN_SW_FINISHED_NOTIFY freq = %d pre_connect_cnt = %d",
		 freq, wpa_s->pre_connect_cnt);
	if (!wpa_s->pre_connect_cnt) {
		if (wpa_s && wpa_s->cache_cwork && wpa_s->cache_cwork->bss)
			sme_schedule_auth_radio_work(wpa_s, wpa_s->cache_cwork);
	}
	return 0;
}

/**
 * wpa_bss_check_5g_320mhz_vendor_ie_extn - Upgrade 5 GHz BSS to 320 MHz
 * @wpa_s: Pointer to wpa_supplicant interface
 * @bss: BSS entry to inspect and update
 *
 * Inspect the QCN vendor-specific IE in a 5 GHz BSS and, when a 240 MHz
 * capability element is present, upgrade the BSS bandwidth to 320 MHz and
 * update center frequency indices and puncturing bitmap accordingly.
 */
void wpa_bss_check_5g_320mhz_vendor_ie_extn(struct wpa_supplicant *wpa_s,
					    struct wpa_bss *bss)
{
	const struct ieee80211_240mhz_vendor_oper_extn_v2 *oper_240 = NULL;
	const u8 *vendor_ie;
	const u8 *pos;
	size_t elen;
	size_t off = 0;

	if (bss->max_cw != CHAN_WIDTH_160 || !is_5ghz_freq(bss->freq))
		return;

	/* Look for QCN Vendor Specific IE for 5G 320MHz support */
	vendor_ie = wpa_bss_get_vendor_ie(bss, QCN_IE_VENDOR_TYPE);
	if (!vendor_ie) {
		wpa_dbg(wpa_s, MSG_ERROR,
			"5G 320MHz check: QCN Vendor IE not found, keeping 160MHz");
		return;
	}

	/* vendor_ie[1] is length; need at least OUI (3) + OUI Type (1) */
	if (vendor_ie[1] < 4)
		return;

	pos = vendor_ie + 2; /* Skip Element ID and Length */
	elen = vendor_ie[1];

	/* Skip OUI (3 bytes) + OUI Type (1 byte) */
	if (pos[3] != QCN_OUI_TYPE)
		return;

	pos  += 4;
	elen -= 4;

	/* Parse TLV attributes */
	while (off + 2 <= elen) {
		u8 attr_id  = pos[off];
		u8 attr_len = pos[off + 1];

		if (off + 2 + attr_len > elen) {
			wpa_msg(wpa_s, MSG_INFO,
				"5G 320MHz: QCN IE malformed (TLV truncated, id=0x%02x len=%u)",
				attr_id, attr_len);
			break;
		}

		if ((attr_id == QCN_ATTRIB_5GHZ_320MHZ_CSA &&
		     attr_len == QCN_5GHZ_320MHZ_CSA_ELEM_LEN) ||
		    (attr_id == QCN_ATTRIB_HE_240_MHZ_SUPP && !oper_240 &&
		     attr_len <= QCN_HE_240_MHZ_MAX_ELEM_LEN &&
		     attr_len >= sizeof(struct ieee80211_240mhz_vendor_oper_extn_v2))) {
			oper_240 = (const struct ieee80211_240mhz_vendor_oper_extn_v2 *)
				(pos + off + 2);
			if (attr_id == QCN_ATTRIB_5GHZ_320MHZ_CSA)
				break;
		}

		off += 2 + attr_len;
	}

	if (!oper_240)
		return;

	wpa_hexdump(MSG_DEBUG,
		    "5G 320MHz check: 240MHz vendor oper structure",
		    (const u8 *)oper_240,
		    sizeof(struct ieee80211_240mhz_vendor_oper_extn_v2));

	wpa_dbg(wpa_s, MSG_DEBUG,
		"5G 320MHz check: is5ghz240mhz=%u ccfs0=%u ccfs1=%u punct_bitmap=0x%04x",
		oper_240->is5ghz240mhz, oper_240->ccfs0,
		oper_240->ccfs1, le_to_host16(oper_240->punct_bitmap));

	/* Validate 5G 320MHz (240MHz) support */
	if (!oper_240->is5ghz240mhz)
		return;

	wpa_printf(MSG_INFO,
		   "5G 320MHz: Upgrading bandwidth from 160MHz to 320MHz");

	/* Update to 320MHz bandwidth */
	bss->max_cw = CHAN_WIDTH_320;

	/* Update center frequency indices from vendor IE */
	bss->center_freq1_idx = oper_240->ccfs0;
	bss->center_freq2_idx = oper_240->ccfs1;

	/* Update puncturing bitmap from vendor IE */
	bss->punc_bitmap = le_to_host16(oper_240->punct_bitmap);

	wpa_printf(MSG_INFO,
		   "5G 320MHz: Updated BSS " MACSTR " - "
		   "freq=%d center_freq1_idx=%u center_freq2_idx=%u "
		   "punct_bitmap=0x%04x",
		   MAC2STR(bss->bssid), bss->freq,
		   bss->center_freq1_idx, bss->center_freq2_idx,
		   bss->punc_bitmap);
}

bool wpas_sta_cac_5g_320mhz_update_freq_params_extn(enum chan_width width,
					     u8 cf2_idx,
					     struct hostapd_freq_params *params)
{
	if (width != CHAN_WIDTH_320 || !cf2_idx || !params)
		return false;

	params->center_freq1 = 5000 + 5 * cf2_idx;
	params->center_freq2 = 0;

	return true;
}

void wpas_ch_switch_5g_320mhz_vendor_ie_extn(struct wpa_supplicant *wpa_s,
					    union wpa_event_data *data)
{
	if (!wpa_s || !data || !wpa_s->current_bss)
		return;

	if ((data->ch_switch.ch_width != CHAN_WIDTH_160 &&
	     data->ch_switch.ch_width != CHAN_WIDTH_80) ||
	    !is_5ghz_freq(data->ch_switch.freq))
		return;

	wpa_bss_check_5g_320mhz_vendor_ie_extn(wpa_s, wpa_s->current_bss);
}

/**
 * compute_sec_channel_offset_extn - Derive secondary channel offset
 * @primary_freq: Primary channel center frequency in MHz
 * @center_freq1: Center frequency of the wider channel in MHz
 * @width: Channel width enumeration (20/40/80/160/320 MHz)
 *
 * Compute the secondary channel offset value used in HT/VHT/HE/EHT
 * configuration based on the relative position of the primary channel to
 * the wider channel center.
 *
 * Return: +1 if secondary channel is above primary, -1 if below, or 0 if
 *         there is no secondary channel or the configuration is invalid.
 */
int compute_sec_channel_offset_extn(int primary_freq, int center_freq1, enum chan_width width)
{
	int offset_from_center;

	/* For 20 MHz bandwidth, there is no secondary channel */
	if (width == CHAN_WIDTH_20)
		return 0;

	/* Validate inputs */
	if (primary_freq <= 0 || center_freq1 <= 0)
		return 0;

	/* Calculate the offset between primary and center frequency */
	offset_from_center = primary_freq - center_freq1;

	switch (width) {
	case CHAN_WIDTH_40:
		if (offset_from_center == -10)
			return 1;  /* Secondary above primary */
		else if (offset_from_center == 10)
			return -1; /* Secondary below primary */
		break;

	case CHAN_WIDTH_80:
		if (offset_from_center == -30 || offset_from_center == 10)
			return 1;  /* Secondary above primary */
		else if (offset_from_center == -10 || offset_from_center == 30)
			return -1; /* Secondary below primary */
		break;

	case CHAN_WIDTH_160:
		if (offset_from_center == -70 || offset_from_center == 50 ||
		    offset_from_center == -30 || offset_from_center == 10)
			return 1;  /* Secondary above primary */
		else if (offset_from_center == 70 || offset_from_center == -50 ||
			 offset_from_center == 30 || offset_from_center == -10)
			return -1; /* Secondary below primary */
		break;

	case CHAN_WIDTH_320:
		if (offset_from_center == -150 || offset_from_center == 130 ||
		    offset_from_center == -110 || offset_from_center == 90 ||
		    offset_from_center == -70 || offset_from_center == 50 ||
		    offset_from_center == -30 || offset_from_center == 10)
			return 1;  /* Secondary above primary */
		else if (offset_from_center == 150 || offset_from_center == -130 ||
			 offset_from_center == 110 || offset_from_center == -90 ||
			 offset_from_center == 70 || offset_from_center == -50 ||
			 offset_from_center == 30 || offset_from_center == -10)
			return -1; /* Secondary below primary */
		break;

	default:
		/* Unsupported bandwidth */
		return 0;
	}

	/* Invalid configuration */
	return 0;
}
/**
 * compute_dfs_for_chanwidth_extn - Compute DFS status for channel
 * @freq: Primary channel center frequency in MHz
 * @chanwidth: Channel width enumeration (20/40/80/160/320 MHz)
 *
 * Determine whether the given primary channel (and, for 160 MHz bandwidth,
 * the secondary 80 MHz segment) falls on DFS frequencies. This is used to
 * populate DFS state in interface status for Independent Repeater flows.
 *
 * Return: true if the primary and/or secondary 80 MHz channel requires DFS,
 *         false otherwise.
 */
bool compute_dfs_for_chanwidth_extn(int freq, int chanwidth)
{
	bool is_dfs;

	is_dfs = ieee80211_is_dfs(freq, NULL, 0);
	if (!is_dfs && channel_width_to_int(chanwidth) == 160)
		is_dfs = ieee80211_is_dfs(freq + 80, NULL, 0);

	return is_dfs;
}

static bool wpas_parse_ht_operation_extn(struct wpa_supplicant *wpa_s,
					 struct wpa_bss *bss)
{
	const u8 *ie;
	const struct ieee80211_ht_operation *ht_oper;
	u8 secondary_offset;
	u8 center_chan;
	int center_freq;

	ie = wpa_bss_get_ie(bss, WLAN_EID_HT_OPERATION);
	if (!ie || ie[1] < 2)
		return false;

	ht_oper = (const struct ieee80211_ht_operation *) (ie + 2);
	bss->max_cw = CHAN_WIDTH_20;
	secondary_offset = ht_oper->ht_param & HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
	center_freq = 0;

	if (ht_oper->ht_param & HT_INFO_HT_PARAM_STA_CHNL_WIDTH) {
		if (secondary_offset == HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE)
			center_freq = bss->freq + 10;
		else if (secondary_offset == HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW)
			center_freq = bss->freq - 10;

		if (center_freq) {
			if (ieee80211_freq_to_chan(center_freq, &center_chan) != NUM_HOSTAPD_MODES) {
				bss->max_cw = CHAN_WIDTH_40;
				bss->center_freq1_idx = center_chan;
			}
		}
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "WLAN_EID_HT_OPERATION: width=%d "
		"center_freq1_idx=%d center_freq2_idx=%d ht_param=0x%02x",
		bss->max_cw, bss->center_freq1_idx,
		bss->center_freq2_idx, ht_oper->ht_param);
	return true;
}

/**
 * wpa_get_bss_channel_oper_info_extn - Extract BSS channel operating info
 * @wpa_s: Pointer to wpa_supplicant interface
 * @bss: BSS entry whose IEs are parsed
 *
 * Parse EHT, HE, VHT and HT operation IEs to derive maximum channel width,
 * center frequency indices and puncturing bitmap, updating the BSS fields
 * used by repeater and channel selection logic.
 */
void wpa_get_bss_channel_oper_info_extn(struct wpa_supplicant *wpa_s,
					struct wpa_bss *bss)
{
	const u8 *ie;

	/* Operation IEs are reparsed when a cached BSS is updated. */
	bss->max_cw = CHAN_WIDTH_20;
	bss->center_freq1_idx = 0;
	bss->center_freq2_idx = 0;
	bss->punc_bitmap = 0;

	/* Parse EHT Operation first:
	 * Ensures punctured channel bitmap (disabled_chan_bitmap) and EHT-specific
	 * params are captured before VHT/HE to avoid missing or being overwritten. */
	ie = get_ie_ext(bss->ies, bss->ie_len, WLAN_EID_EXT_EHT_OPERATION);
	if (ie && ie[1] >= 1) {
		const struct ieee80211_eht_operation *eht_oper =
			(const struct ieee80211_eht_operation *) (ie + 3);

		if (eht_oper->oper_params & EHT_OPER_INFO_PRESENT) {
			switch(eht_oper->oper_info.control) {
			case EHT_OPER_CHANNEL_WIDTH_20MHZ:
				bss->max_cw = CHAN_WIDTH_20;
				break;
			case EHT_OPER_CHANNEL_WIDTH_40MHZ:
				bss->max_cw = CHAN_WIDTH_40;
				break;
			case EHT_OPER_CHANNEL_WIDTH_80MHZ:
				bss->max_cw = CHAN_WIDTH_80;
				break;
			case EHT_OPER_CHANNEL_WIDTH_160MHZ:
				bss->max_cw = CHAN_WIDTH_160;
				break;
			case EHT_OPER_CHANNEL_WIDTH_320MHZ:
				bss->max_cw = CHAN_WIDTH_320;
				break;
			default:
				bss->max_cw = CHAN_WIDTH_20;
				break;
			}
			bss->center_freq1_idx = eht_oper->oper_info.ccfs0;
			bss->center_freq2_idx = eht_oper->oper_info.ccfs1;
			if (eht_oper->oper_params & EHT_OPER_DISABLED_SUBCHAN_BITMAP_PRESENT)
				bss->punc_bitmap = eht_oper->oper_info.disabled_chan_bitmap;
			else
				bss->punc_bitmap = 0;

			wpa_dbg(wpa_s, MSG_DEBUG, "WLAN_EID_EXT_EHT_OPERATION:"
				"width = %d center_freq1_idx = %d center_freq2_idx = %d"
				"eht_oper->oper_info.disabled_chan_bitmap = %d",
				bss->max_cw, bss->center_freq1_idx, bss->center_freq2_idx,
				eht_oper->oper_info.disabled_chan_bitmap);
			return;
		}
	}

	ie = wpa_bss_get_ie(bss, WLAN_EID_VHT_OPERATION);
	if (ie && ie[1] >= 1) {
		const struct ieee80211_vht_operation *vht_oper =
			(struct ieee80211_vht_operation *) (ie + 2);

		switch (vht_oper->vht_op_info_chwidth) {
		case CHANWIDTH_80MHZ:
			bss->center_freq1_idx = vht_oper->vht_op_info_chan_center_freq_seg0_idx;
			bss->center_freq2_idx = vht_oper->vht_op_info_chan_center_freq_seg1_idx;
			if (bss->center_freq2_idx &&
			    abs(bss->center_freq2_idx - bss->center_freq1_idx) == 8)
				bss->max_cw = CHAN_WIDTH_160;
			else if (bss->center_freq2_idx)
				bss->max_cw = CHAN_WIDTH_80P80;
			else
				bss->max_cw = CHAN_WIDTH_80;
			break;
		case CHANWIDTH_160MHZ:
			bss->max_cw = CHAN_WIDTH_160;
			break;
		case CHANWIDTH_80P80MHZ:
			bss->max_cw = CHAN_WIDTH_80P80;
			break;
		case CHANWIDTH_USE_HT:
			if (!wpas_parse_ht_operation_extn(wpa_s, bss))
				bss->max_cw = CHAN_WIDTH_20;
			break;
		default:
			bss->max_cw = CHAN_WIDTH_20;
			break;
		}

		if (vht_oper->vht_op_info_chwidth != CHANWIDTH_USE_HT) {
			bss->center_freq1_idx =
				vht_oper->vht_op_info_chan_center_freq_seg0_idx;
			bss->center_freq2_idx =
				vht_oper->vht_op_info_chan_center_freq_seg1_idx;
		}

		wpa_bss_check_5g_320mhz_vendor_ie_extn(wpa_s, bss);

		wpa_dbg(wpa_s, MSG_DEBUG, "WLAN_EID_VHT_OPERATION: width = %d"
			"center_freq1_idx = %d *center_freq2_idx = %d",
			bss->max_cw, bss->center_freq1_idx, bss->center_freq2_idx);
		return;
	}

	ie = get_ie_ext(bss->ies, bss->ie_len, WLAN_EID_EXT_HE_OPERATION);
	if (ie && ie[1] >= 1) {
		const struct ieee80211_he_operation *he_oper =
			(const struct ieee80211_he_operation *) (ie + 3);
		int offset = 0;
		u8 he_oper_chwidth;

		if (he_oper->he_oper_params & HE_OPERATION_VHT_OPER_INFO)
			offset = 3;
		if (he_oper->he_oper_params & HE_OPERATION_COHOSTED_BSS)
			offset += 1;
		if (he_oper->he_oper_params & HE_OPERATION_6GHZ_OPER_INFO) {
			const struct ieee80211_he_6ghz_oper_info *oper_info =
				(const struct ieee80211_he_6ghz_oper_info *)
				(ie + 3 + sizeof(struct ieee80211_he_operation) + offset);

			he_oper_chwidth = (oper_info->control &
					HE_6GHZ_OPER_INFO_CTRL_CHAN_WIDTH_MASK);

			switch (he_oper_chwidth) {
			case IEEE80211_6GHZ_OP_CHWIDTH_20:
				bss->max_cw = CHAN_WIDTH_20;
				break;
			case IEEE80211_6GHZ_OP_CHWIDTH_40:
				bss->max_cw = CHAN_WIDTH_40;
				break;
			case IEEE80211_6GHZ_OP_CHWIDTH_80:
				bss->max_cw = CHAN_WIDTH_80;
				break;
			case IEEE80211_6GHZ_OP_CHWIDTH_160_80_80:
				bss->max_cw = CHAN_WIDTH_160;
				break;
			default:
				bss->max_cw = CHAN_WIDTH_20;
				break;
			}
			bss->center_freq1_idx =
				oper_info->chan_center_freq_seg0;
			bss->center_freq2_idx =
				oper_info->chan_center_freq_seg1;

			wpa_dbg(wpa_s, MSG_DEBUG, "WLAN_EID_EXT_HE_OPERATION:"
				"width = %d *center_freq1_idx = %d *center_freq2_idx = %d",
				bss->max_cw, bss->center_freq1_idx, bss->center_freq2_idx);
			return;
		}
	}

	/* Legacy HT operation without VHT/HE/EHT operation information. */
	wpas_parse_ht_operation_extn(wpa_s, bss);

	return;
}

/**
 * wpa_supp_pre_connect_state_handle_extn - Emit PRE_CONNECT notifications
 * @wpa_s: Pointer to wpa_supplicant interface
 * @bss: Target BSS entry selected for connection
 *
 * Send per-link (for MLO) or legacy channel parameters to the control
 * interface during PRE_CONNECT handling in Independent Repeater mode.
 * This allows the AP side to align channels and bandwidth before SME
 * authentication work is scheduled.
 */
void wpa_supp_pre_connect_state_handle_extn(struct wpa_supplicant *wpa_s, struct wpa_bss *bss)
{
	u8 i, msg[2048];
	u8 op_class, channel;
	int center_freq1, center_freq2;
	s8 hw_idx;
	bool is_dfs = false;
	char links_buf[1024];
	char *pos, *end;
	bool first;

	if (wpa_s && bss) {
		if (!is_zero_ether_addr(bss->mld_addr)) {
			/*
			 * MLO Pre-connect Handling
			 *
			 * Send a single PRE-CONNECTING message per MLD, aggregating
			 * all per-link information in a comma-separated list. Each
			 * link contributes one tuple of:
			 *   hw_idx,freq,c_freq1,c_freq2,width,punc_bitmap,is_dfs
			 */
			pos = links_buf;
			end = links_buf + sizeof(links_buf) - 1;
			first = true;

			links_buf[0] = '\0';
			for_each_link(bss->valid_links, i) {
				ieee80211_freq_to_channel_ext(bss->mld_links[i].freq,
							      0, 1, &op_class, &channel);
				center_freq1 = ieee80211_chan_to_freq(NULL,
				op_class, bss->mld_links[i].center_freq1_idx);
				center_freq2 = ieee80211_chan_to_freq(NULL,
				op_class, bss->mld_links[i].center_freq2_idx);
				hw_idx = wpa_get_hw_idx_by_freq(wpa_s,
								bss->mld_links[i].freq);
				is_dfs = ieee80211_is_dfs(bss->mld_links[i].freq,
							  NULL, 0);
				wpa_printf(MSG_INFO, "MLO hw_idx = %d ifname = %s"
					   "freq = %d center_freq1 = %d center_freq2 = %d width = %d"
					   "punc_bitmap = %d is_dfs = %d", hw_idx, wpa_s->ifname,
					   bss->mld_links[i].freq, center_freq1, center_freq2,
					   bss->mld_links[i].width, bss->mld_links[i].punc_bitmap, is_dfs);

				if (!first) {
					if (pos + 1 >= end)
						break;
					*pos++ = ';';
				}
				first = false;

				/* Count one pre-connect operation per MLD */
				wpa_s->pre_connect_cnt++;

				int n = os_snprintf(pos, end - pos,
						"%d,%d,%d,%d,%d,%d,%d",
						hw_idx,
						bss->mld_links[i].freq,
						center_freq1,
						center_freq2,
						bss->mld_links[i].width,
						bss->mld_links[i].punc_bitmap,
						is_dfs);
				if (os_snprintf_error(end - pos, n))
					break;
				pos += n;
			}

			if (!first) {
				os_snprintf((char *)msg, sizeof(msg),
					"%s ifname = %s links = %s",
					WPA_EVENT_PRE_CONNECTING, wpa_s->ifname,
					links_buf);
				wpa_msg_ctrl(wpa_s, MSG_INFO, "%s", (char *)msg);
			}
		} else {
			/* Legacy Pre-connect Handling (single-link "links" entry) */
			ieee80211_freq_to_channel_ext(bss->freq, 0, 1,
						      &op_class, &channel);
			center_freq1 = ieee80211_chan_to_freq(NULL, op_class,
							      bss->center_freq1_idx);
			center_freq2 = ieee80211_chan_to_freq(NULL, op_class,
							      bss->center_freq2_idx);
			hw_idx = wpa_get_hw_idx_by_freq(wpa_s, bss->freq);
			is_dfs = ieee80211_is_dfs(bss->freq, NULL, 0);

			wpa_printf(MSG_INFO, "Legacy hw_idx = %d ifname = %s"
				   "freq = %d center_freq1 = %d center_freq2 = %d width = %d"
				   "punc_bitmap = %d is_dfs = %d", hw_idx, wpa_s->ifname,
				   bss->freq, center_freq1, center_freq2, bss->max_cw,
				   bss->punc_bitmap, is_dfs);

			os_snprintf(links_buf, sizeof(links_buf),
				    "%d,%d,%d,%d,%d,%d,%d",
				    hw_idx,
				    bss->freq,
				    center_freq1,
				    center_freq2,
				    bss->max_cw,
				    bss->punc_bitmap,
				    is_dfs);

			os_snprintf((char *)msg, sizeof(msg),
				"%s ifname = %s links = %s",
				WPA_EVENT_PRE_CONNECTING, wpa_s->ifname,
				links_buf);
			wpa_msg_ctrl(wpa_s, MSG_INFO, "%s", (char *)msg);
			wpa_s->pre_connect_cnt++;
		}
	}
}

/**
 * sme_pre_connect_timer_extn - Handle pre-connect timeout for repeater flows
 * @eloop_ctx: Eloop context, cast to struct wpa_supplicant pointer
 * @timeout_ctx: Unused timeout context (reserved for future use)
 *
 * Invoked by the eloop timeout mechanism while in WPA_PRE_CONNECT state to
 * detect when pre-connection attempts have timed out. On timeout, resets the
 * pre_connect counter and requests a fresh scan to continue repeater connect
 * logic.
 */
void sme_pre_connect_timer_extn(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->wpa_state == WPA_PRE_CONNECT) {
		wpa_msg(wpa_s, MSG_DEBUG, "SME: Pre-Connection timeout");
		wpa_s->pre_connect_cnt = 0;
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
}

/**
 * wpa_bss_update_link_rnr_ap_info_extn - Update MLD link info from RNR
 * @wpa_s: Pointer to wpa_supplicant interface
 * @bss: Base BSS entry representing the MLD AP
 * @bssid_ptr: Pointer to the neighbor AP BSSID from RNR
 * @ap_info: Neighbor AP info (op_class/channel) taken from RNR element
 * @mld_params: Pointer to MLD parameter field from the RNR TBTT info
 * @link_id: Link ID index to update in the MLD link array
 *
 * Use Reduced Neighbor Report (RNR) information to update per-link
 * information for Multi-Link Device (MLD) APs in the BSS structure.
 * When a neighbor BSS entry is available, also populate channel width,
 * center frequency indices and puncturing bitmap for the link.
 */
void wpa_bss_update_link_rnr_ap_info_extn(struct wpa_supplicant *wpa_s,
					  struct wpa_bss *bss,
					  const u8 *bssid_ptr,
					  const struct ieee80211_neighbor_ap_info *ap_info,
					  const u8 *mld_params,
					  u8 link_id)
{
	struct mld_link *l;
	struct wpa_bss *neigh_bss = NULL;

	if (bss)
		neigh_bss = wpa_bss_get(wpa_s, bssid_ptr, bss->ssid, bss->ssid_len);
	if (!neigh_bss)
		neigh_bss = wpa_bss_get_bssid(wpa_s, bssid_ptr);

	bss->valid_links |= BIT(link_id);
	l = &bss->mld_links[link_id];
	os_memcpy(l->bssid, bssid_ptr, ETH_ALEN);
	l->disabled = mld_params[2] & RNR_TBTT_INFO_MLD_PARAM2_LINK_DISABLED;
	l->freq = ieee80211_chan_to_freq(NULL, ap_info->op_class, ap_info->channel);

	/*
	 * neigh_bss is matched by BSSID only, with no check against the freq
	 * RNR just reported for this link. A stale entry from another band
	 * can leak an invalid width here (e.g. CHAN_WIDTH_80 on 2.4 GHz),
	 * which later makes hostapd's channel lookup fail silently and drop
	 * this link's PRE_CONNECT ack, stalling pre_connect_cnt forever.
	 * Discard it instead of trusting its fields.
	 */
	if (neigh_bss && l->freq && neigh_bss->freq != l->freq) {
		wpa_printf(MSG_DEBUG,
			   "%s: link_id=%u discarding stale neigh_bss freq=%d "
			   "(RNR reports freq=%d for this link)",
			   __func__, link_id, neigh_bss->freq, l->freq);
		neigh_bss = NULL;
	}

	if (neigh_bss) {
		l->center_freq1_idx = neigh_bss->center_freq1_idx;
		l->center_freq2_idx = neigh_bss->center_freq2_idx;
		l->width = neigh_bss->max_cw;
		l->punc_bitmap = neigh_bss->punc_bitmap;
		if (neigh_bss->mld_links[link_id].mcst) {
			l->mcst = neigh_bss->mld_links[link_id].mcst;
			l->mcst_update_time =
				neigh_bss->mld_links[link_id].mcst_update_time;
		}
		os_get_reltime(&neigh_bss->last_update);
		wpa_printf(MSG_DEBUG, "%s: link_id=%u freq=%d cf1=%d cf2=%d "
			   "width=%d punc=%d mcst=%u",
			   __func__, link_id, l->freq, l->center_freq1_idx,
			   l->center_freq2_idx, l->width, l->punc_bitmap,
			   l->mcst);
	} else {
		wpa_printf(MSG_DEBUG, "%s: link_id=%u freq=%d (no neigh_bss, "
			   "channel info unavailable)", __func__, link_id, l->freq);
	}
}

/**
 * wpa_supplicant_start_sta_scan - Handle repeater AP ACS/CSA timeout for repeater
 * @eloop_ctx: Eloop context, cast to struct wpa_supplicant pointer
 * @timeout_ctx: Unused timeout context (reserved for future use)
 *
 * Invoked by the eloop timeout mechanism while in WPA_DISCONNECTED state to
 * detect when either repeater AP ACS attempts have timed out or repeater AP
 * CSA has timed out. On timeout, set the acs_complete flag or reset the
 * hold_scan_csa flag accordingly and requests a fresh scan to
 * continue repeater connect logic.
 */
void wpa_supplicant_start_sta_scan(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->wpa_state == WPA_DISCONNECTED) {
		if (IS_CSH_IGNORE_CSA_DFS_ENABLED(wpa_s->conf->cswopts) &&
		    wpa_s->hold_scan_csa) {
			wpa_msg(wpa_s, MSG_DEBUG, "Repeater CSA timed out for CSwOpts %s",
				convert_cswopts_to_str(CSH_OPT_IGNORE_CSA_DFS));
			wpa_s->hold_scan_csa = false;
		} else {
			wpa_msg(wpa_s, MSG_DEBUG, "Repeater ACS timeout");
			wpa_s->acs_complete = 1;
		}
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
}

/**
 * wpa_config_process_cswopts_extn - Parse and apply CSwOpts from config file
 * @config: Pointer to wpa_config structure
 * @line: Line number in the configuration file (for error messages)
 * @pos: String value to parse (decimal or hex with 0x prefix)
 *
 * Validate and store the Channel Switch Options bitmap from the configuration
 * file into wpa_config::cswopts.
 *
 * Return: 0 on success, -1 on invalid value.
 */
int wpa_config_process_cswopts_extn(struct wpa_config *config, int line,
				    const char *pos)
{
	long int val = strtol(pos, NULL, 0);

	if (!cswopts_validate(val)) {
		wpa_printf(MSG_ERROR,
			   "Line %d: Invalid CSwOpts value 0x%lx: valid mask is 0x%x",
			   line, val, CSH_OPT_VALID_MASK);
		return -1;
	}

	config->cswopts = (unsigned int)val;
	wpa_printf(MSG_INFO, "CSwOpts set to 0x%x", config->cswopts);
	if (IS_CSH_APRIORI_NEXT_CHANNEL_ENABLED(config->cswopts))
		wpa_printf(MSG_INFO,
			   "CSwOpts: Apriori next channel (0x40) - Apriori channel selection feature is not yet supported");

	return 0;
}

/** wpas_set_dfs_state_freq - Set DFS state for a single 20 MHz channel
 * @wpa_s: wpa_supplicant context
 * @freq: Channel frequency in MHz
 * @state: New DFS state (HOSTAPD_CHAN_DFS_UNAVAILABLE or HOSTAPD_CHAN_DFS_USABLE)
 *
 * Iterates over all hardware modes and sets the DFS state bits in chan->flag
 * for the channel matching @freq.  Only acts on channels that have the
 * HOSTAPD_CHAN_RADAR flag set.
 *
 * Returns 1 if the channel was found and updated, 0 otherwise.
 */
int wpas_set_dfs_state_freq(struct wpa_supplicant *wpa_s, int freq, u32 state)
{
	int i, j;

	if (!wpa_s->hw.modes || !wpa_s->hw.num_modes)
		return 0;

	for (j = 0; j < wpa_s->hw.num_modes; j++) {
		struct hostapd_hw_modes *mode = &wpa_s->hw.modes[j];

		for (i = 0; i < mode->num_channels; i++) {
			struct hostapd_channel_data *chan = &mode->channels[i];

			if (chan->freq == freq && (chan->flag & HOSTAPD_CHAN_RADAR)) {
				wpa_dbg(wpa_s, MSG_DEBUG,
					"DFS: wpas_set_dfs_state_freq: "
					"freq=%d MHz old_dfs_state=0x%X new_dfs_state=0x%X",
					freq,
					chan->flag & HOSTAPD_CHAN_DFS_MASK,
					state);
				chan->flag &= ~HOSTAPD_CHAN_DFS_MASK;
				chan->flag |= state;
				return 1;
			}
		}
	}
	wpa_printf(MSG_WARNING,
		   "DFS: wpas_set_dfs_state_freq: cannot set state for "
		   "freq %d MHz (not a radar channel or not found)", freq);
	return 0;
}

/**
 * wpa_supplicant_ctrl_iface_set_cswopts_extn - Handle SET CSwOpts command
 * @wpa_s: Pointer to wpa_supplicant interface
 * @value: String value to parse (decimal or hex with 0x prefix)
 *
 * Validate and apply the Channel Switch Options bitmap from the SET control
 * interface command. A value of 0 clears all bits; non-zero values are OR'd
 * into the existing bitmap.
 *
 * Return: 0 on success, -1 on invalid value.
 */
int wpa_supplicant_ctrl_iface_set_cswopts_extn(struct wpa_supplicant *wpa_s,
					       const char *value)
{
	long int val = strtol(value, NULL, 0);

	if (!cswopts_validate(val)) {
		wpa_printf(MSG_ERROR,
			   "Invalid CSwOpts value 0x%lx: valid mask is 0x%x",
			   val, CSH_OPT_VALID_MASK);
		return -1;
	}

	if (val == 0)
		wpa_s->conf->cswopts = 0;
	else {
		if ((IS_CSH_RCSA_TO_UPLINK_ENABLED(*value) ||
		     IS_CSH_PROCESS_RCSA_ENABLED(*value)) &&
		    wpa_s->conf->uplink_csa) {
			wpa_printf(MSG_INFO, "RCSA and Uplink CSA cann't co-exists! disabling");
			return -1;
		}

		wpa_s->conf->cswopts |= (unsigned int)val;
	}

	wpa_printf(MSG_INFO, "Updated CSwOpts to 0x%x", wpa_s->conf->cswopts);
	if (IS_CSH_APRIORI_NEXT_CHANNEL_ENABLED(wpa_s->conf->cswopts))
		wpa_printf(MSG_INFO,
			   "CSwOpts: Apriori next channel (0x40) - Apriori channel selection feature is not yet supported");

	return 0;
}

/** wpas_set_dfs_state - Set DFS state for all sub-channels of a channel block
 * @wpa_s: wpa_supplicant context
 * @freq: Primary channel frequency in MHz
 * @ht_enabled: Whether HT is enabled (unused, kept for API symmetry)
 * @chan_offset: HT40 secondary channel offset (unused, kept for API symmetry)
 * @chan_width: Channel width (enum chan_width)
 * @cf1: Center frequency 1 in MHz
 * @cf2: Center frequency 2 in MHz (non-zero for 80P80 and 5G 240 MHz)
 * @state: New DFS state (HOSTAPD_CHAN_DFS_UNAVAILABLE or HOSTAPD_CHAN_DFS_USABLE)
 * @radar_bitmap: Bitmap of affected sub-channels (0 = all sub-channels)
 *
 * Mirrors the logic of set_dfs_state() in src/ap/dfs.c but operates on
 * wpa_s->hw.modes instead of iface->current_mode.  Used in STA-only mode
 * (no AP iface) to keep the channel table in sync with the kernel NOL state.
 *
 * For 5G 320MHz-2 (240 MHz, non-contiguous):
 *   cf1 = center of the 160 MHz segment (8 x 20 MHz sub-channels)
 *   cf2 = center of the  80 MHz segment (4 x 20 MHz sub-channels)
 *   radar_bitmap bits  0-7  -> 160 MHz segment sub-channels
 *   radar_bitmap bits  8-11 ->  80 MHz segment sub-channels
 *
 * Returns the number of channels successfully updated.
 */
int wpas_set_dfs_state(struct wpa_supplicant *wpa_s, int freq,
		       int ht_enabled, int chan_offset, int chan_width,
		       int cf1, int cf2, u32 state, u16 radar_bitmap)
{
	int n_chans = 1, i;
	int frequency = freq;
	int frequency2 = 0;
	int ret = 0;

	switch (chan_width) {
	case CHAN_WIDTH_20_NOHT:
	case CHAN_WIDTH_20:
		n_chans = 1;
		if (frequency == 0)
			frequency = cf1;
		break;
	case CHAN_WIDTH_40:
		n_chans = 2;
		frequency = cf1 - 10;
		break;
	case CHAN_WIDTH_80:
		n_chans = 4;
		frequency = cf1 - 30;
		break;
	case CHAN_WIDTH_80P80:
		n_chans = 4;
		frequency = cf1 - 30;
		frequency2 = cf2 - 30;
		break;
	case CHAN_WIDTH_160:
		n_chans = 8;
		frequency = cf1 - 70;
		break;
	default:
		if (!hostapd_get_n_chans_and_frequency_extn(
			hostapd_get_oper_chwidth_from_width_extn(
				channel_width_to_int(chan_width)),
			cf1, &n_chans, &frequency))
			break;

		wpa_printf(MSG_INFO,
				   "DFS: wpas_set_dfs_state: chan_width %d not supported, "
				   "treating as 20 MHz",
				   chan_width);
		n_chans = 1;
		if (frequency == 0)
			frequency = cf1;
		break;
	}

	wpa_dbg(wpa_s, MSG_DEBUG,
			"DFS: wpas_set_dfs_state: start_freq=%d MHz n_chans=%d "
			"state=0x%X radar_bitmap=0x%04X",
			frequency, n_chans, state, radar_bitmap);

	for (i = 0; i < n_chans; i++) {
		if (radar_bitmap && state == HOSTAPD_CHAN_DFS_UNAVAILABLE) {
			if (radar_bitmap & (1 << i)) {
				wpa_dbg(wpa_s, MSG_DEBUG,
						"DFS: wpas_set_dfs_state: marking "
						"freq=%d MHz UNAVAILABLE (bit %d set)",
						frequency, i);
				ret += wpas_set_dfs_state_freq(wpa_s, frequency, state);
			} else {
				wpa_dbg(wpa_s, MSG_DEBUG,
						"DFS: wpas_set_dfs_state: skipping "
						"freq=%d MHz (bit %d not set)",
						frequency, i);
			}
			frequency += 20;
			if (chan_width == CHAN_WIDTH_80P80) {
				if (radar_bitmap & (1 << (i + 4)))
					ret += wpas_set_dfs_state_freq(wpa_s, frequency2, state);
				frequency2 += 20;
			}
		} else {
			ret += wpas_set_dfs_state_freq(wpa_s, frequency, state);
			frequency += 20;
			if (chan_width == CHAN_WIDTH_80P80) {
				ret += wpas_set_dfs_state_freq(wpa_s, frequency2, state);
				frequency2 += 20;
			}
		}
	}

	return ret;
}

/**
 * wpas_dfs_radar_detected_sta_mode - Handle radar detection in STA-only mode
 * @wpa_s: wpa_supplicant context
 * @radar: DFS event data from the driver
 *
 * Called when EVENT_DFS_RADAR_DETECTED is received but no AP/mesh iface is
 * present.  Replicates the hostapd_dfs_radar_detected -> set_dfs_state ->
 * set_dfs_state_freq path directly on wpa_s->hw.modes so that the channel
 * table reflects the NOL state and subsequent BSS selection correctly avoids
 * radar-impacted channels.
 *
 * When device-level parameters differ from the operating bandwidth (e.g.,
 * EHT 320 MHz device operating at 160 MHz), the device parameters are used
 * to determine which sub-channels are affected, matching hostapd behaviour.
 */
void wpas_dfs_radar_detected_sta_mode(struct wpa_supplicant *wpa_s,
				      struct dfs_event *radar)
{
	bool use_device_params =
		radar->cf_device && radar->chan_width_device &&
		radar->chan_width_device != radar->chan_width &&
		radar->cf_device != radar->cf1;

	wpa_dbg(wpa_s, MSG_DEBUG,
			"DFS: radar detected on %d MHz (no AP iface) - "
			"updating hw channel states directly: "
			"ht_enabled=%d chan_offset=%d chan_width=%d "
			"cf1=%d cf2=%d radar_bitmap=0x%04X "
			"chan_width_device=%d cf_device=%d",
			radar->freq, radar->ht_enabled, radar->chan_offset,
			radar->chan_width, radar->cf1, radar->cf2,
			radar->radar_bitmap,
			radar->chan_width_device, radar->cf_device);

	if (use_device_params) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"DFS: using device params for NOL update: "
			"chan_width_device=%d cf_device=%d",
			radar->chan_width_device, radar->cf_device);
		wpas_set_dfs_state(wpa_s, radar->freq,
				   radar->ht_enabled, radar->chan_offset,
				   radar->chan_width_device, radar->cf_device,
				   radar->cf2,
				   HOSTAPD_CHAN_DFS_UNAVAILABLE,
				   radar->radar_bitmap);
	} else {
		wpas_set_dfs_state(wpa_s, radar->freq,
				   radar->ht_enabled, radar->chan_offset,
				   radar->chan_width, radar->cf1, radar->cf2,
				   HOSTAPD_CHAN_DFS_UNAVAILABLE,
				   radar->radar_bitmap);
	}
}

/**
 * wpas_dfs_nop_finished_sta_mode - Handle NOP expiry in STA-only mode
 * @wpa_s: wpa_supplicant context
 * @radar: DFS event data from the driver
 *
 * Called when EVENT_DFS_NOP_FINISHED is received but no AP/mesh iface is
 * present.  Replicates the hostapd_dfs_nop_finished -> set_dfs_state ->
 * set_dfs_state_freq path directly on wpa_s->hw.modes so that the channel
 * table reflects the USABLE state once the Non-Occupancy Period has expired,
 * allowing the STA to consider these channels again for future connections.
 *
 * Note: radar_bitmap is not used for NOP-finished (channels become usable
 * again unconditionally), matching hostapd behaviour.
 */
void wpas_dfs_nop_finished_sta_mode(struct wpa_supplicant *wpa_s,
				    struct dfs_event *radar)
{
	bool use_device_params =
		radar->cf_device && radar->chan_width_device &&
		radar->chan_width_device != radar->chan_width &&
		radar->cf_device != radar->cf1;

	wpa_dbg(wpa_s, MSG_DEBUG,
			"DFS: NOP finished on %d MHz (no AP iface) - "
			"updating hw channel states directly: "
			"ht_enabled=%d chan_offset=%d chan_width=%d "
			"cf1=%d cf2=%d chan_width_device=%d cf_device=%d",
			radar->freq, radar->ht_enabled, radar->chan_offset,
			radar->chan_width, radar->cf1, radar->cf2,
			radar->chan_width_device, radar->cf_device);

	if (use_device_params) {
		wpa_dbg(wpa_s, MSG_DEBUG,
				"DFS: using device params for USABLE update: "
				"chan_width_device=%d cf_device=%d",
				radar->chan_width_device, radar->cf_device);
		wpas_set_dfs_state(wpa_s, radar->freq,
				   radar->ht_enabled, radar->chan_offset,
				   radar->chan_width_device, radar->cf_device,
				   radar->cf2,
				   HOSTAPD_CHAN_DFS_USABLE, 0);
	} else {
		wpas_set_dfs_state(wpa_s, radar->freq,
				   radar->ht_enabled, radar->chan_offset,
				   radar->chan_width, radar->cf1, radar->cf2,
				   HOSTAPD_CHAN_DFS_USABLE, 0);
	}
}

/**
 * wpas_is_chan_nol_extn - Check if a frequency is in the NOL
 * @wpa_s: wpa_supplicant context
 * @freq: Frequency in MHz
 * Returns: true if the channel is marked HOSTAPD_CHAN_DFS_UNAVAILABLE,
 *          false otherwise
 *
 * Uses chan->flag DFS bits directly instead of the legacy nol_flag field.
 */
static bool wpas_is_chan_nol_extn(struct wpa_supplicant *wpa_s, int freq)
{
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan;
	int i, j;

	if (!wpa_s->hw.modes || !wpa_s->hw.num_modes)
		return 0;

	for (i = 0; i < wpa_s->hw.num_modes; i++) {
		mode = &wpa_s->hw.modes[i];

		for (j = 0; j < mode->num_channels; j++) {
			chan = &mode->channels[j];

			if (chan->freq == freq &&
				(chan->flag & HOSTAPD_CHAN_RADAR) &&
				(chan->flag & HOSTAPD_CHAN_DFS_MASK) ==
					HOSTAPD_CHAN_DFS_UNAVAILABLE)
				return true;
		}
	}
	return false;
}

static bool wpas_check_channel_range_extn(struct wpa_supplicant *wpa_s,
					  int start_freq, int num_channels,
					  bool check_nol, u16 punct_bitmap)
{
	int i;

	if (num_channels <= 0 || start_freq <= 0)
		return true;

	for (i = 0; i < num_channels; i++) {
		int freq = start_freq + i * 20;
		bool hit;

		if (punct_bitmap & BIT(i))
			continue;

		if (check_nol)
			hit = wpas_is_chan_nol_extn(wpa_s, freq);
		else
			hit = disabled_freq(wpa_s, freq);

		if (hit) {
			wpa_printf(MSG_DEBUG,
				   "%s: Freq %d in range [%d-%d] (n20=%d)",
				   check_nol ? "NOL" : "REGDOM",
				   freq, start_freq,
				   start_freq + (num_channels - 1) * 20,
				   num_channels);
			return true;
		}
	}

	return false;
}

static bool wpas_primary_freq_in_range_extn(int freq, int start_freq,
					    int num_channels)
{
	if (freq <= 0 || start_freq <= 0 || num_channels <= 0 ||
	    freq < start_freq ||
	    freq > start_freq + (num_channels - 1) * 20)
		return false;

	return (freq - start_freq) % 20 == 0;
}

/**
 * wpas_check_link_ranges_extn - Check all active 20 MHz channels of a link
 * @wpa_s: wpa_supplicant context
 * @freq: primary 20 MHz channel frequency in MHz
 * @width: operating channel width
 * @cf1: first center frequency in MHz
 * @cf2: second center or full bonded center frequency in MHz
 * @check_nol: check DFS NOL state when true, regulatory state otherwise
 * @punct_bitmap: bitmap of punctured 20 MHz channels
 *
 * The center-frequency fields depend on the operation encoding. For 20 MHz,
 * cf1 and cf2 are unused. For 40/80 MHz, cf1 is the contiguous operating
 * center. For native VHT 160 MHz, cf1 is the full center and cf2 is zero.
 * QSDK's VHT compatibility encoding uses cf1 for the primary-side 80 MHz
 * center and cf2 for the full 160 MHz center. EHT 160/320 MHz and QCN
 * 5 GHz 240 MHz layouts also use cf2 as the full bonded center, with cf1
 * identifying the primary-side segment. For 80+80, cf1 and cf2 are the
 * centers of the two 80 MHz segments. 6 GHz uses the separate
 * power-mode-aware validator below.
 *
 * Return: true if any active subchannel is unavailable or the link state is
 *         incomplete, false otherwise
 */
static bool wpas_check_link_ranges_extn(struct wpa_supplicant *wpa_s,
					int freq, enum chan_width width,
					int cf1, int cf2, bool check_nol,
					u16 punct_bitmap)
{
	int bw;
	int start_freq, num_channels;

	bw = channel_width_to_int(width);
	if (bw <= 0)
		return true;

	if (freq <= 0)
		return true;

	if (check_nol) {
		if (wpas_is_chan_nol_extn(wpa_s, freq))
			return true;
	} else {
		if (disabled_freq(wpa_s, freq))
			return true;
	}

	if (bw <= 20)
		return false;

	/* Two independent 80 MHz segments. */
	if (width == CHAN_WIDTH_80P80) {
		if (cf1 <= 0)
			return true;
		if (cf2 <= 0)
			return true;
		if (!wpas_primary_freq_in_range_extn(freq, cf1 - 30, 4)) {
			if (!wpas_primary_freq_in_range_extn(freq, cf2 - 30, 4)) {
				wpa_printf(MSG_DEBUG,
					   "REGDOM/NOL: primary freq %d is outside "
					   "80 MHz segments cf1=%d cf2=%d",
					   freq, cf1, cf2);
				return true;
			}
		}
		if (wpas_check_channel_range_extn(wpa_s, cf1 - 30, 4,
						  check_nol, punct_bitmap))
			return true;

		if (wpas_check_channel_range_extn(wpa_s, cf2 - 30, 4,
						  check_nol, punct_bitmap))
			return true;

		return false;
	}

	/* QCN 5 GHz 240 MHz uses the bonded-channel table, not cf2 - 150. */
	if (width == CHAN_WIDTH_320 && is_5ghz_freq(freq) && cf2 > 0) {
		if (hostapd_get_n_chans_and_frequency_extn(CONF_OPER_CHWIDTH_320MHZ, cf2,
							   &num_channels, &start_freq))
			return true;
		if (!wpas_primary_freq_in_range_extn(freq, start_freq,
						     num_channels)) {
			wpa_printf(MSG_DEBUG,
				   "REGDOM/NOL: primary freq %d is outside "
				   "QCN 240 MHz range [%d-%d]",
				   freq, start_freq,
				   start_freq + (num_channels - 1) * 20);
			return true;
		}
		return wpas_check_channel_range_extn(wpa_s, start_freq, num_channels,
						     check_nol, punct_bitmap);
	}

	/* cf2 is the full contiguous 160 MHz center. */
	if (width == CHAN_WIDTH_160 && cf2 > 0) {
		if (!wpas_primary_freq_in_range_extn(freq, cf2 - 70, 8)) {
			wpa_printf(MSG_DEBUG,
				   "REGDOM/NOL: primary freq %d is outside "
				   "160 MHz range centered at %d",
				   freq, cf2);
			return true;
		}
		return wpas_check_channel_range_extn(wpa_s, cf2 - 70, 8,
						     check_nol, punct_bitmap);
	}

	if (cf1 <= 0)
		return true;

	start_freq = cf1 - (bw / 2) + 10;
	num_channels = bw / 20;
	if (!wpas_primary_freq_in_range_extn(freq, start_freq,
					      num_channels)) {
		wpa_printf(MSG_DEBUG,
				   "REGDOM/NOL: primary freq %d is outside "
				   "%d MHz range [%d-%d]",
				   freq, bw, start_freq,
				   start_freq + (num_channels - 1) * 20);
		return true;
	}

	return wpas_check_channel_range_extn(wpa_s, start_freq, num_channels,
					     check_nol, punct_bitmap);
}

/**
 * wpas_get_channel_info_extn - Extract channel info from frequency and indices
 * @freq: Primary frequency in MHz
 * @width: Channel width enum
 * @center_freq1_idx: Center frequency 1 index
 * @center_freq2_idx: Center frequency 2 index
 * @center_freq1: Output for CCFS0 in MHz. For EHT 160/320 MHz this is the
 *                segment center containing the primary channel.
 * @center_freq2: Output for CCFS1 in MHz. This is the second 80 MHz center
 *                for 80+80 MHz and the full bonded center for EHT 160/320
 *                MHz and QCN 5 GHz 240 MHz operation.
 * Returns: channel width, or CHAN_WIDTH_UNKNOWN on error
 *
 * Common helper function to extract bandwidth and center frequencies from
 * pre-parsed channel information. Used for both BSS and MLO link processing.
 */
static enum chan_width
wpas_get_channel_info_extn(int freq, enum chan_width width,
			   u8 center_freq1_idx, u8 center_freq2_idx,
			   int *center_freq1, int *center_freq2)
{
	if (!center_freq1 || !center_freq2)
		return CHAN_WIDTH_UNKNOWN;

	*center_freq1 = 0;
	*center_freq2 = 0;

	if (channel_width_to_int(width) <= 0)
		return CHAN_WIDTH_UNKNOWN;

	/* Calculate center frequencies from indices if available */
	if (center_freq1_idx) {
		*center_freq1 = wpas_center_idx_to_freq_extn(freq,
							     center_freq1_idx);
		if (*center_freq1 <= 0)
			return CHAN_WIDTH_UNKNOWN;
	}

	if (center_freq2_idx) {
		*center_freq2 = wpas_center_idx_to_freq_extn(freq,
							     center_freq2_idx);
		if (*center_freq2 <= 0)
			return CHAN_WIDTH_UNKNOWN;
	}

	/* If no center freq calculated, use primary freq */
	if (*center_freq1 == 0)
		*center_freq1 = freq;

	return width;
}

/**
 * wpas_check_link_nol_extn - Check if a single link uses NOL channels
 * @wpa_s: wpa_supplicant context
 * @freq: Primary channel frequency in MHz
 * @width: Channel width
 * @cf1: CCFS0 in MHz. For EHT 160/320 MHz this identifies the segment
 *       containing the primary channel.
 * @cf2: CCFS1 in MHz. For 80+80 MHz this identifies the second 80 MHz
 *       segment; for EHT 160/320 MHz and 5 GHz 240 MHz it identifies the
 *       full bonded-channel center.
 * Returns: true if link uses any NOL channel, false otherwise
 */
static bool wpas_check_link_nol_extn(struct wpa_supplicant *wpa_s,
				     int freq, enum chan_width width,
				     int cf1, int cf2,
				     u16 punct_bitmap)
{
	return wpas_check_link_ranges_extn(wpa_s, freq, width, cf1, cf2, true,
					   punct_bitmap);
}

/**
 * wpas_bss_uses_nol_channel_extn - Check if BSS assoc link uses NOL channels
 * @wpa_s: wpa_supplicant context
 * @bss: BSS to check (Legacy, SLO, or MLO assoc link)
 * Returns: true if the assoc link uses a NOL channel, false otherwise
 *
 * Checks whether the BSS primary (assoc) link frequency is in the NOL.
 * For MLO, only the assoc link is checked here; partner link NOL handling
 * is done earlier in wpa_bss_update_scan_rnr_res() by excluding NOL partner
 * links from the connection attempt.
 */
bool wpas_bss_uses_nol_channel_extn(struct wpa_supplicant *wpa_s,
				    struct wpa_bss *bss)
{
	enum chan_width width;
	int cf1, cf2;

	if (!bss)
		return false;

	wpa_printf(MSG_DEBUG, "NOL: Checking BSS " MACSTR " (freq=%d)",
			   MAC2STR(bss->bssid), bss->freq);

	/* Only 5G channels are subject to NOL */
	if (!is_5ghz_freq(bss->freq))
		return false;

	width = wpas_get_channel_info_extn(bss->freq, bss->max_cw,
					   bss->center_freq1_idx,
					   bss->center_freq2_idx, &cf1, &cf2);
	if (width == CHAN_WIDTH_UNKNOWN) {
		wpa_printf(MSG_DEBUG, "NOL: Failed to get bandwidth info");
		return false;
	}

	wpa_printf(MSG_DEBUG,
		   "NOL: validate BSS " MACSTR
		   " freq=%d max_cw=%d bw=%d cf1_idx=%u cf2_idx=%u "
		   "cf1=%d cf2=%d punc=0x%04x",
		   MAC2STR(bss->bssid), bss->freq, bss->max_cw,
		   channel_width_to_int(width),
		   bss->center_freq1_idx, bss->center_freq2_idx,
		   cf1, cf2, bss->punc_bitmap);

	return wpas_check_link_nol_extn(wpa_s, bss->freq, width, cf1, cf2,
					bss->punc_bitmap);
}

static bool wpas_6ghz_segment_uses_disabled_extn(struct wpa_supplicant *wpa_s,
						 const struct wpa_bss *bss,
						 int start_freq, int num_subchannels,
						 u16 punct_bitmap, int punct_offset)
{
	struct hostapd_hw_modes *mode = NULL;
	struct hostapd_channel_data *start_chan;
	u8 pwr_mode;
	u8 chan_idx, num_channels;
	int i, j;

	if (!wpa_s || start_freq <= 0 || num_subchannels <= 0)
		return true;

	pwr_mode = wpas_get_6ghz_link_pwr_mode_extn(wpa_s, bss);
	if (pwr_mode >= NL80211_REG_NUM_POWER_MODES) {
		wpa_printf(MSG_DEBUG,
			   "REGDOM: no 6 GHz power mode for freq=%d n20=%d",
			   bss ? bss->freq : start_freq, num_subchannels);
		return true;
	}

	for (i = 0; i < wpa_s->hw.num_modes; i++) {
		if (wpa_s->hw.modes[i].is_6ghz) {
			mode = &wpa_s->hw.modes[i];
			break;
		}
	}
	if (!mode)
		return true;

	start_chan = hw_mode_get_6ghz_power_mode_channel(wpa_s->hw.modes,
							 wpa_s->hw.num_modes,
							 start_freq, pwr_mode,
							 &num_channels, &chan_idx,
							 true);
	if (!start_chan || (size_t)chan_idx + num_subchannels > num_channels) {
		wpa_printf(MSG_DEBUG,
			   "REGDOM: 6 GHz segment not found "
			   "(start=%d n20=%d pwr=%d)", start_freq,
			   num_subchannels, pwr_mode);
		return true;
	}

	for (j = 0; j < num_subchannels; j++) {
		if (punct_bitmap & BIT(punct_offset + j))
			continue;

		if (start_chan[j].flag & HOSTAPD_CHAN_DISABLED) {
			wpa_printf(MSG_DEBUG,
				   "REGDOM: 6 GHz freq %d disabled "
				   "(start=%d n20=%d pwr=%d)",
				   start_chan[j].freq, start_freq,
				   num_subchannels, pwr_mode);
			return true;
		}
	}

	return false;
}

static bool wpas_check_6ghz_link_disabled_extn(struct wpa_supplicant *wpa_s,
					       const struct wpa_bss *bss,
					       int freq, enum chan_width width,
					       int cf1, int cf2,
					       u16 punct_bitmap)
{
	int bw;
	int start_freq, num_channels;

	bw = channel_width_to_int(width);
	if (!bw)
		return true;

	if (bw <= 20)
		return disabled_freq(wpa_s, freq);

	if (width == CHAN_WIDTH_80P80) {
		if (cf1 <= 0)
			return true;
		if (cf2 <= 0)
			return true;

		if (wpas_6ghz_segment_uses_disabled_extn(wpa_s, bss, cf1 - 30, 4,
							 punct_bitmap, 0))
			return true;

		return wpas_6ghz_segment_uses_disabled_extn(wpa_s, bss, cf2 - 30,
							    4, punct_bitmap, 0);
	}

	if ((width == CHAN_WIDTH_160 || width == CHAN_WIDTH_320) && cf2 > 0) {
		start_freq = cf2 - (bw == 320 ? 150 : 70);
		num_channels = bw / 20;
	} else {
		if (cf1 <= 0)
			return true;
		start_freq = cf1 - (bw / 2) + 10;
		num_channels = bw / 20;
	}

	return wpas_6ghz_segment_uses_disabled_extn(wpa_s, bss, start_freq,
						    num_channels, punct_bitmap, 0);
}

/**
 * wpas_check_link_disabled_extn - Check whether a link uses disabled
 *   channels, including all sub-channels in a bonded operating bandwidth.
 */
static bool wpas_check_link_disabled_extn(struct wpa_supplicant *wpa_s,
					  int freq, enum chan_width width,
					  int cf1, int cf2,
					  u16 punct_bitmap)
{
	return wpas_check_link_ranges_extn(wpa_s, freq, width, cf1, cf2, false,
					   punct_bitmap);
}

static bool wpas_check_disabled_channel_extn(struct wpa_supplicant *wpa_s,
					     const struct wpa_bss *bss,
					     int freq, enum chan_width width,
					     int cf1, int cf2,
					     u16 punct_bitmap)
{
	if (is_6ghz_freq(freq))
		return wpas_check_6ghz_link_disabled_extn(wpa_s, bss, freq, width,
							  cf1, cf2, punct_bitmap);

	return wpas_check_link_disabled_extn(wpa_s, freq, width, cf1, cf2,
					     punct_bitmap);
}

static bool wpas_get_signal_channel_info_extn(int freq,
					      const struct wpa_signal_info *si,
					      enum chan_width *width,
					      int *cf1, int *cf2,
					      u16 *punct_bitmap)
{
	int bw;

	if (!si || freq <= 0 || si->chanwidth == CHAN_WIDTH_UNKNOWN ||
		!width || !cf1 || !cf2 || !punct_bitmap)
		return false;

	*width = si->chanwidth;
	bw = channel_width_to_int(*width);
	if (!bw)
		return false;

	/* A 20 MHz link does not need a bonded center; wider links do. */
	if (bw <= 20) {
		*cf1 = freq;
	} else {
		if (si->center_frq1 <= 0)
			return false;
		*cf1 = si->center_frq1;
	}
	*cf2 = 0;

	/* center_freq2 is used for the second segment of 80+80 only. */
	if (si->chanwidth == CHAN_WIDTH_80P80) {
		if (si->center_frq2 > 0)
			*cf2 = si->center_frq2;
	}

	*punct_bitmap = (u16)si->punct_bitmap;

	return true;
}

bool wpas_link_uses_nol_channel_signal_extn(struct wpa_supplicant *wpa_s,
					    int freq,
					    const struct wpa_signal_info *si)
{
	enum chan_width width;
	int cf1, cf2;
	u16 punct_bitmap;

	if (!is_5ghz_freq(freq))
		return false;

	if (!wpas_get_signal_channel_info_extn(freq, si, &width, &cf1, &cf2,
					       &punct_bitmap))
		return true;

	return wpas_check_link_nol_extn(wpa_s, freq, width, cf1, cf2,
					punct_bitmap);
}

bool wpas_link_uses_disabled_channel_signal_extn(struct wpa_supplicant *wpa_s,
						 struct wpa_bss *bss, int freq,
						 const struct wpa_signal_info *si)
{
	enum chan_width width;
	int cf1, cf2;
	u16 punct_bitmap;

	if (!wpa_s)
		return true;

	if (!wpas_get_signal_channel_info_extn(freq, si, &width, &cf1, &cf2,
					       &punct_bitmap))
		return true;

	return wpas_check_disabled_channel_extn(wpa_s, bss, freq, width, cf1,
						cf2, punct_bitmap);
}

/**
 * wpas_link_uses_nol_channel_extn - Check if an arbitrary link freq/width is in NOL
 * @wpa_s: wpa_supplicant context
 * @freq: Link primary frequency in MHz
 * @width: Channel width enum
 * @cf1: Center frequency 1 in MHz
 * @cf2: Center frequency 2 in MHz (0 if not applicable)
 * Returns: true if the link uses any NOL channel, false otherwise
 *
 * Same NOL logic as wpas_bss_uses_nol_channel_extn(), but for MLO partner
 * links (bss->mld_links[]) that are not represented as a standalone
 * struct wpa_bss.
 */
bool wpas_link_uses_nol_channel_extn(struct wpa_supplicant *wpa_s, int freq,
				     enum chan_width width, int cf1, int cf2)
{
	int bw;

	if (!is_5ghz_freq(freq))
		return false;

	bw = channel_width_to_int(width);
	wpa_printf(MSG_DEBUG,
		   "NOL: validate link freq=%d width=%d bw=%d cf1=%d cf2=%d",
		   freq, width, bw, cf1, cf2);
	return wpas_check_link_nol_extn(wpa_s, freq, width, cf1, cf2, 0);
}

#include "wds_ie.h"

/**
 * wds_ie_assoc_req_len_extn - Return WDS IE wire length for assoc request
 *
 * @wpa_s: wpa_supplicant instance
 * @ssid:  Network profile
 *
 * Returns WDS_IE_TOTAL_LEN when ssid->wds_ie is enabled, 0 otherwise.
 */
size_t wds_ie_assoc_req_len_extn(struct wpa_supplicant *wpa_s,
				 struct wpa_ssid *ssid)
{
	if (!wpa_s || !ssid)
		return 0;

	if (!ssid->wds_ie)
		return 0;

	return WDS_IE_TOTAL_LEN;
}


/**
 * wds_ie_populate_assoc_req_extn - Append WDS IE to an association request
 *
 * Writes the WDS IE advertising WDS_IE_CAP_STA when ssid->wds_ie is enabled.
 *
 * @wpa_s:  wpa_supplicant instance
 * @ssid:   Network profile
 * @pos:    Current write position in the IE buffer
 * @avail:  Remaining bytes available at @pos
 *
 * Returns the updated write pointer.
 */
u8 *wds_ie_populate_assoc_req_extn(struct wpa_supplicant *wpa_s,
				   struct wpa_ssid *ssid,
				   u8 *pos, size_t avail)
{
	size_t wds_ie_len;

	if (!wpa_s || !ssid || !pos)
		return pos;

	if (!ssid->wds_ie)
		return pos;

	wds_ie_len = wds_ie_build(pos, avail, WDS_IE_CAP_STA);
	if (wds_ie_len < WDS_IE_TOTAL_LEN) {
		wpa_printf(MSG_WARNING,
			   "WDS IE: STA - failed to build WDS IE for assoc req "
			   "(buffer too small, avail=%zu)", avail);
		return pos;
	}

	wpa_printf(MSG_DEBUG,
		   "WDS IE: STA - added WDS STA capability IE to assoc req");

	return pos + wds_ie_len;
}


bool wds_ie_set_params(struct wpa_supplicant *wpa_s,
		       const u8 *wds_ie, u8 elen)
{
	struct wds_ie_params params;
	bool found = false;

	if (wds_ie_parse(wds_ie, elen, &params) == 0) {
		if (params.capability & WDS_IE_CAP_AP) {
			wpa_s->wds_ie_ap = 1;
			wpa_printf(MSG_INFO,
				   "WDS IE: STA - AP advertises WDS IE capability "
				   "(cap=0x%02x ver=%u) - ",
				   params.capability, params.version);
		} else {
			wpa_printf(MSG_DEBUG,
				  "WDS IE: STA - AP WDS IE present but WDS_IE_CAP_AP "
				  "not set (cap=0x%02x)", params.capability);
		}
		return true;
	}

	return found;
}

/**
 * wds_ie_process_assoc_resp_extn - Parse WDS IE from an association response
 *                                  and enable 4-address mode on mutual WDS
 *
 * Searches the association response IEs for the WDS vendor IE.  When found
 * and the AP advertises WDS_IE_CAP_AP, sets wpa_s->wds_ie_ap = 1 and
 * enables 4-address mode via wpa_drv_set_4addr_mode() to complete the
 * mutual WDS capability negotiation on the STA side.
 *
 * Mutual WDS activation requires BOTH:
 *   - STA has wds_ie=1 configured (ssid->wds_ie)
 *   - AP advertises WDS_IE_CAP_AP in its association response
 *
 * @wpa_s:    wpa_supplicant instance
 * @ies:      IEs from the association response frame
 * @ies_len:  Length of @ies in bytes
 */
void wds_ie_process_assoc_resp_extn(struct wpa_supplicant *wpa_s,
				    const u8 *ies, size_t ies_len)
{
	const u8 *pos;
	size_t remaining;

	if (!wpa_s)
		return;

	wpa_s->wds_ie_ap = 0;

	if (wpa_s->current_ssid)
		wpa_s->conf->conf_extn.allow_3addr_mc = wpa_s->current_ssid->allow_3addr_mc;

	if (nl80211_set_allow_3addr_mc_extn(wpa_s->drv_priv,
					    (u8)wpa_s->conf->conf_extn.allow_3addr_mc))
		wpa_printf(MSG_DEBUG,
			   "ALLOW_3ADDR_MC: driver command failed");

	/* Only process if the STA profile has wds_ie enabled */
	if (!wpa_s->current_ssid || !wpa_s->current_ssid->wds_ie)
		return;

	if (!wpa_s->enabled_4addr_mode) {
		if (wpa_drv_set_4addr_mode(wpa_s, 1) == 0) {
			wpa_s->enabled_4addr_mode = 1;
			wpa_printf(MSG_INFO, "WDS IE: STA - 4-address mode enabled");
		} else {
			wpa_printf(MSG_ERROR,
				   "WDS IE: STA - failed to enable 4-address mode");
		}
	} else {
		wpa_printf(MSG_DEBUG,
			  "WDS IE: STA - 4-address mode already enabled");
	}

	if (!ies || ies_len < WDS_IE_TOTAL_LEN)
		return;

	/* Walk the IE list looking for our WDS vendor IE */
	pos = ies;
	remaining = ies_len;

	while (remaining >= 2) {
		u8 eid  = pos[0];
		u8 elen = pos[1];

		if (2u + elen > remaining)
			break;

		if (eid == WLAN_EID_VENDOR_SPECIFIC &&
		    elen >= WDS_IE_PAYLOAD_LEN) {
			/*
			 * pos + 2 points to the OUI byte.
			 * wds_ie_parse() will verify OUI and type.
			 */
			if (wds_ie_set_params(wpa_s, (pos + 2), elen))
				break;
		}

		pos += 2 + elen;
		remaining -= 2 + elen;
	}

	if (!wpa_s->wds_ie_ap) {
		wpa_printf(MSG_DEBUG,
			   "WDS IE: STA - AP did not advertise WDS capability");
	}
}
