// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*/

#include "includes.h"
#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/hw_features_common.h"
#include "common/wpa_ctrl.h"
#include "common/qca-vendor.h"
#include "cmn.h"
#include <ap/hostapd.h>
#include <ap/dfs.h>
#include <ap/hw_features.h>
#include "utils/wpa_debug.h"
#include "ucode_extn.h"
#include "ubus_extn.h"
#include "dfs_extn.h"
#include "utils/common.h"
#include "utils/eloop.h"

#define IEEE80211_DFS_MIN_CAC_TIME_MS  60000
#define HAPD_DFS_WAIT_FOR_CSA_FROM_ROOT_DUR 500000

/**
 * enum qca_wlan_vendor_attr_dfs_nol_info - DFS NOL information attributes
 * Used for NOL IE in uplink CSA action frames
 *
 * @QCA_WLAN_VENDOR_ATTR_DFS_NOL_FREQ: u32 attribute
 *	Center frequency in MHz of the radar-detected channel
 * @QCA_WLAN_VENDOR_ATTR_DFS_NOL_BW: u32 attribute
 *	Bandwidth of the radar-detected channel (20, 40, 80, 160, 320 MHz)
 * @QCA_WLAN_VENDOR_ATTR_DFS_NOL_BITMAP: u16 attribute
 *	Bitmap indicating affected 20MHz subchannels within the bandwidth
 *	Bit 0 = lowest 20MHz subchannel, Bit 7 = highest (for 160MHz)
 *	Example: 0x03 = bits 0,1 set = first two 20MHz channels affected
 */
enum qca_wlan_vendor_attr_dfs_nol_info {
	QCA_WLAN_VENDOR_ATTR_DFS_NOL_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_DFS_NOL_FREQ = 1,
	QCA_WLAN_VENDOR_ATTR_DFS_NOL_BW = 2,
	QCA_WLAN_VENDOR_ATTR_DFS_NOL_BITMAP = 3,

	/* keep last */
	QCA_WLAN_VENDOR_ATTR_DFS_NOL_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_DFS_NOL_MAX =
		QCA_WLAN_VENDOR_ATTR_DFS_NOL_AFTER_LAST - 1,
};

/* QCA Vendor Element type for NOL IE in action frames */
#define QCA_VENDOR_ELEM_NOL_UPDATE 0x01

/* NOL IE constants - aligned with RCSA design */
#define DFS_MAX_20M_SUB_CH 8
#define DFS_NOL_IE_TIMEOUT_MS 1800000  /* 30 minutes */
#define MIN_DFS_SUBCHAN_BW 20          /* 20 MHz minimum subchannel */

/* NOL IE vendor element encoding/decoding constants */
#define DFS_NOL_IE_FIXED_HDR_LEN 7 /* EID+Len+OUI(3)+Type+Count */
#define DFS_NOL_IE_ENTRY_LEN 10    /* freq(4)+bw(4)+bitmap(2) */
#define DFS_NOL_IE_MIN_LEN DFS_NOL_IE_FIXED_HDR_LEN

#define DFS_NOL_IE_SINGLE_SUBCHAN_BITMAP BIT(0)

#define DFS_NOL_IE_U32_LEN 4
#define DFS_NOL_IE_U16_LEN 2

#define DFS_NOL_IE_BITMAP_MASK(n_subchans) ((u16)((1U << (n_subchans)) - 1))

enum dfs_nol_ie_bw_mhz {
	DFS_NOL_IE_BW_20_MHZ = 20,
	DFS_NOL_IE_BW_40_MHZ = 40,
	DFS_NOL_IE_BW_80_MHZ = 80,
	DFS_NOL_IE_BW_160_MHZ = 160,
	DFS_NOL_IE_BW_320_MHZ = 320,
};

int handle_action_extn(struct hostapd_data *hapd,
		       const struct ieee80211_mgmt *mgmt, size_t len,
		       unsigned int freq)
{
	if (!hapd || !mgmt)
		return 0;

	switch (mgmt->u.action.category) {
		case WLAN_ACTION_SPECTRUM_MGMT:
			 wpa_printf(MSG_DEBUG,
				    "uplink_csa: received the action frame at top most API");
			if (hostapd_uplink_csa_hdl(hapd, (const u8 *) mgmt, len))
				 return 1;
			break;
		default:
			return 0;
	}
	return 0;
}

