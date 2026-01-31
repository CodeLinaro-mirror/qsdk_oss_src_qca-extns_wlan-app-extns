// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*/

#include "includes.h"
#include "common.h"
#include <libubus.h>
#include "config.h"
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
#include "wpa_supplicant_extn.h"
#include "common/defs.h"
#include "cmn.h"

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

int wpa_drv_send_uplink_csa(struct wpa_supplicant *wpa_s, int freq,
			    u8 cs_count, u8 ch_seg_0, u8 ch_seg_1,
			    u8 new_ch_width, const u8 *nol_ie,
			    size_t nol_ie_len)
{
	struct wpabuf *buf = NULL;
	u8 chan;
	u8 res;
	bool is_wb_ie_present = false;
	size_t total_len;
	u8 width = (new_ch_width == CONF_OPER_CHWIDTH_80MHZ ||
		    new_ch_width == CONF_OPER_CHWIDTH_160MHZ) ? 1 : 0;

	if (wpa_s->wpa_state != WPA_COMPLETED)
		return -1;

	if ((new_ch_width < CONF_OPER_CHWIDTH_USE_HT) ||
	    (new_ch_width > CONF_OPER_CHWIDTH_160MHZ)) {
		wpa_printf(MSG_DEBUG, "Invalid new channel width %u", new_ch_width);
		return -1;
	}

	ieee80211_freq_to_chan(freq, &chan);
	wpa_printf(MSG_DEBUG, "freq %u chan %u cs_count %u ch_seg_0 %u ch_seg_1 %u new_ch_width %u",
		   freq, chan, cs_count, ch_seg_0, ch_seg_1, new_ch_width);

	/* Calculate total buffer size including NOL IE */
	total_len = UPLINK_CSA_MIN_FRAME_LEN;
	if (chan == ch_seg_0) {
		wpa_printf(MSG_DEBUG, "20MHz, ignore adding wide band ie");
	} else {
		wpa_printf(MSG_DEBUG, "Add wide band ie");
		total_len += UPLINK_CSA_WIDE_BW_IE_TOTAL_LEN;
		is_wb_ie_present = true;
	}

	/* Add NOL IE length if present */
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
	if (!is_wb_ie_present)
		goto send_action;

	wpabuf_put_u8(buf, WLAN_EID_WIDE_BW_CHSWITCH);
	wpabuf_put_u8(buf, UPLINK_CSA_WIDE_BW_IE_BODY_LEN);
	wpabuf_put_u8(buf, width);
	wpabuf_put_u8(buf, ch_seg_0);
	wpabuf_put_u8(buf, ch_seg_1);

	/* Add NOL IE if present */
	if (nol_ie && nol_ie_len > 0) {
		wpabuf_put_data(buf, nol_ie, nol_ie_len);
		wpa_hexdump(MSG_INFO, "Uplink CSA NOL IE", nol_ie, nol_ie_len);
	}

send_action:
	res = wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
				  wpa_s->own_addr, wpa_s->bssid,
				  wpabuf_head(buf), wpabuf_len(buf), 0);
	if (res < 0)
		wpa_printf(MSG_ERROR,
			   "Failed to send uplink CSA action frame");
	wpabuf_free(buf);
	return res;
}
