// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*/

#include "includes.h"
#include "common.h"
#include "config.h"
#include "../wpa_supplicant/config.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/driver_i.h"
#include "../wpa_supplicant/ctrl_iface.h"
#include "common/version.h"
#include "common/wpa_ctrl.h"
#include "common/ieee802_11_common.h"
#include "common/ieee802_11_defs.h"
#include "common/hw_features_common.h"
#include "../wpa_supplicant/wps_supplicant.h"
#include "../wpa_supplicant/ap.h"
#include "../wpa_supplicant/scan.h"
#include "../wpa_supplicant/bss.h"
#include "wpa_supplicant_extn.h"
#include "common/defs.h"
#include "cmn.h"
#include "../wpa_supplicant/config.h"

/*
 * IEEE 802.11 action frame/IE constants used by wpa_drv_send_uplink_csa().
 *
 * References:
 * - IEEE 802.11-2020, Spectrum Management action frames
 * - IEEE 802.11-2020, Channel Switch Announcement element
 * - IEEE 802.11-2020, Wide Bandwidth Channel Switch element
 */
#define UPLINK_CSA_ACTION_CHANNEL_SWITCH_ANNOUNCEMENT 4

/*
 * CSA action frame (category + action + CSA IE):
 *   1 (Category: Spectrum Management) +
 *   1 (Action: Channel Switch Announcement) +
 *   1 (Element ID: Channel Switch) +
 *   1 (Length) +
 *   3 (CSA element body)
 */
#define UPLINK_CSA_MIN_FRAME_LEN 7

/*
 * Wide Bandwidth Channel Switch element length is 3 bytes:
 *   New Channel Width, New Channel Center Frequency Segment 0,
 *   New Channel Center Frequency Segment 1.
 */
#define UPLINK_CSA_WIDE_BW_IE_BODY_LEN 3
#define UPLINK_CSA_WIDE_BW_IE_TOTAL_LEN (2 + UPLINK_CSA_WIDE_BW_IE_BODY_LEN)

/* Channel Switch Announcement element body length is 3 bytes. */
#define UPLINK_CSA_IE_BODY_LEN 3

/*
 * Channel Switch Mode:
 * 1 = stop transmitting until the scheduled channel switch (CSA).
 */
#define UPLINK_CSA_MODE_STOP_TX 1

#define UPLINK_CSA_DUMP_ACTION_FRAME 0


/**
 * wpa_ctrl_set_wds_ie_extn - Set wds_ie for the current network profile
 *
 * Handles the "WDS_IE <0|1>" ctrl_iface command.  Sets ssid->wds_ie for
 * the currently selected network and updates wpa_s->wds_ie_ap to 0 so
 * that the next association will re-negotiate WDS capability.
 *
 * @wpa_s:      wpa_supplicant instance
 * @buf:        Command argument string ("0" or "1")
 * @reply:      Output buffer for the response
 * @reply_size: Size of @reply
 *
 * Returns number of bytes written to @reply on success, -1 on error.
 */
int wpa_ctrl_set_wds_ie_extn(struct wpa_supplicant *wpa_s, int val)
{
	struct wpa_ssid *ssid;
	int count = 0;

	if (!wpa_s)
		return -1;

	if (val < 0 || val > 1) {
		wpa_printf(MSG_ERROR,
			   "WDS IE: WDS_IE: invalid value %d (expected 0 or 1)",
			   val);
		return -1;
	}

	/* Apply to every configured network profile so the setting is
	 * effective regardless of whether the STA is currently associated. */
	if (wpa_s->conf) {
		for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
			ssid->wds_ie = val;
			count++;
			wpa_printf(MSG_DEBUG,
				   "WDS IE: WDS_IE set to %d for network id=%d",
				   val, ssid->id);
		}
	}

	if (count == 0)
		wpa_printf(MSG_DEBUG,
			   "WDS IE: WDS_IE: no configured networks found");
	else
		wpa_printf(MSG_INFO,
			   "WDS IE: WDS_IE set to %d across %d network profile(s)",
			   val, count);

	/* Reset the AP-side WDS IE flag so the next association
	 * re-negotiates WDS capability. */
	wpa_s->wds_ie_ap = 0;

	return 0;
}

