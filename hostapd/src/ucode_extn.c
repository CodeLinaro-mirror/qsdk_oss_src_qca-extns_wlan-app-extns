// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*/

#include "includes.h"
#include "common.h"
#include <sys/un.h>
#include "utils/includes.h"
#include "utils/ucode.h"
#include <ap/hostapd.h>
#include "ucode_extn.h"
#include "dfs_extn.h"
#include "ap/beacon.h"
#include "ap/ap_drv_ops.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/bss.h"
#include "wpa_supplicant_extn.h"
#include "cmn.h"

static u32 get_mcst_from_bss_extn(struct wpa_bss *bss, int link_id)
{
	u32 mcst;

	if (!bss)
		return 0;

	mcst = wpa_bss_get_mld_link_mcst_extn(bss, link_id);
	if (!mcst)
		return 0;

	wpa_printf(MSG_INFO, "%s: link_id=%d bssid=" MACSTR " mcst=%u",
		   __func__, link_id, MAC2STR(bss->bssid), mcst);

	return mcst;
}

u32 wpas_ucode_get_link_mcst_extn(struct wpa_supplicant *wpa_s, int link_id)
{
	u32 mcst;

	if (!wpa_s || link_id < 0 || link_id >= MAX_NUM_MLD_LINKS)
		return 0;

	mcst = get_mcst_from_bss_extn(wpa_s->links[link_id].bss, link_id);
	if (mcst)
		return mcst;

	return get_mcst_from_bss_extn(wpa_s->current_bss, link_id);
}


bool wpas_ucode_freq_range_is_dfs(int center_freq, int bandwidth)
{
	int start_freq, end_freq, subchan_freq;

	if (bandwidth <= 20)
		return ieee80211_is_dfs(center_freq, NULL, 0);

	start_freq = center_freq - bandwidth / 2 + 10;
	end_freq = center_freq + bandwidth / 2 - 10;

	for (subchan_freq = start_freq; subchan_freq <= end_freq;
	     subchan_freq += 20) {
		if (ieee80211_is_dfs(subchan_freq, NULL, 0))
			return true;
	}

	return false;
}

bool wpas_ucode_is_dfs_chandef(int freq, enum chan_width ch_width,
			       int cf1, int cf2)
{
	int bw;

	if (ieee80211_is_dfs(freq, NULL, 0))
		return true;

	bw = channel_width_to_int(ch_width);
	if (bw <= 20)
		return false;

	if (ch_width == CHAN_WIDTH_80P80) {
		if (cf1 > 0 && wpas_ucode_freq_range_is_dfs(cf1, 80))
			return true;
		return cf2 > 0 && wpas_ucode_freq_range_is_dfs(cf2, 80);
	}

	if (bw == 320 && cf2 > 0) {
		if (cf1 > 0 && wpas_ucode_freq_range_is_dfs(cf1, 160))
			return true;
		return wpas_ucode_freq_range_is_dfs(cf2, 80);
	}

	if (bw == 160 && cf2 > 0)
		return wpas_ucode_freq_range_is_dfs(cf2, 160);

	if (cf1 > 0)
		return wpas_ucode_freq_range_is_dfs(cf1, bw);

	if (freq > 0)
		return wpas_ucode_freq_range_is_dfs(freq, bw);

	return false;
}