static int dfs_nol_ie_chan_width_to_bw_mhz(enum chan_width chan_width,
					    int *bandwidth_mhz)
{
	if (!bandwidth_mhz)
		return -1;

	switch (chan_width) {
	case CHAN_WIDTH_20_NOHT:
	case CHAN_WIDTH_20:
		*bandwidth_mhz = DFS_NOL_IE_BW_20_MHZ;
		return 0;
	case CHAN_WIDTH_40:
		*bandwidth_mhz = DFS_NOL_IE_BW_40_MHZ;
		return 0;
	case CHAN_WIDTH_80:
		*bandwidth_mhz = DFS_NOL_IE_BW_80_MHZ;
		return 0;
	case CHAN_WIDTH_160:
		*bandwidth_mhz = DFS_NOL_IE_BW_160_MHZ;
		return 0;
	case CHAN_WIDTH_320:
		*bandwidth_mhz = DFS_NOL_IE_BW_320_MHZ;
		return 0;
	default:
		return -1;
	}
}

static int dfs_nol_ie_bw_mhz_to_chan_width(u32 bandwidth_mhz,
					  int *chan_width)
{
	if (!chan_width)
		return -1;

	switch (bandwidth_mhz) {
	case DFS_NOL_IE_BW_20_MHZ:
		*chan_width = CHAN_WIDTH_20;
		return 0;
	case DFS_NOL_IE_BW_40_MHZ:
		*chan_width = CHAN_WIDTH_40;
		return 0;
	case DFS_NOL_IE_BW_80_MHZ:
		*chan_width = CHAN_WIDTH_80;
		return 0;
	case DFS_NOL_IE_BW_160_MHZ:
		*chan_width = CHAN_WIDTH_160;
		return 0;
	case DFS_NOL_IE_BW_320_MHZ:
		*chan_width = CHAN_WIDTH_320;
		return 0;
	default:
		return -1;
	}
}

int dfs_is_uplink_csa_enabled(struct hostapd_iface *iface)
{
	if (!iface || !iface->conf)
		return 0;
	return iface->conf->conf_extn.uplink_csa;
}

bool hostapd_is_backhaul_sta_configured(struct hostapd_iface *iface)
{
	if (hostapd_ubus_is_bhsta_configured(iface))
		return true;
	return false;
}

void hostapd_trigger_backhaul_sta_disconnect(void *eloop_data, void *user_data)
{
	struct hostapd_iface *iface = eloop_data;
	if (!iface)
		return;

	wpa_printf(MSG_INFO, "CSA is not received from Root AP");
	hostapd_ucode_trigger_bhsta_disconnect(iface);
}

bool hostapd_uplink_csa_bh_enabled(struct hostapd_iface *iface)
{
	if (dfs_is_uplink_csa_enabled(iface) &&
	    hostapd_is_backhaul_sta_configured(iface))
		return true;
	else
		return false;
}

void hostapd_uplink_cancel_disconnect_timeout_extn(struct hostapd_iface *iface)
{
	if (!iface)
		return;

	wpa_printf(MSG_INFO, "chanswitch: cancel radar handling timer");
	eloop_cancel_timeout(hostapd_trigger_backhaul_sta_disconnect, iface, NULL);
}

static void hostapd_notify_uplink_csa(struct hostapd_iface *iface, u8 channel, int freq,
				      u8 new_ch_width, u8 ch_seg_0, u8 ch_seg_1,
				      struct dfs_nol_ie_list *nol_list)
{
	wpa_printf(MSG_INFO, "DFS channel uplink notifcation %d", channel);

	wpa_printf(MSG_INFO, "freq=%d channel=%d cs_count=%d chan_width=%d cf1=%d cf2=%d",
		   freq, channel,  10, new_ch_width, ch_seg_0, ch_seg_1);