/**
 * wpa_ctrl_get_wds_ie_extn - Get wds_ie state for the current network
 *
 * Handles the "GET_WDS_IE" ctrl_iface command.  Returns the current
 * wds_ie value for the selected network and the AP-side WDS IE flag.
 *
 * @wpa_s:      wpa_supplicant instance
 * @reply:      Output buffer for the response
 * @reply_size: Size of @reply
 *
 * Returns number of bytes written to @reply on success, -1 on error.
 */
int wpa_ctrl_get_wds_ie_extn(struct wpa_supplicant *wpa_s,
			     char *reply, int reply_size)
{
	int ssid_wds_ie = 0, res;

	if (!wpa_s)
		return -1;

	if (wpa_s->current_ssid)
		ssid_wds_ie = wpa_s->current_ssid->wds_ie;

	res = os_snprintf(reply, reply_size,
			  "wds_ie=%d\nwds_ie_ap=%d\n",
			  ssid_wds_ie, wpa_s->wds_ie_ap);
	if (os_snprintf_error(reply_size, res))
		return -1;
	return res;
}

/**
 * wpa_ctrl_set_allow_3addr_mc_extn - Set INTF_ALLOW_3ADDR_MC via vendor command
 *
 * Handles the "INTF_ALLOW_3ADDR_MC <0|1>" ctrl_iface command.  Sends
 * QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION with
 * QCA_WLAN_VENDOR_ATTR_CONFIG_INTF_ALLOW_3ADDR_MC attribute to the driver.
 *
 * @wpa_s:      wpa_supplicant instance
 * @buf:        Command argument string ("0" or "1")
 * @reply:      Output buffer for the response
 * @reply_size: Size of @reply
 *
 * Returns number of bytes written to @reply on success, -1 on error.
 */
int wpa_ctrl_set_allow_3addr_mc_extn(struct wpa_supplicant *wpa_s, int val)
{
	if (!wpa_s)
		return -1;

	if (val < 0 || val > 1) {
		wpa_printf(MSG_ERROR,
			   "INTF_ALLOW_3ADDR_MC: invalid value %d"
			   " (expected 0 or 1)", val);
		return -1;
	}

	if (nl80211_set_allow_3addr_mc_extn(wpa_s->drv_priv, (u8)val)) {
		wpa_printf(MSG_ERROR,
			   "ALLOW_3ADDR_MC: driver command failed");
		return -1;
	}

	wpa_s->conf->conf_extn.allow_3addr_mc = val;
	wpa_printf(MSG_INFO, "ALLOW_3ADDR_MC: set to %d", val);

	return 0;
}

bool wpas_ap_link_address_extn(struct wpa_supplicant *wpa_s, const u8 *addr)
{
	int i;

	if (!wpa_s->valid_links)
		return false;

	for_each_link(wpa_s->valid_links, i) {
		if (ether_addr_equal(wpa_s->links[i].bssid, addr))
			return true;
	}

	return false;
}


int wpa_drv_send_action_extn(struct wpa_supplicant *wpa_s, unsigned int freq,
			unsigned int wait, const u8 *dst, const u8 *src,
			const u8 *bssid, const u8 *data, size_t data_len,
			int no_cck, int link_id)
{
	if (!wpa_s->driver->send_action)
		return -1;

	if (data_len > 0 && data[0] != WLAN_ACTION_PUBLIC) {
		if (wpas_ap_link_address_extn(wpa_s, dst))
			dst = wpa_s->ap_mld_addr;

		if (wpas_ap_link_address_extn(wpa_s, bssid))
			bssid = wpa_s->ap_mld_addr;
	}

	return wpa_s->driver->send_action(wpa_s->drv_priv, freq, wait, dst, src,
					  bssid, data, data_len, no_cck, link_id);
}

static bool wpas_uplink_csa_freq_band_match(unsigned int ref_freq,
					    unsigned int link_freq)
{
	if (!ref_freq || !link_freq)
		return false;

	if (is_24ghz_freq(ref_freq))
		return is_24ghz_freq(link_freq);

	if (is_5ghz_freq(ref_freq))
		return is_5ghz_freq(link_freq);

	if (is_6ghz_freq(ref_freq))
		return is_6ghz_freq(link_freq);

	return false;
}