#ifdef UCODE_SUPPORT
void hostapd_ucode_notify_uplink_csa(struct hostapd_iface *hapd, int event, u8 channel,
				     int freq, int csa_count, u8 new_ch_width,
				     u8 ch_seg_0, u8 ch_seg_1,
				     const dfs_nol_ie_info *nol_info)
{
	uc_value_t *val = NULL;
	uc_value_t *iface_registry = NULL;
	struct uc_vm *vm = NULL;
	s8 hw_idx = 0;

	if (event != EVENT_DFS_UPLINK_CHANNEL_SELECTED)
		return;

	if (!hapd || !hapd->ucode.idx)
		return;

	iface_registry = ucode_ap_fetch_iface_reg_extn();
	if (!iface_registry)
		return;

	vm = ucode_ap_fetch_vm_extn();
	if (!vm)
		return;

	val = wpa_ucode_registry_get(iface_registry, hapd->ucode.idx);
	if (!val)
		return;

	if (wpa_ucode_call_prepare("event"))
		return;

	uc_value_push(ucv_get(ucv_string_new(hapd->phy)));

	if (hapd->current_hw_info)
		hw_idx = hapd->current_hw_info->hw_idx;

	wpa_printf(MSG_INFO, "%s: Channel switch event with radio id %d %s",
		   __func__, hw_idx, hapd->phy);
	uc_value_push(ucv_get(ucv_int64_new(hw_idx)));

	uc_value_push(ucv_get(val));
	uc_value_push(ucv_get(ucv_string_new(event_to_string(event))));
	val = ucv_object_new(vm);
	uc_value_push(ucv_get(val));

	ucv_object_add(val, "frequency", ucv_int64_new(freq));
	ucv_object_add(val, "channel", ucv_int64_new(channel));
	ucv_object_add(val, "csa_count", ucv_int64_new(csa_count));
	ucv_object_add(val, "new_ch_width", ucv_int64_new(new_ch_width));
	ucv_object_add(val, "ch_seg_0", ucv_int64_new(ch_seg_0));
	ucv_object_add(val, "ch_seg_1", ucv_int64_new(ch_seg_1));
	ucv_object_add(val, "cac_abort",
		       ucv_int64_new(hapd->iface_extn.cac_abort ? 1 : 0));

	if (nol_info) {
		uc_value_t *nol_entry = ucv_object_new(vm);

		if (nol_entry) {
			ucv_object_add(nol_entry, "freq",
				       ucv_int64_new(nol_info->freq));
			ucv_object_add(nol_entry, "bandwidth",
				       ucv_int64_new(nol_info->bandwidth));
			ucv_object_add(nol_entry, "bitmap",
				       ucv_int64_new(nol_info->subchan_bitmap));
			ucv_object_add(val, "nol_channel", nol_entry);
			wpa_printf(MSG_INFO,
				   "%s: NOL entry freq=%u bw=%u bitmap=0x%04x",
				   __func__, nol_info->freq, nol_info->bandwidth,
				   nol_info->subchan_bitmap);
		}
	}

	ucv_put(wpa_ucode_call(5));
	ucv_gc(vm);
}

void hostapd_ucode_trigger_bhsta_disconnect(struct hostapd_iface *hapd)
{
	uc_value_t *val;
	s8 hw_idx = 0;
	uc_value_t *iface_registry = NULL;
	struct uc_vm *vm = NULL;

	if (!hapd || !hapd->ucode.idx)
		return;

	iface_registry = ucode_ap_fetch_iface_reg_extn();
	if (!iface_registry)
		return;

	vm = ucode_ap_fetch_vm_extn();
	if (!vm)
		return;

	val = wpa_ucode_registry_get(iface_registry, hapd->ucode.idx);

	if (!val) {
		wpa_printf(MSG_ERROR, "%s: ucode ref not found", __func__);
		return;
	}

	if (wpa_ucode_call_prepare("disconnect_backhaul"))
		return;

	uc_value_push(ucv_get(ucv_string_new(hapd->phy)));

	if (hapd->current_hw_info)
		hw_idx = hapd->current_hw_info->hw_idx;

	wpa_printf(MSG_INFO, "%s: disconnect backhaul event with radio id %d %s \n",
		   __func__, hw_idx, hapd->phy);
	uc_value_push(ucv_get(ucv_int64_new(hw_idx)));
	uc_value_push(ucv_get(val));
	ucv_put(wpa_ucode_call(3));
	ucv_gc(vm);
}

