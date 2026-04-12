/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/hw_features_common.h"
#include "common/wpa_ctrl.h"
#include "cmn.h"
#include <ap/hostapd.h>
#include <ap/dfs.h>
#include <ap/hw_features.h>
#include "utils/wpa_debug.h"
#include "ucode_extn.h"
#include "ubus_extn.h"
#include "dfs_extn.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/driver_i.h"
#include "../wpa_supplicant/bss.h"
#include "common/ieee802_11_common.h"
#include "common/ieee802_11_defs.h"
#include "wpa_supplicant_extn.h"
#include "utils/common.h"

/*
 * RCSA Vendor Specific Action frame:
 * category(1) + Atheros OUI(3) + CSA IE [+ optional QCA NOL IE].
 */

#define RCSA_VENDOR_ACTION_HDR_LEN 4
#define RCSA_CSA_IE_HDR_LEN 2
#define RCSA_CSA_IE_BODY_LEN 3
#define RCSA_MIN_FRAME_LEN \
	(RCSA_VENDOR_ACTION_HDR_LEN + RCSA_CSA_IE_HDR_LEN + \
	 RCSA_CSA_IE_BODY_LEN)
#define QCA_VENDOR_OUI_0 0x00
#define QCA_VENDOR_OUI_1 0x03
#define QCA_VENDOR_OUI_2 0x7f
#define RCSA_CSA_IE_MODE_OFFSET 2
#define RCSA_CSA_IE_NEW_CHANNEL_OFFSET 3
#define RCSA_CSA_IE_COUNT_OFFSET 4


/* To be revisited to send 5 RCSAs */
#define HOSTAPD_RCSA_TX_COUNT 1
#define HOSTAPD_RCSA_SWITCH_MODE 1


/* RCSA config to be revisited once cswopt is introduced.
 * Hardcoding this to DISABLE for now
 */
static int dfs_is_rcsa_tx_enabled(struct hostapd_iface *iface)
{
	if (!iface || !iface->conf)
		return 0;

	return iface->conf->conf_extn.rcsa_tx;
}

static bool hostapd_rcsa_tx_bh_enabled(struct hostapd_iface *iface)
{
	if (dfs_is_rcsa_tx_enabled(iface) &&
	    hostapd_is_backhaul_sta_configured(iface))
		return true;

	return false;
}

#ifdef UCODE_SUPPORT
static int wpa_drv_send_rcsa(struct wpa_supplicant *wpa_s, int freq,
			     u32 chan, u8 cs_count, u32 switch_mode)
{
	struct wpabuf *buf = NULL;
	s8 res;
	s8 target_hw_idx;
	s8 link_id = -1;
	u8 i = 0;
	u32 tx_freq = wpa_s->assoc_freq;
	size_t total_len = RCSA_MIN_FRAME_LEN;

	if (wpa_s->wpa_state != WPA_COMPLETED)
		return -1;

	wpa_printf(MSG_DEBUG, "RCSA: freq %u chan %u cs_count %u",
		   freq, chan, cs_count);

	buf = wpabuf_alloc(total_len);
	if (!buf) {
		wpa_printf(MSG_DEBUG, "RCSA: Memory allocation failed");
		return -1;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_VENDOR_SPECIFIC);
	wpabuf_put_u8(buf, QCA_VENDOR_OUI_0);
	wpabuf_put_u8(buf, QCA_VENDOR_OUI_1);
	wpabuf_put_u8(buf, QCA_VENDOR_OUI_2);
	wpabuf_put_u8(buf, WLAN_EID_CHANNEL_SWITCH);
	wpabuf_put_u8(buf, RCSA_CSA_IE_BODY_LEN);
	wpabuf_put_u8(buf, switch_mode);
	wpabuf_put_u8(buf, 36);
	wpabuf_put_u8(buf, cs_count);

	/*
	 * hapd and wpa link ids in a repeater may not map to same
	 * band. Derive the link_id to be used for tx of rcs in wpa side.
	 * consider case for splitphy as well.
	 */
	if (wpa_s->valid_links) {
		target_hw_idx = wpa_get_hw_idx_by_freq(wpa_s, freq);
		for_each_link(wpa_s->valid_links, i) {
			if (is_5ghz_freq(wpa_s->links[i].freq) &&
			    (target_hw_idx == wpa_get_hw_idx_by_freq(wpa_s, wpa_s->links[i].freq))) {
				link_id = i;
				tx_freq = wpa_s->links[i].freq;
				break;
			}
		}
	}

	wpa_printf(MSG_INFO, "RCSA: sending on %d link freq %u",
		   link_id, tx_freq);

	res = wpa_drv_send_action_extn(wpa_s, tx_freq, 0,
				       wpa_s->bssid,
				       wpa_s->own_addr, wpa_s->bssid,
				       wpabuf_head(buf), wpabuf_len(buf),
				       0, link_id);
	if (res < 0)
		wpa_printf(MSG_ERROR, "RCSA: Failed to send action frame");
	wpabuf_free(buf);

	return res;
}