static int wpas_uplink_csa_get_tx_link(struct wpa_supplicant *wpa_s,
				       unsigned int new_freq,
				       unsigned int *tx_freq, int *tx_link_id)
{
	int i;

	*tx_freq = 0;
	*tx_link_id = -1;

	if (!new_freq)
		return -1;

	if (!wpa_s->valid_links) {
		if (wpas_uplink_csa_freq_band_match(new_freq, wpa_s->assoc_freq)) {
			*tx_freq = wpa_s->assoc_freq;
			return 0;
		}
		return -1;
	}

	/* Pick a live link operating in the same band as the new CSA frequency. */
	for_each_link(wpa_s->valid_links, i) {
		if (wpa_s->links[i].disabled || !wpa_s->links[i].freq)
			continue;
		if (!wpas_uplink_csa_freq_band_match(new_freq, wpa_s->links[i].freq))
			continue;
		*tx_freq = wpa_s->links[i].freq;
		*tx_link_id = i;
		return 0;
	}

	return -1;
}

static void wpas_uplink_csa_update_wb_ie_cfs(u8 primary_chan, u8 new_ch_width,
					     u8 *ch_seg_0, u8 *ch_seg_1)
{
	if (new_ch_width != CONF_OPER_CHWIDTH_160MHZ)
		return;

	/*
	 * Encode CCFS0/CCFS1 for 160 MHz per IEEE Std 802.11-2024,
	 * Table 9-316, matching hostapd_eid_wb_channel_switch():
	 * - CCFS1 is the 160 MHz center frequency segment index.
	 * - CCFS0 is the 80 MHz segment containing the primary channel.
	 */
	*ch_seg_1 = *ch_seg_0;
	if (primary_chan < *ch_seg_0)
		*ch_seg_0 -= 8;
	else
		*ch_seg_0 += 8;
}

#ifdef UPLINK_CSA_DUMP_ACTION_FRAME
static void wpas_uplink_csa_dump_action_frame(struct wpabuf *buf)
{
	const u8 *pos, *end;

	wpa_hexdump_buf(MSG_INFO, "uplink_csa: Action frame payload", buf);

	/* Walk IEs (after Category+Action) and print summary */
	pos = wpabuf_head_u8(buf) + 2;
	end = wpabuf_head_u8(buf) + wpabuf_len(buf);
	while (end - pos >= 2) {
		u8 eid = pos[0];
		u8 elen = pos[1];
		const u8 *edata = pos + 2;
		size_t ie_len = 2 + elen;

		if ((size_t) (end - pos) < ie_len) {
			wpa_printf(MSG_INFO,
				   "uplink_csa: IE parse truncated eid=%u elen=%u rem=%zu",
				   eid, elen, (size_t) (end - pos));
			break;
		}

		if (eid == WLAN_EID_CHANNEL_SWITCH && elen >= 3) {
			wpa_printf(MSG_INFO,
				   "uplink_csa: CSA IE: mode=%u new_chan=%u count=%u",
				   edata[0], edata[1], edata[2]);
		} else if (eid == WLAN_EID_WIDE_BW_CHSWITCH && elen >= 3) {
			wpa_printf(MSG_INFO,
				   "uplink_csa: WideBW IE: width=%u seg0=%u seg1=%u",
				   edata[0], edata[1], edata[2]);
		} else if (eid == WLAN_EID_EXTENSION && elen >= 3 &&
			   edata[0] == WLAN_EID_EXT_MLO_LINK_INFO) {
			wpa_printf(MSG_INFO,
				   "uplink_csa: ML Info IE: link_id_bitmap=0x%04x",
				   WPA_GET_LE16(edata + 1));
		} else if (eid == WLAN_EID_VENDOR_SPECIFIC && elen >= 4) {
			wpa_printf(MSG_INFO,
				   "uplink_csa: Vendor IE: OUI=%02x:%02x:%02x type=%u elen=%u",
				   edata[0], edata[1], edata[2], edata[3], elen);
		} else {
			wpa_printf(MSG_INFO,
				   "uplink_csa: IE parsed: eid=%u elen=%u",
				   eid, elen);
			wpa_hexdump(MSG_INFO, "uplink_csa: IE raw", pos, ie_len);
		}

		pos += ie_len;
	}
}
#endif