uc_value_t *uc_wpas_notify_uplink_csa_extn(uc_vm_t *vm, size_t nargs)
{
	struct wpa_supplicant *wpa_s = uc_fn_thisval("wpas.iface");
	uc_value_t *info = uc_fn_arg(0);
	u32 freq = 0, chan = 0, cs_count = 0;
	u64 intval;
	u32 new_ch_width = 0, ch_seg_0 = 0, ch_seg_1 = 0;
	/* 17 bytes: EID(1)+LEN(1)+OUI(3)+Type(1)+Count(1)+Entry(10) */
	u8 nol_ie_buf[DFS_NOL_IE_FIXED_HDR_LEN + DFS_NOL_IE_ENTRY_LEN];
	size_t nol_ie_len = 0;
	uc_value_t *nol_channel;
	int cac_abort = 0;

	if (!wpa_s || ucv_type(info) != UC_OBJECT)
		return NULL;

	if ((intval = ucv_int64_get(ucv_object_get(info, "csa_count", NULL))) && !errno)
		cs_count = intval;

	if ((intval = ucv_int64_get(ucv_object_get(info, "frequency", NULL))) && !errno)
		freq = intval;

	if ((intval = ucv_int64_get(ucv_object_get(info, "channel", NULL))) && !errno)
		chan = intval;

	if ((intval = ucv_int64_get(ucv_object_get(info, "new_ch_width", NULL))) && !errno)
		new_ch_width = intval;

	if ((intval = ucv_int64_get(ucv_object_get(info, "ch_seg_0", NULL))) && !errno)
		ch_seg_0 = intval;

	if ((intval = ucv_int64_get(ucv_object_get(info, "ch_seg_1", NULL))) && !errno)
		ch_seg_1 = intval;

	if ((intval = ucv_int64_get(ucv_object_get(info, "cac_abort", NULL))) && !errno)
		cac_abort = intval;

	wpa_printf(MSG_INFO,
		   "%s freq=%d chan=%d csa_count=%d new_ch_width=%u ch_seg_0=%u ch_seg_1=%u cac_abort=%d",
		   __func__, freq, chan, cs_count, new_ch_width, ch_seg_0,
		   ch_seg_1, cac_abort);

	if (!wpas_uplink_csa_link_available(wpa_s, freq)) {
		wpa_printf(MSG_WARNING,
			   "%s: BH link is not available for freq=%u, skipping uplink CSA",
			   __func__, freq);
		return ucv_boolean_new(0);
	}

	nol_channel = ucv_object_get(info, "nol_channel", NULL);
	if (nol_channel && ucv_type(nol_channel) == UC_OBJECT) {
		dfs_nol_ie_info nol_info;
		int64_t val;
		int encoded_len;

		os_memset(&nol_info, 0, sizeof(nol_info));

		val = ucv_int64_get(ucv_object_get(nol_channel, "freq", NULL));
		nol_info.freq = (u32)val;

		val = ucv_int64_get(ucv_object_get(nol_channel, "bandwidth", NULL));
		nol_info.bandwidth = (u32)val;

		val = ucv_int64_get(ucv_object_get(nol_channel, "bitmap", NULL));
		nol_info.subchan_bitmap = (u16)val;

		encoded_len = dfs_encode_nol_ie(&nol_info, nol_ie_buf,
						sizeof(nol_ie_buf));
		if (encoded_len > 0) {
			nol_ie_len = (size_t)encoded_len;
			wpa_printf(MSG_INFO,
				   "%s: Encoded NOL entry freq=%u bw=%u bitmap=0x%04x len=%zu",
				   __func__, nol_info.freq, nol_info.bandwidth,
				   nol_info.subchan_bitmap, nol_ie_len);
		} else {
			wpa_printf(MSG_WARNING,
				   "%s: Failed to encode NOL IE", __func__);
		}
	}

	wpa_drv_send_uplink_csa(wpa_s, freq, cs_count, ch_seg_0, ch_seg_1,
				new_ch_width,
				nol_ie_len > 0 ? nol_ie_buf : NULL,
				nol_ie_len, cac_abort);

	return ucv_boolean_new(1);
}

uc_value_t *uc_wpas_iface_reconnect_extn(uc_vm_t *vm, size_t nargs)
{
	struct wpa_supplicant *wpa_s = uc_fn_thisval("wpas.iface");

	if (!wpa_s)
		return NULL;
	wpa_printf(MSG_INFO, "%s disconnect and reconnect backhaul station", __func__);
	wpas_request_disconnection(wpa_s);
	wpas_request_connection(wpa_s);
	return ucv_boolean_new(1);
}

#else
void hostapd_ucode_notify_uplink_csa(struct hostapd_iface *hapd, int event, u8 channel,
				     int freq, int csa_count, u8 new_ch_width,
				     u8 ch_seg_0, u8 ch_seg_1,
				     const dfs_nol_ie_info *nol_info)
{
}

void hostapd_ucode_trigger_bhsta_disconnect(struct hostapd_iface *hapd)
{
}
#endif /* UCODE_SUPPORT */