	/* Notify wpa_supplicant to send uplink csa action frame */
	hostapd_ucode_notify_uplink_csa(iface, EVENT_DFS_UPLINK_CHANNEL_SELECTED, channel,
					freq, 10, new_ch_width, ch_seg_0, ch_seg_1,
					nol_list);

	/* start timer for fallback mechanism, disconnect backhaul station when
	 * channel switch is not received from root AP.
	 */
	if (!eloop_is_timeout_registered(hostapd_trigger_backhaul_sta_disconnect,
					 iface, NULL))
		eloop_register_timeout(0, HAPD_DFS_WAIT_FOR_CSA_FROM_ROOT_DUR,
				       hostapd_trigger_backhaul_sta_disconnect,
				       iface, NULL);
}

int hostapd_send_uplink_csa_extn(struct hostapd_iface *iface,
				 int channel, int freq,
				 int secondary_channel,
				 u8 current_vht_oper_chwidth,
				 u8 oper_centr_freq_seg0_idx,
				 u8 oper_centr_freq_seg1_idx,
				 u16 punct_bitmap)
{
	struct dfs_nol_ie_list nol_list;
	struct dfs_nol_ie_info nol_info;

	if (!iface)
		return -EINVAL;

	if (!hostapd_uplink_csa_bh_enabled(iface))
		return -EINVAL;

	os_memset(&nol_list, 0, sizeof(nol_list));
	wpa_printf(MSG_INFO,
		   "DFS: Preparing NOL IE with radar_bit_pattern=0x%04x for current channel %d freq %d chwidth %d cf0 %d cf1 %d puct 0x%x ",
		   iface->radar_bit_pattern, channel, freq,
		   current_vht_oper_chwidth, oper_centr_freq_seg0_idx,
		   oper_centr_freq_seg1_idx, punct_bitmap);
	if (dfs_prepare_nol_ie_bitmap(iface, freq, current_vht_oper_chwidth,
				      oper_centr_freq_seg0_idx,
				      oper_centr_freq_seg1_idx,
				      iface->radar_bit_pattern_extn, &nol_info) == 0) {
		nol_list.entries = &nol_info;
		nol_list.count = 1;
		wpa_printf(MSG_INFO,
			   "DFS: NOL IE prepared - freq=%u bw=%u bitmap=0x%04x",
			   nol_info.freq, nol_info.bandwidth,
			   nol_info.subchan_bitmap);
	}

	hostapd_notify_uplink_csa(iface, channel, freq,
				  current_vht_oper_chwidth,
				  oper_centr_freq_seg0_idx,
				  oper_centr_freq_seg1_idx,
				  nol_list.count > 0 ? &nol_list : NULL);
	return 0;
}

/*
 * IEEE 802.11 Spectrum Management Action frame specifics.
 *
 * References:
 * - IEEE Std 802.11, "Spectrum Management" Action frames
 * - IEEE Std 802.11, "Channel Switch Announcement" element
 * - IEEE Std 802.11, "Wide Bandwidth Channel Switch" element
 */
#define IEEE80211_SPECTRUM_MGMT_ACTION_CHANNEL_SWITCH 4

/* Minimum fixed fields after the 802.11 header for Action frames: Category+Action */
#define IEEE80211_ACTION_FRAME_MIN_FIXED_FIELDS 2

/* Channel Switch Announcement element (WLAN_EID_CHANNEL_SWITCH) fixed layout. */
#define IEEE80211_CSA_IE_MIN_LEN 3
#define IEEE80211_CSA_IE_NEW_CHANNEL_OFFSET 3
#define IEEE80211_CSA_IE_TOTAL_LEN 5