int wpa_drv_send_uplink_csa(struct wpa_supplicant *wpa_s, int freq,
			    u8 cs_count, u8 ch_seg_0, u8 ch_seg_1,
			    u8 new_ch_width, const u8 *nol_ie,
			    size_t nol_ie_len, int cac_abort)
{
	struct wpabuf *buf = NULL;
	u8 chan;
	int res;
	unsigned int tx_freq;
	int tx_link_id;
	bool is_wb_ie_present = false;
	size_t total_len;
	u8 width = 0;
	bool ml_info_present = false;

	if (wpa_s->wpa_state != WPA_COMPLETED)
		return -1;

	if ((new_ch_width < CONF_OPER_CHWIDTH_USE_HT) ||
	    (new_ch_width > CONF_OPER_CHWIDTH_160MHZ)) {
		wpa_printf(MSG_DEBUG, "Invalid new channel width %u", new_ch_width);
		return -1;
	}

	/*
	 * Wide Bandwidth Channel Switch element channel width encoding per
	 * IEEE Std 802.11-2024, Table 9-316 (VHT Operation Information):
	 * 0 = 40 MHz, 1 = 80 MHz / 160 MHz / 80+80 MHz.
	 *
	 * Both 80 MHz and 160 MHz use width=1. The receiver distinguishes
	 * them by checking whether cf1 (ch_seg_1 / CCFS1) is non-zero:
	 * non-zero cf1 indicates 160 MHz, zero cf1 indicates 80 MHz.
	 */
	if (new_ch_width == CONF_OPER_CHWIDTH_160MHZ ||
	    new_ch_width == CONF_OPER_CHWIDTH_80MHZ)
		width = 1;

	/* Here, "freq" refers to the new channel frequency selected by the repeater
	 * upon radar detection. This frequency is conveyed to the root AP via an
	 * uplink CSA frame, enabling the root AP to switch to the new channel.
	 */
	ieee80211_freq_to_chan(freq, &chan);
	if (wpas_uplink_csa_get_tx_link(wpa_s, freq, &tx_freq, &tx_link_id) < 0) {
		wpa_printf(MSG_ERROR,
			   "Drop uplink CSA: no valid tx link found for new freq %d",
			   freq);
		return -1;
	}

	wpas_uplink_csa_update_wb_ie_cfs(chan, new_ch_width,
					 &ch_seg_0, &ch_seg_1);

	wpa_printf(MSG_DEBUG,
		   "freq %u chan %u cs_count %u ch_seg_0 %u ch_seg_1 %u new_ch_width %u assoc_freq %u tx_freq %u tx_link_id %d",
		   freq, chan, cs_count, ch_seg_0, ch_seg_1, new_ch_width,
		   wpa_s->assoc_freq, tx_freq, tx_link_id);

	total_len = UPLINK_CSA_MIN_FRAME_LEN;
	if (chan == ch_seg_0) {
		wpa_printf(MSG_DEBUG, "20MHz, ignore adding wide band ie");
	} else {
		wpa_printf(MSG_DEBUG, "Add wide band ie");
		total_len += UPLINK_CSA_WIDE_BW_IE_TOTAL_LEN;
		is_wb_ie_present = true;
	}

	if (cac_abort && wpa_s->valid_links)
		total_len += 5; /* ML Info IE length */

	if (nol_ie && nol_ie_len > 0) {
		total_len += nol_ie_len;
		wpa_printf(MSG_INFO, "Adding NOL IE to uplink CSA, len=%zu",
			   nol_ie_len);
	}

	buf = wpabuf_alloc(total_len);

	if (!buf) {
		wpa_printf(MSG_DEBUG, "Memory allocation failed");
		return -1;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_SPECTRUM_MGMT);
	wpabuf_put_u8(buf, UPLINK_CSA_ACTION_CHANNEL_SWITCH_ANNOUNCEMENT);
	wpabuf_put_u8(buf, WLAN_EID_CHANNEL_SWITCH);
	wpabuf_put_u8(buf, UPLINK_CSA_IE_BODY_LEN);
	wpabuf_put_u8(buf, UPLINK_CSA_MODE_STOP_TX);
	wpabuf_put_u8(buf, chan);
	wpabuf_put_u8(buf, cs_count);
	if (is_wb_ie_present) {
		wpabuf_put_u8(buf, WLAN_EID_WIDE_BW_CHSWITCH);
		wpabuf_put_u8(buf, UPLINK_CSA_WIDE_BW_IE_BODY_LEN);
		wpabuf_put_u8(buf, width);
		wpabuf_put_u8(buf, ch_seg_0);
		wpabuf_put_u8(buf, ch_seg_1);
	}

	/* Add NOL IE only when Wide BW IE is present */
	if (is_wb_ie_present && nol_ie && nol_ie_len > 0) {
		wpabuf_put_data(buf, nol_ie, nol_ie_len);
		wpa_hexdump(MSG_INFO, "Uplink CSA NOL IE", nol_ie, nol_ie_len);
	}

	if (cac_abort && wpa_s->valid_links) {
		unsigned int pending_ch_switch_freq = 0;
		u16 link_id_bitmap = 0;
		int link_5g = -1;
		int i;
		bool has_non_5g_partner = false;

		for_each_link(wpa_s->valid_links, i) {
			if (wpa_s->links[i].disabled || !wpa_s->links[i].freq)
				continue;
			if (is_5ghz_freq(wpa_s->links[i].freq))
				link_5g = i;
			else
				has_non_5g_partner = true;
		}

		if (has_non_5g_partner && link_5g >= 0) {
			/* Encoding ML Info IE to the action frame */
			link_id_bitmap = BIT(link_5g);
			ml_info_present = true;
			wpabuf_put_u8(buf, WLAN_EID_EXTENSION);
			wpabuf_put_u8(buf, 3); /* ext_id (1) + bitmap (2) */
			wpabuf_put_u8(buf, WLAN_EID_EXT_MLO_LINK_INFO);
			wpabuf_put_le16(buf, link_id_bitmap);
		}

		if (tx_link_id >= 0 && tx_link_id < MAX_NUM_MLD_LINKS)
			pending_ch_switch_freq =
				wpa_s->links[tx_link_id].pending_ch_switch_freq;

		if (pending_ch_switch_freq != 0)
			tx_freq = pending_ch_switch_freq;
	}

#ifdef UPLINK_CSA_DUMP_ACTION_FRAME
	wpas_uplink_csa_dump_action_frame(buf);
#endif
	res = wpa_drv_send_action_extn(wpa_s, tx_freq, 0, wpa_s->bssid,
				       wpa_s->own_addr, wpa_s->bssid,
				       wpabuf_head(buf), wpabuf_len(buf), 0,
				       ml_info_present ? -1 : tx_link_id);
	if (res < 0)
		wpa_printf(MSG_ERROR,
			   "Failed to send uplink CSA action frame");
	wpabuf_free(buf);
	return res;
}