static int hostapd_ucode_notify_rcsa_tx(struct hostapd_iface *iface, u8 channel,
					int freq, u8 switch_mode)
{
	uc_value_t *val = NULL;
	uc_value_t *iface_registry = NULL;
	struct uc_vm *vm = NULL;
	s8 hw_idx = 0;

	wpa_printf(MSG_INFO, "RCSA TX notify: freq=%d channel=%d",
		   freq, channel);

	if (!iface->ucode.idx)
		return -1;

	iface_registry = ucode_ap_fetch_iface_reg_extn();
	if (!iface_registry)
		return -1;

	vm = ucode_ap_fetch_vm_extn();
	if (!vm)
		return -1;

	val = wpa_ucode_registry_get(iface_registry, iface->ucode.idx);
	if (!val)
		return -1;

	if (wpa_ucode_call_prepare("event"))
		return -1;

	uc_value_push(ucv_get(ucv_string_new(iface->phy)));

	if (iface->current_hw_info)
		hw_idx = iface->current_hw_info->hw_idx;

	uc_value_push(ucv_get(ucv_int64_new(hw_idx)));
	uc_value_push(ucv_get(val));
	uc_value_push(ucv_get(ucv_string_new(event_to_string(EVENT_DFS_RCSA_TX))));
	val = ucv_object_new(vm);
	uc_value_push(ucv_get(val));

	ucv_object_add(val, "frequency", ucv_int64_new(freq));
	ucv_object_add(val, "channel", ucv_int64_new(channel));
	ucv_object_add(val, "csa_count", ucv_int64_new(HOSTAPD_RCSA_TX_COUNT));
	ucv_object_add(val, "switch_mode", ucv_int64_new(switch_mode));

	ucv_put(wpa_ucode_call(5));
	ucv_gc(vm);

	wpa_printf(MSG_INFO, "RCSA event with radio id %d %s\n",
		   hw_idx, iface->phy);
	return 0;
}

struct uc_value *uc_wpas_notify_rcsa_extn(struct uc_vm *vm, size_t nargs)
{
	struct wpa_supplicant *wpa_s = uc_fn_thisval("wpas.iface");
	uc_value_t *info = uc_fn_arg(0);
	u32 freq = 0;
	u32 chan = 0;
	u32 cs_count = 0;
	u32 switch_mode = 0;
	u64 intval;

	if (!wpa_s || ucv_type(info) != UC_OBJECT)
		return NULL;

	if ((intval = ucv_int64_get(ucv_object_get(info, "csa_count",
						   NULL))) && !errno)
		cs_count = intval;
	if ((intval = ucv_int64_get(ucv_object_get(info, "frequency",
						   NULL))) && !errno)
		freq = intval;
	if ((intval = ucv_int64_get(ucv_object_get(info, "channel",
						   NULL))) && !errno)
		chan = intval;
	if ((intval = ucv_int64_get(ucv_object_get(info, "switch_mode", NULL)))
	    && !errno)
		switch_mode = intval;

	wpa_drv_send_rcsa(wpa_s, freq, chan, cs_count, switch_mode);

	return ucv_boolean_new(1);
}
#else
static int hostapd_ucode_notify_rcsa_tx(struct hostapd_iface *iface, u8 channel,
					int freq, u8 switch_mode)
{
	return -1;
}
#endif

int hostapd_send_rcsa_extn(struct hostapd_iface *iface,
			   int channel, int freq,
			   int secondary_channel,
			   u8 current_vht_oper_chwidth,
			   u8 oper_centr_freq_seg0_idx,
			   u8 oper_centr_freq_seg1_idx,
			   u16 punct_bitmap)
{
	if (!hostapd_rcsa_tx_bh_enabled(iface))
		return -EINVAL;

	wpa_printf(MSG_INFO, "RCSA: received send req");

	return hostapd_ucode_notify_rcsa_tx(iface, channel, freq,
					    HOSTAPD_RCSA_SWITCH_MODE);
}

static int hostapd_parse_rcsa_frame(struct hostapd_data *hapd,
				    const u8 *buf, size_t len,
				    u8 *new_chan, int *freq,
				    u8 *csa_count, u8 *switch_mode)
{
	const u8 *cs_ie = NULL;
	const u8 *pos, *end;

	if (len < IEEE80211_HDRLEN + RCSA_VENDOR_ACTION_HDR_LEN) {
		wpa_printf(MSG_ERROR, "rcsa: frame too short");
		return -EINVAL;
	}

	end = buf + len;
	pos = buf + IEEE80211_HDRLEN + RCSA_VENDOR_ACTION_HDR_LEN;

	len = end - pos;

	cs_ie = get_ie(pos, len, WLAN_EID_CHANNEL_SWITCH);
	if (!cs_ie || cs_ie[1] < RCSA_CSA_IE_BODY_LEN) {
		wpa_printf(MSG_ERROR, "rcsa: no valid CSA IE found");
		return -EINVAL;
	}

	*new_chan = cs_ie[RCSA_CSA_IE_NEW_CHANNEL_OFFSET];
	*csa_count = cs_ie[RCSA_CSA_IE_COUNT_OFFSET];
	*switch_mode = cs_ie[RCSA_CSA_IE_MODE_OFFSET];
	*freq = hostapd_hw_get_freq(hapd, *new_chan);

	wpa_printf(MSG_DEBUG, "rcsa: chan %u, csa_cnt %u, switch_mode %u, freq %u",
		   *new_chan, *csa_count, *switch_mode, *freq);

	return 0;
}


bool hostapd_rcsa_rx_hdl(struct hostapd_data *hapd,
			 const u8 *buf, size_t len)
{
	struct hostapd_iface *iface;
	u8 new_chan = 0;
	u8 csa_count = 0;
	u8 switch_mode = 0;
	int freq = 0;

	iface = hapd->iface;

	if (!iface->conf->conf_extn.process_rcsa)
		return 1;

	wpa_printf(MSG_INFO, "RCSA: received RCSA from repeater");

	if (hostapd_parse_rcsa_frame(hapd, buf, len, &new_chan, &freq,
				     &csa_count, &switch_mode))
		return 0;

	return hostapd_ucode_notify_rcsa_tx(iface, new_chan, freq,
					    switch_mode);
}