/* Wide Bandwidth Channel Switch element (WLAN_EID_WIDE_BW_CHSWITCH) fixed layout. */
#define IEEE80211_WB_CSA_IE_MIN_LEN 3
#define IEEE80211_WB_CSA_IE_CH_WIDTH_OFFSET 2
#define IEEE80211_WB_CSA_IE_CF0_OFFSET 3
#define IEEE80211_WB_CSA_IE_CF1_OFFSET 4
#define IEEE80211_WB_CSA_IE_TOTAL_LEN 5
void hostapd_handle_action_csa(struct hostapd_data *hapd,
			       const u8 *buf, size_t len)
{
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)buf;
	struct hostapd_iface *iface;
	enum oper_chan_width ch_width;
	const u8 *wb_cs_ie = NULL;
	const u8 *cs_ie = NULL;
	const u8 *pos, *end;
	int freq, sec_chan;
	u8 cf0, cf1;
	const u8 *vendor_ie = NULL;
	struct dfs_nol_ie_list nol_list;

	if (!hapd || !hapd->iface || !buf)
		return;
	iface = hapd->iface;
	os_memset(&nol_list, 0, sizeof(nol_list));

	if (len < IEEE80211_HDRLEN + IEEE80211_ACTION_FRAME_MIN_FIXED_FIELDS) {
		wpa_printf(MSG_DEBUG, "invalid action frame received");
		return;
	}

	if (mgmt->u.action.u.spectrum_mgmt.action !=
	    IEEE80211_SPECTRUM_MGMT_ACTION_CHANNEL_SWITCH) {
		wpa_printf(MSG_DEBUG, "unsupported action frame received %u",
			   mgmt->u.action.u.spectrum_mgmt.action);
		return;
	}

	wpa_printf(MSG_DEBUG, "uplink_csa: action frame received");

	end = buf + len;
	pos = mgmt->u.action.u.spectrum_mgmt.variable;
	len = end - pos;

	cs_ie = get_ie(pos, len, WLAN_EID_CHANNEL_SWITCH);
	if (!cs_ie || cs_ie[1] < IEEE80211_CSA_IE_MIN_LEN)
		return;

	u8 new_chan = cs_ie[IEEE80211_CSA_IE_NEW_CHANNEL_OFFSET];

	wpa_printf(MSG_DEBUG, "uplink_csa: channel switch to %u", new_chan);
	pos = cs_ie + IEEE80211_CSA_IE_TOTAL_LEN;
	len = end - pos;

	wb_cs_ie = get_ie(pos, len, WLAN_EID_WIDE_BW_CHSWITCH);
	if (wb_cs_ie && wb_cs_ie[1] >= IEEE80211_WB_CSA_IE_MIN_LEN) {
		ch_width = wb_cs_ie[IEEE80211_WB_CSA_IE_CH_WIDTH_OFFSET];
		cf0 = wb_cs_ie[IEEE80211_WB_CSA_IE_CF0_OFFSET];
		cf1 = wb_cs_ie[IEEE80211_WB_CSA_IE_CF1_OFFSET];
		wpa_printf(MSG_DEBUG, "uplink_csa: wide band ie: cf0 %u cf1 %u chwidth %d",
			   cf0, cf1, ch_width);
	} else {
		wpa_printf(MSG_DEBUG, "uplink_csa: no wide band ie, assuming 20MHz");
		ch_width = 0;
		cf0 = new_chan;
		cf1 = 0;
	}

	freq = hostapd_hw_get_freq(hapd, new_chan);

	if (ch_width == 0) {
		sec_chan = 0;
	} else {
		if (cf0 < new_chan)
			sec_chan = -1;
		else if (cf0 > new_chan)
			sec_chan = 1;
		else
			sec_chan = 0;
	}

	wpa_printf(MSG_DEBUG, "uplink_csa: chanel change prams: cf0 %u cf1 %u sec %u chwidth %u",
		   cf0, cf1, sec_chan, ch_width);

	if (!wb_cs_ie) {
		pos = cs_ie + IEEE80211_CSA_IE_TOTAL_LEN;
		len = end - pos;
	} else {
		pos = wb_cs_ie + IEEE80211_WB_CSA_IE_TOTAL_LEN;
		len = end - pos;
	}

	while (len >= 2) {
		u8 ie_id = *pos;
		u8 ie_len = *(pos + 1);

		if (ie_len > len - 2)
			break;

		if (ie_id == WLAN_EID_VENDOR_SPECIFIC && ie_len >= 4) {
			const u8 *oui = pos + 2;

			if (WPA_GET_BE24(oui) == OUI_QCA &&
			    oui[3] == QCA_VENDOR_ELEM_NOL_UPDATE) {
				vendor_ie = pos;
				wpa_printf(MSG_INFO,
					   "uplink_csa: Found NOL IE, len=%u",
					   ie_len);
				break;
			}
		}

		pos += 2 + ie_len;
		len -= 2 + ie_len;
	}

	if (vendor_ie) {
		u8 vendor_ie_len = vendor_ie[1] + 2;

		if (dfs_decode_nol_ie(vendor_ie, vendor_ie_len, &nol_list) == 0) {
			wpa_printf(MSG_INFO,
				   "uplink_csa: Decoded %zu NOL entries",
				   nol_list.count);

			if (dfs_process_nol_ie_bitmap(iface, &nol_list) == 0) {
				wpa_printf(MSG_INFO,
					   "uplink_csa: Successfully updated NOL from uplink CSA");
			} else {
				wpa_printf(MSG_WARNING,
					   "uplink_csa: Failed to update NOL");
			}

			dfs_free_nol_ie_list(&nol_list);
		} else {
			wpa_printf(MSG_WARNING,
				   "uplink_csa: Failed to decode NOL IE");
		}
	}

	hostapd_dfs_request_channel_switch(iface, new_chan, freq, sec_chan, ch_width, cf0, cf1, 0);
}