static const struct wpa_ssid *
wpas_get_strict_passive_ssid(struct wpa_supplicant *wpa_s,
			 const struct wpa_scan_res *scan_res_item)
{
	struct wpa_ssid *ssid;

	if (!wpa_s->conf || !is_5ghz_freq(scan_res_item->freq)) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"strict_passive_scan: configuration is not available");
		return NULL;
	}

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (ssid->disabled || !ssid->bssid_set || !ssid->ssid ||
		    !ssid->ssid_len)
			continue;

		if (ether_addr_equal(ssid->bssid, scan_res_item->bssid)) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"strict_passive_scan: matched configured SSID '%s' for BSSID " MACSTR,
				wpa_ssid_txt(ssid->ssid, ssid->ssid_len),
				MAC2STR(scan_res_item->bssid));
			return ssid;
		}
	}

	return NULL;
}

static struct wpa_scan_res *
wpas_add_scan_ssid_to_beacon_ies(struct wpa_scan_res *scan_res_item,
				 const u8 *ssid, size_t ssid_len)
{
	struct wpa_scan_res *new_res;
	size_t total_ie_len = scan_res_item->ie_len + scan_res_item->beacon_ie_len;
	size_t beacon_ie_offset = scan_res_item->ie_len;
	size_t insert_offset;
	size_t extra_len;
	u8 *base, *block, *insert_pos, *end_total, *new_ssid_ie;
	const u8 *ssid_ie;

	if (!ssid || !ssid_len || ssid_len > SSID_MAX_LEN) {
		wpa_printf(MSG_DEBUG,
			   "strict_passive_scan: invalid SSID parameters ssid=%p ssid_len=%zu",
			   ssid, ssid_len);
		return scan_res_item;
	}

	base = (u8 *) (scan_res_item + 1);
	block = base + beacon_ie_offset;
	ssid_ie = get_ie(block, scan_res_item->beacon_ie_len, WLAN_EID_SSID);
	if (!ssid_ie)
		return scan_res_item;

	/* + 2 to skip the SSID IE header: Element ID byte and length byte */
	insert_offset = (ssid_ie - block) + 2;
	extra_len = ssid_len;

	new_res = os_realloc(scan_res_item, sizeof(*new_res) + total_ie_len + extra_len);
	if (!new_res)
		return scan_res_item;

	base = (u8 *) (new_res + 1);
	block = base + beacon_ie_offset;
	insert_pos = block + insert_offset;
	end_total = base + total_ie_len;
	os_memmove(insert_pos + extra_len, insert_pos, end_total - insert_pos);

	new_ssid_ie = insert_pos - 2;
	new_ssid_ie[1] = ssid_len;
	os_memcpy(insert_pos, ssid, ssid_len);

	new_res->beacon_ie_len += extra_len;

	return new_res;
}

