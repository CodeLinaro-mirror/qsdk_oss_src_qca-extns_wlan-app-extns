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
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/sme.h"
#include "../wpa_supplicant/config.h"
#include "../wpa_supplicant/ubus.h"
#include "../wpa_supplicant/bss.h"
#include "../wpa_supplicant/scan.h"
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
	if (bss->max_cw != CHAN_WIDTH_160 || !is_5ghz_freq(bss->freq))
		return;

	/* Look for QCN Vendor Specific IE for 5G 320MHz support */
	const u8 *vendor_ie = wpa_bss_get_vendor_ie(bss, QCN_IE_VENDOR_TYPE);
	if (!vendor_ie) {
		wpa_dbg(wpa_s, MSG_ERROR,
			"5G 320MHz check: QCN Vendor IE not found, keeping 160MHz");
		return;
	}

	/* vendor_ie[1] is length; need at least OUI (3) + OUI Type (1) */
	if (vendor_ie[1] < 4)
		return;

	const u8 *pos = vendor_ie + 2; /* Skip Element ID and Length */
	size_t elen = vendor_ie[1];

	/* Skip OUI (3 bytes) + OUI Type (1 byte) */
	if (pos[3] != QCN_OUI_TYPE)
		return;

	pos  += 4;
	elen -= 4;

	/* Parse TLV attributes */
	size_t off = 0;

	while (off + 2 <= elen) {
		u8 attr_id  = pos[off];
		u8 attr_len = pos[off + 1];

		if (off + 2 + attr_len > elen) {
			wpa_msg(wpa_s, MSG_INFO,
				"5G 320MHz: QCN IE malformed (TLV truncated, id=0x%02x len=%u)",
				attr_id, attr_len);
			break;
		}

		if (attr_id == QCN_ATTRIB_HE_240_MHZ_SUPP &&
		    attr_len <= QCN_HE_240_MHZ_MAX_ELEM_LEN &&
		    attr_len >= sizeof(struct ieee80211_240mhz_vendor_oper_extn_v2)) {

			const struct ieee80211_240mhz_vendor_oper_extn_v2 *oper_240 =
				(const struct ieee80211_240mhz_vendor_oper_extn_v2 *)(pos + off + 2);

			wpa_hexdump(MSG_DEBUG,
				    "5G 320MHz check: 240MHz vendor oper structure",
				    (const u8 *)oper_240,
				    sizeof(struct ieee80211_240mhz_vendor_oper_extn_v2));

			wpa_dbg(wpa_s, MSG_DEBUG,
				"5G 320MHz check: is5ghz240mhz=%u ccfs0=%u ccfs1=%u punct_bitmap=0x%04x",
				oper_240->is5ghz240mhz, oper_240->ccfs0,
				oper_240->ccfs1, le_to_host16(oper_240->punct_bitmap));

			/* Validate 5G 320MHz (240MHz) support */
			if (oper_240->is5ghz240mhz) {
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
			break;
		}
		off += 2 + attr_len;
	}
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

/**
 * wpa_get_bss_channel_oper_info_extn - Extract BSS channel operating info
 * @wpa_s: Pointer to wpa_supplicant interface
 * @bss: BSS entry whose IEs are parsed
 *
 * Parse EHT, HE and VHT operation IEs to derive maximum channel width,
 * center frequency indices and puncturing bitmap, updating the BSS fields
 * used by repeater and channel selection logic.
 */
void wpa_get_bss_channel_oper_info_extn(struct wpa_supplicant *wpa_s,
					struct wpa_bss *bss)
{
	const u8 *ie;

	/* Parse EHT Operation first:
	 * Ensures punctured channel bitmap (disabled_chan_bitmap) and EHT-specific
	 * params are captured before VHT/HE to avoid missing or being overwritten. */
	ie = get_ie_ext(bss->ies, bss->ie_len, WLAN_EID_EXT_EHT_OPERATION);
	if (ie && ie[1] >= 1) {
		const struct ieee80211_eht_operation *eht_oper =
			(const struct ieee80211_eht_operation *) (ie + 3);

		/* Clear stale puncturing bitmap. VHT IEs do not define puncturing,
		 * so if this BSS has no EHT Operation, drop any leftover bitmap. */
		if (bss->punc_bitmap > 0)
			bss->punc_bitmap = 0;

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
		default:
			/* VHT fallback width via HT Capabilities:
			 * If VHT width is inconclusive, use HT 'Supported Channel
			 * Width Set' to choose 40 MHz vs 20 MHz instead of
			 * defaulting to 20 MHz. */
			ie = wpa_bss_get_ie(bss, WLAN_EID_HT_CAP);
			if (ie && ie[1] >= 1) {
				if(WPA_GET_LE16(ie + 2) &
					HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET) {
					bss->max_cw = CHAN_WIDTH_40;
				} else {
					bss->max_cw = CHAN_WIDTH_20;
				}
			}
			break;
		}

		bss->center_freq1_idx = vht_oper->vht_op_info_chan_center_freq_seg0_idx;
		bss->center_freq2_idx = vht_oper->vht_op_info_chan_center_freq_seg1_idx;

		wpa_bss_check_5g_320mhz_vendor_ie_extn(wpa_s, bss);

		wpa_dbg(wpa_s, MSG_DEBUG, "WLAN_EID_VHT_OPERATION: width = %d"
			"center_freq1_idx = %d *center_freq2_idx = %d",
			bss->max_cw, bss->center_freq1_idx, bss->center_freq2_idx);
		return;
	}

	ie = get_ie_ext(bss->ies, bss->ie_len, WLAN_EID_EXT_HE_OPERATION);
	if (ie && ie[1] >= 1) {
		const struct ieee80211_he_operation *he_oper =
			(const struct ieee80211_he_operation *) (ie + 2);
		int offset = 0;
		u8 he_oper_chwidth;

		if (he_oper->he_oper_params & HE_OPERATION_VHT_OPER_INFO)
			offset = 3;
		if (he_oper->he_oper_params & HE_OPERATION_COHOSTED_BSS)
			offset += 1;
		if (he_oper->he_oper_params & HE_OPERATION_6GHZ_OPER_INFO) {
			const struct ieee80211_he_6ghz_oper_info *oper_info =
				(const struct ieee80211_he_6ghz_oper_info *)
				(ie + 2 + sizeof(struct ieee80211_he_operation) + 1);

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

			/* Clear stale puncturing bitmap. HE IEs do not define puncturing,
			 * so if this BSS has no EHT Operation, drop any leftover bitmap. */
			if (bss->punc_bitmap > 0)
				bss->punc_bitmap = 0;

			wpa_dbg(wpa_s, MSG_DEBUG, "WLAN_EID_EXT_HE_OPERATION:"
				"width = %d *center_freq1_idx = %d *center_freq2_idx = %d",
				bss->max_cw, bss->center_freq1_idx, bss->center_freq2_idx);
			return;
		}
	}
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
 * information for Multi-Link Device (MLD) APs in the BSS structure. In
 * Independent Repeater mode, use the neighbor BSS entry to populate
 * frequency, width, center frequency indices and puncturing bitmap for
 * the link.
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

	if (!wpa_s->conf->ind_rptr) {
		bss->valid_links |= BIT(link_id);
		l = &bss->mld_links[link_id];
		os_memcpy(l->bssid, bssid_ptr, ETH_ALEN);
		l->disabled = mld_params[2] & RNR_TBTT_INFO_MLD_PARAM2_LINK_DISABLED;
		l->freq = ieee80211_chan_to_freq(NULL, ap_info->op_class, ap_info->channel);
		return;
	}

	if (neigh_bss) {
		bss->valid_links |= BIT(link_id);
		l = &bss->mld_links[link_id];
		os_memcpy(l->bssid, bssid_ptr, ETH_ALEN);
		l->disabled = mld_params[2] & RNR_TBTT_INFO_MLD_PARAM2_LINK_DISABLED;
		l->freq = ieee80211_chan_to_freq(NULL, ap_info->op_class, ap_info->channel);
		l->center_freq1_idx = neigh_bss->center_freq1_idx;
		l->center_freq2_idx = neigh_bss->center_freq2_idx;
		l->width = neigh_bss->max_cw;
		l->punc_bitmap = neigh_bss->punc_bitmap;
		wpa_printf(MSG_DEBUG, "%s: link_id=%u freq=%d cf1=%d cf2=%d width=%d punc=%d",
			   __func__, link_id, l->freq, l->center_freq1_idx, l->center_freq2_idx,
			   l->width, l->punc_bitmap);
	}
}

/**
 * wpa_supplicant_start_sta_scan - Handle repeater AP ACS timeout for repeater
 * @eloop_ctx: Eloop context, cast to struct wpa_supplicant pointer
 * @timeout_ctx: Unused timeout context (reserved for future use)
 *
 * Invoked by the eloop timeout mechanism while in WPA_DISCONNECTED state to
 * detect when repeater AP ACS attempts have timed out. On timeout, set the
 * acs_complete and requests a fresh scan to continue repeater connect logic.
 */
void wpa_supplicant_start_sta_scan(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->wpa_state == WPA_DISCONNECTED) {
		wpa_msg(wpa_s, MSG_DEBUG, "Repeater ACS timeout");
		wpa_s->acs_complete = 1;
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
}