bool hostapd_uplink_csa_hdl(struct hostapd_data *hapd,
				 const u8 *buf, size_t len)
{
	if (!hapd || !hapd->iface || !buf)
		return 0;

	if (dfs_is_uplink_csa_enabled(hapd->iface)) {
		hostapd_handle_action_csa(hapd, buf, len);
		return 1;
	}

	return 0;
}

/**
 * dfs_prepare_nol_ie_bitmap - Prepare NOL IE from radar detection
 *
 * This function creates a NOL IE entry based on the RCSA design pattern.
 * It converts radar detection information into a subchannel bitmap format.
 */
int dfs_prepare_nol_ie_bitmap(struct hostapd_iface *iface, int freq,
			      int chan_width, int cf1, int cf2,
			      u16 radar_bitmap,
			      struct dfs_nol_ie_info *nol_info)
{
	int bandwidth_mhz;
	int n_subchans;
	u16 bitmap_mask;
	u16 radar_bitmap_oper;
	int start_subchan_idx;

	if (!iface || !nol_info) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid parameters");
		return -1;
	}

	os_memset(nol_info, 0, sizeof(*nol_info));

	if (dfs_nol_ie_chan_width_to_bw_mhz(chan_width, &bandwidth_mhz)) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Unsupported channel width %d",
			   chan_width);
		return -1;
	}

	n_subchans = bandwidth_mhz / MIN_DFS_SUBCHAN_BW;
	if (n_subchans <= 0 || n_subchans > DFS_MAX_20M_SUB_CH) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid subchannel count %d",
			   n_subchans);
		return -1;
	}

	bitmap_mask = DFS_NOL_IE_BITMAP_MASK(n_subchans);
	radar_bitmap_oper = radar_bitmap & bitmap_mask;

	if (!radar_bitmap_oper) {
		wpa_printf(MSG_DEBUG,
			   "DFS NOL IE: radar_bitmap is 0 after masking (0x%04x)",
			   radar_bitmap);
		return -1;
	}

	start_subchan_idx = 0;
	while (start_subchan_idx < n_subchans &&
	       !(radar_bitmap_oper & (1U << start_subchan_idx)))
		start_subchan_idx++;

	if (start_subchan_idx >= n_subchans)
		return -1;

	/*
	 * RCSA design for NOL IE:
	 * - freq: first affected 20 MHz subchannel frequency
	 * - subchan_bitmap: contiguous bitmap starting at freq
	 */
	nol_info->freq = (cf1 - (bandwidth_mhz / 2)) + (MIN_DFS_SUBCHAN_BW / 2) +
			(start_subchan_idx * MIN_DFS_SUBCHAN_BW);
	nol_info->bandwidth = bandwidth_mhz;
	nol_info->subchan_bitmap = radar_bitmap_oper >> start_subchan_idx;

	wpa_printf(MSG_DEBUG,
		   "DFS NOL IE: Input radar_bitmap=0x%04x for freq=%d bw=%d",
		   radar_bitmap, cf1, bandwidth_mhz);

	wpa_printf(MSG_INFO,
		   "DFS NOL IE: Prepared - freq=%u bw=%u bitmap=0x%04x",
		   nol_info->freq, nol_info->bandwidth, nol_info->subchan_bitmap);

	return 0;
}