struct wpa_scan_res *
wpa_scan_ssid_hide_beacon_extn(struct wpa_supplicant *wpa_s,
			       struct wpa_scan_res *scan_res_item)
{
	const struct wpa_ssid *ssid;
	const u8 *new_ssid_ie;
	const u8 *ssid_ie;
	size_t beacon_ie_offset;

	ssid = wpas_get_strict_passive_ssid(wpa_s, scan_res_item);
	if (!ssid)
		return scan_res_item;

	if (!scan_res_item->beacon_ie_len) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"strict_passive_scan: no beacon IEs available for BSSID " MACSTR " ssid='%s' ssid_len=%zu",
			MAC2STR(scan_res_item->bssid),
			wpa_ssid_txt(ssid->ssid, ssid->ssid_len),
			ssid->ssid_len);
		return scan_res_item;
	}

	beacon_ie_offset = scan_res_item->ie_len;
	ssid_ie = get_ie((const u8 *) (scan_res_item + 1) + beacon_ie_offset,
			 scan_res_item->beacon_ie_len, WLAN_EID_SSID);

	if (!ssid_ie) {
		wpa_printf(MSG_DEBUG,
			   "strict_passive_scan: SSID IE itself is not present for BSSID " MACSTR,
			   MAC2STR(scan_res_item->bssid));
		return scan_res_item;
	}

	if (ssid_ie[1] != 0) {
		wpa_printf(MSG_DEBUG,
			   "strict_passive_scan: existing SSID is not hidden for BSSID " MACSTR " ssid='%s' ie_len=%u",
			   MAC2STR(scan_res_item->bssid),
			   wpa_ssid_txt(ssid_ie + 2, ssid_ie[1]),
			   ssid_ie[1]);
		return scan_res_item;
	}

	scan_res_item = wpas_add_scan_ssid_to_beacon_ies(scan_res_item,
							 ssid->ssid,
							 ssid->ssid_len);

	beacon_ie_offset = scan_res_item->ie_len;
	new_ssid_ie = get_ie((const u8 *) (scan_res_item + 1) + beacon_ie_offset,
			     scan_res_item->beacon_ie_len, WLAN_EID_SSID);
	if (new_ssid_ie && new_ssid_ie[1] == ssid->ssid_len &&
	    os_memcmp(new_ssid_ie + 2, ssid->ssid, ssid->ssid_len) == 0) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"strict_passive_scan: successfully injected hidden SSID into beacon IEs for BSSID " MACSTR " ssid='%s' ssid_len=%zu",
			MAC2STR(scan_res_item->bssid),
			wpa_ssid_txt(ssid->ssid, ssid->ssid_len),
			ssid->ssid_len);
	}

	return scan_res_item;
}

static u32 wpa_bss_get_mcst_ie_extn(const u8 *ies, size_t ies_len)
{
	const struct element *elem;

	for_each_element_extid(elem, WLAN_EID_EXT_MAX_CHANNEL_SWITCH_TIME,
			       ies, ies_len) {
		wpa_printf(MSG_DEBUG, "MLD: MCST IE candidate datalen=%u",
			   elem->datalen);
		if (elem->datalen >= 4)
			return WPA_GET_LE24(&elem->data[1]);
	}

	return 0;
}


void wpa_bss_update_mld_link_mcst_extn(struct wpa_bss *bss, u8 link_id,
				       const u8 *ies, size_t ies_len)
{
	u32 mcst;

	if (!bss || link_id >= MAX_NUM_MLD_LINKS) {
		wpa_printf(MSG_DEBUG, "MLD: skip MCST cache bss=%p link_id=%u",
			   bss, link_id);
		return;
	}

	mcst = wpa_bss_get_mcst_ie_extn(ies, ies_len);
	if (!mcst)
		return;

	bss->mld_links[link_id].mcst = mcst;
	os_get_reltime(&bss->mld_links[link_id].mcst_update_time);
	wpa_printf(MSG_INFO, "MLD: link_id=%u MCST=%u TU cached",
		   link_id, mcst);
}


u32 wpa_bss_get_mld_link_mcst_extn(struct wpa_bss *bss, u8 link_id)
{
	struct os_reltime now, age;
	u64 elapsed_tu;
	u32 mcst;

	if (!bss || link_id >= MAX_NUM_MLD_LINKS)
		return 0;

	mcst = bss->mld_links[link_id].mcst;
	if (!mcst)
		return 0;

	if (os_get_reltime(&now) < 0)
		return mcst;

	os_reltime_sub(&now, &bss->mld_links[link_id].mcst_update_time, &age);
	elapsed_tu = (u64) age.sec * 1000000 + age.usec;
	elapsed_tu /= 1024;

	if (elapsed_tu >= mcst) {
		wpa_printf(MSG_INFO, "MLD: MCST expired link_id=%u original=%u "
			   "elapsed=%llu TU",
			   link_id, mcst, (unsigned long long) elapsed_tu);
		return 0;
	}

	wpa_printf(MSG_DEBUG, "MLD: MCST residual link_id=%u original=%u "
		   "elapsed=%llu residual=%llu TU",
		   link_id, mcst, (unsigned long long) elapsed_tu,
		   (unsigned long long) (mcst - elapsed_tu));
	return mcst - elapsed_tu;
}


void wpa_bss_parse_basic_mle_per_sta_mcst_extn(struct wpa_bss *bss,
					       struct wpabuf *mlbuf,
					       size_t common_info_len)
{
	const u8 *pos, *end;
	size_t rem_len;

	pos = wpabuf_head_u8(mlbuf) + sizeof(struct ieee80211_eht_ml) +
		common_info_len;
	end = wpabuf_head_u8(mlbuf) + wpabuf_len(mlbuf);
	rem_len = end - pos;

	while (rem_len > 2) {
		size_t sub_elem_len;
		int num_frag_subelems;

		num_frag_subelems =
			ieee802_11_defrag_mle_subelem(mlbuf, pos,
						      &sub_elem_len);
		if (num_frag_subelems < 0) {
			wpa_printf(MSG_DEBUG, "MLD: ML probe response MCST "
				   "Per-STA defrag failed");
			break;
		}

		rem_len -= num_frag_subelems * 2;
		if (2 + sub_elem_len > rem_len) {
			wpa_printf(MSG_DEBUG,
				   "MLD: ML probe response MCST Per-STA "
				   "invalid subelem_len=%zu rem_len=%zu",
				   sub_elem_len, rem_len);
			break;
		}

		if (*pos == MULTI_LINK_SUB_ELEM_ID_PER_STA_PROFILE &&
		    sub_elem_len >= BASIC_MLE_STA_CTRL_LEN + 1) {
			const u8 *body = pos + 2;
			const u8 *sub_end = pos + 2 + sub_elem_len;
			u16 ctrl = WPA_GET_LE16(body);
			u8 link_id = ctrl & BASIC_MLE_STA_CTRL_LINK_ID_MASK;
			size_t sta_info_len;

			body += BASIC_MLE_STA_CTRL_LEN;
			if (link_id < MAX_NUM_MLD_LINKS &&
			    ctrl & BASIC_MLE_STA_CTRL_COMPLETE_PROFILE &&
			    body < sub_end) {
				sta_info_len = *body;
				wpa_printf(MSG_INFO,
					   "MLD: ML probe response MCST Per-STA link_id=%u "
					   "ctrl=0x%x sta_info_len=%zu profile_len=%zu",
					   link_id, ctrl, sta_info_len,
					   (size_t) (sub_end - body));
				if (sta_info_len <= (size_t) (sub_end - body))
					wpa_bss_update_mld_link_mcst_extn(
						bss, link_id, body + sta_info_len,
						sub_end - (body + sta_info_len));
			}
		}

		pos += 2 + sub_elem_len;
		rem_len -= 2 + sub_elem_len;
	}
}