/**
 * dfs_encode_nol_ie - Encode NOL IE into vendor-specific IE format
 *
 * Format (based on RCSA design):
 * - Element ID: WLAN_EID_VENDOR_SPECIFIC (221)
 * - Length: Variable
 * - OUI: 00:13:74 (QCA)
 * - OUI Type: QCA_VENDOR_ELEM_NOL_UPDATE (0x01)
 * - NOL Entry Count: 1 byte
 * - NOL Entries: Variable (each entry 15 bytes)
 *   - Frequency: 4 bytes (u32)
 *   - Bandwidth: 4 bytes (u32)
 *   - Subchan Bitmap: 2 bytes (u16)
 */
int dfs_encode_nol_ie(struct dfs_nol_ie_list *nol_list, u8 *buf,
		      size_t buf_len)
{
	u8 *pos = buf;
	u8 *length_pos;
	size_t i;
	size_t required_len;

	if (!nol_list || !buf || nol_list->count == 0) {
		wpa_printf(MSG_DEBUG, "DFS NOL IE: No NOL entries to encode");
		return 0;
	}

	required_len = DFS_NOL_IE_FIXED_HDR_LEN +
			(nol_list->count * DFS_NOL_IE_ENTRY_LEN);

	if (buf_len < required_len) {
		wpa_printf(MSG_ERROR,
			   "DFS NOL IE: Buffer too small (%zu < %zu)",
			   buf_len, required_len);
		return -1;
	}

	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	length_pos = pos++;
	WPA_PUT_BE24(pos, OUI_QCA);
	pos += 3;
	*pos++ = QCA_VENDOR_ELEM_NOL_UPDATE;
	*pos++ = nol_list->count;

	for (i = 0; i < nol_list->count; i++) {
		struct dfs_nol_ie_info *entry = &nol_list->entries[i];

		WPA_PUT_LE32(pos, entry->freq);
		pos += DFS_NOL_IE_U32_LEN;

		WPA_PUT_LE32(pos, entry->bandwidth);
		pos += DFS_NOL_IE_U32_LEN;

		WPA_PUT_LE16(pos, entry->subchan_bitmap);
		pos += DFS_NOL_IE_U16_LEN;

		wpa_printf(MSG_DEBUG,
			   "DFS NOL IE: Encoded entry %zu - freq=%u bw=%u bitmap=0x%04x",
			   i, entry->freq, entry->bandwidth,
			   entry->subchan_bitmap);
	}

	*length_pos = pos - length_pos - 1;

	wpa_printf(MSG_INFO, "DFS NOL IE: Encoded %zu entries, total %zu bytes",
		   nol_list->count, (size_t)(pos - buf));

	return pos - buf;
}

/**
 * dfs_decode_nol_ie - Decode NOL IE from vendor-specific IE
 */
int dfs_decode_nol_ie(const u8 *ie, size_t ie_len,
		      struct dfs_nol_ie_list *nol_list)
{
	const u8 *pos;
	u8 count;
	size_t i;
	size_t expected_len;

	if (!ie || !nol_list || ie_len < DFS_NOL_IE_MIN_LEN) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid decode parameters");
		return -1;
	}

	os_memset(nol_list, 0, sizeof(*nol_list));

	if (ie[0] != WLAN_EID_VENDOR_SPECIFIC) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid element ID %u", ie[0]);
		return -1;
	}

	if (ie[1] + 2 != ie_len) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Length mismatch %u != %zu",
			   ie[1] + 2, ie_len);
		return -1;
	}

	pos = ie + 2;

	if (WPA_GET_BE24(pos) != OUI_QCA) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid OUI");
		return -1;
	}
	pos += 3;

	if (*pos != QCA_VENDOR_ELEM_NOL_UPDATE) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid OUI type %u", *pos);
		return -1;
	}
	pos++;
	count = *pos++;

	if (count == 0 || count > DFS_MAX_20M_SUB_CH) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid entry count %u", count);
		return -1;
	}

	expected_len = count * DFS_NOL_IE_ENTRY_LEN;
	if (ie_len - (pos - ie) < expected_len) {
		wpa_printf(MSG_ERROR,
			   "DFS NOL IE: Insufficient data for %u entries", count);
		return -1;
	}

	nol_list->entries = os_calloc(count, sizeof(struct dfs_nol_ie_info));
	if (!nol_list->entries) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Memory allocation failed");
		return -1;
	}
	nol_list->count = count;

	for (i = 0; i < count; i++) {
		struct dfs_nol_ie_info *entry = &nol_list->entries[i];

		entry->freq = WPA_GET_LE32(pos);
		pos += DFS_NOL_IE_U32_LEN;

		entry->bandwidth = WPA_GET_LE32(pos);
		pos += DFS_NOL_IE_U32_LEN;

		entry->subchan_bitmap = WPA_GET_LE16(pos);
		pos += DFS_NOL_IE_U16_LEN;

		wpa_printf(MSG_INFO,
			   "DFS NOL IE: Decoded entry %zu - freq=%u bw=%u bitmap=0x%04x",
			   i, entry->freq, entry->bandwidth,
			   entry->subchan_bitmap);
	}

	return 0;
}

/**
 * dfs_process_nol_ie_bitmap - Process received NOL IE and update local NOL
 *
 * This function processes NOL IE entries received from uplink CSA and
 * marks the corresponding channels as DFS_UNAVAILABLE in the local NOL.
 */
int dfs_process_nol_ie_bitmap(struct hostapd_iface *iface,
			      struct dfs_nol_ie_list *nol_list)
{
	size_t i;
	int ret = 0;

	if (!iface || !nol_list || nol_list->count == 0) {
		wpa_printf(MSG_DEBUG, "DFS NOL IE: No entries to process");
		return 0;
	}

	wpa_printf(MSG_INFO, "DFS NOL IE: Processing %zu NOL entries",
		   nol_list->count);

	for (i = 0; i < nol_list->count; i++) {
		struct dfs_nol_ie_info *entry = &nol_list->entries[i];
		int chan_width;
		int n_subchans;
		u16 bitmap;
		u16 mask;
		u32 startfreq;
		u32 centerfreq;
		u32 block_first20;
		int shift;

		if (dfs_nol_ie_bw_mhz_to_chan_width(entry->bandwidth, &chan_width)) {
			wpa_printf(MSG_WARNING,
				   "DFS NOL IE: Unsupported bandwidth %u MHz",
				   entry->bandwidth);
			continue;
		}

		/*
		 * NOL IE (RCSA design): entry->freq is the first affected 20 MHz
		 * subchannel frequency (startfreq), and entry->subchan_bitmap contains
		 * a contiguous bitmap starting at startfreq.
		 *
		 * set_dfs_state() expects the bitmap to be relative to the channel block
		 * starting frequency derived from the center frequency (cf1). So convert
		 * (startfreq, contiguous_bitmap) into an operating-block radar_bitmap.
		 */
		startfreq = entry->freq;
		n_subchans = entry->bandwidth / MIN_DFS_SUBCHAN_BW;
		if (n_subchans <= 0 || n_subchans > DFS_MAX_20M_SUB_CH) {
			wpa_printf(MSG_WARNING,
				   "DFS NOL IE: Invalid subchannel count %d (bw=%u)",
				   n_subchans, entry->bandwidth);
			continue;
		}

		mask = DFS_NOL_IE_BITMAP_MASK(n_subchans);
		bitmap = entry->subchan_bitmap & mask;
		if (!bitmap) {
			wpa_printf(MSG_DEBUG,
				   "DFS NOL IE: Empty bitmap for freq %u bw %u",
				   entry->freq, entry->bandwidth);
			continue;
		}

		/*
		 * Compute center frequency (cf1) for set_dfs_state(), based on startfreq.
		 * For an N*20 MHz block, center = startfreq + (N/2)*20 - 10.
		 */
		centerfreq = startfreq + (n_subchans / 2) * MIN_DFS_SUBCHAN_BW -
			(MIN_DFS_SUBCHAN_BW / 2);
		block_first20 = centerfreq - (entry->bandwidth / 2) +
			(MIN_DFS_SUBCHAN_BW / 2);
		shift = (startfreq - block_first20) / MIN_DFS_SUBCHAN_BW;
		if (shift < 0 || shift >= n_subchans) {
			wpa_printf(MSG_WARNING,
				   "DFS NOL IE: Invalid startfreq %u for bw=%u",
				   startfreq, entry->bandwidth);
			continue;
		}

		bitmap <<= shift;
		bitmap &= mask;

		if (!set_dfs_state(iface, centerfreq, 1, 0, chan_width,
				   centerfreq, 0,
				   HOSTAPD_CHAN_DFS_UNAVAILABLE,
				   bitmap)) {
			wpa_printf(MSG_WARNING,
				   "DFS NOL IE: Failed to mark centerfreq %u as unavailable",
				   centerfreq);
			ret = -1;
		} else {
			wpa_printf(MSG_INFO,
				   "DFS NOL IE: Marked startfreq %u (bw=%u, bitmap=0x%04x) as NOL",
				   startfreq, entry->bandwidth, bitmap);
		}
	}

	return ret;
}

/**
 * dfs_free_nol_ie_list - Free NOL IE list
 */
void dfs_free_nol_ie_list(struct dfs_nol_ie_list *nol_list)
{
	if (!nol_list)
		return;

	if (nol_list->entries) {
		os_free(nol_list->entries);
		nol_list->entries = NULL;
	}
	nol_list->count = 0;
}

/**
 * dfs_get_nol_ie_from_iface - Get current NOL as IE list
 *
 * This function extracts the current NOL from the interface and
 * converts it to NOL IE format for transmission.
 */
int dfs_get_nol_ie_from_iface(struct hostapd_iface *iface,
			      struct dfs_nol_ie_list *nol_list)
{
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan;
	size_t nol_count = 0;
	size_t i, j;

	if (!iface || !nol_list) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Invalid parameters");
		return -1;
	}

	os_memset(nol_list, 0, sizeof(*nol_list));

	mode = iface->current_mode;
	if (!mode) {
		wpa_printf(MSG_DEBUG, "DFS NOL IE: No current mode");
		return 0;
	}

	for (i = 0; i < mode->num_channels; i++) {
		chan = &mode->channels[i];
		if ((chan->flag & HOSTAPD_CHAN_RADAR) &&
		    (chan->flag & HOSTAPD_CHAN_DFS_MASK) ==
		     HOSTAPD_CHAN_DFS_UNAVAILABLE) {
			nol_count++;
		}
	}

	if (nol_count == 0) {
		wpa_printf(MSG_DEBUG, "DFS NOL IE: No NOL channels");
		return 0;
	}

	nol_list->entries = os_calloc(nol_count,
				      sizeof(struct dfs_nol_ie_info));
	if (!nol_list->entries) {
		wpa_printf(MSG_ERROR, "DFS NOL IE: Memory allocation failed");
		return -1;
	}

	j = 0;
	for (i = 0; i < mode->num_channels && j < nol_count; i++) {
		chan = &mode->channels[i];
		if ((chan->flag & HOSTAPD_CHAN_RADAR) &&
		    (chan->flag & HOSTAPD_CHAN_DFS_MASK) ==
		     HOSTAPD_CHAN_DFS_UNAVAILABLE) {
			struct dfs_nol_ie_info *entry = &nol_list->entries[j];

			entry->freq = chan->freq;
			entry->bandwidth = DFS_NOL_IE_BW_20_MHZ;
			entry->subchan_bitmap = DFS_NOL_IE_SINGLE_SUBCHAN_BITMAP;

			wpa_printf(MSG_DEBUG,
				   "DFS NOL IE: Added NOL channel freq=%u",
				   entry->freq);
			j++;
		}
	}

	nol_list->count = j;

	wpa_printf(MSG_INFO, "DFS NOL IE: Extracted %zu NOL channels",
		   nol_list->count);

	return 0;
}
