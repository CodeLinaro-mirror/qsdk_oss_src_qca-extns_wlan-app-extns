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
#include "wpa_supplicant_extn.h"
#include "cmn.h"

#ifdef UCODE_SUPPORT
void hostapd_ucode_notify_uplink_csa(struct hostapd_iface *hapd, int event, u8 channel,
				     int freq, int csa_count, u8 new_ch_width,
				     u8 ch_seg_0, u8 ch_seg_1,
				     dfs_nol_ie_list *nol_list)
{
	uc_value_t *val = NULL;
	uc_value_t *nol_array = NULL;
	uc_value_t *iface_registry = NULL;
	struct uc_vm *vm = NULL;
	s8 hw_idx = 0;
	size_t i;

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

	wpa_printf(MSG_INFO, "%s: Channel switch event with radio id %d %s \n",
		   __func__, hw_idx, hapd->phy);
	uc_value_push(ucv_get(ucv_int64_new(hw_idx)));

	uc_value_push(ucv_get(val));
	uc_value_push(ucv_get(ucv_string_new(event_to_string(event))));
	val = ucv_object_new(vm);
	uc_value_push(ucv_get(val));

	if (event == EVENT_DFS_UPLINK_CHANNEL_SELECTED) {
		ucv_object_add(val, "frequency", ucv_int64_new(freq));
		ucv_object_add(val, "channel", ucv_int64_new(channel));
		ucv_object_add(val, "csa_count", ucv_int64_new(csa_count));
		ucv_object_add(val, "new_ch_width", ucv_int64_new(new_ch_width));
		ucv_object_add(val, "ch_seg_0", ucv_int64_new(ch_seg_0));
		ucv_object_add(val, "ch_seg_1", ucv_int64_new(ch_seg_1));
		ucv_object_add(val, "cac_abort",
			       ucv_int64_new(hapd->iface_extn.cac_abort ? 1 : 0));

		if (nol_list && nol_list->count > 0) {
			nol_array = ucv_array_new(vm);
			for (i = 0; i < nol_list->count; i++) {
				uc_value_t *nol_entry = ucv_object_new(vm);
				dfs_nol_ie_info *entry = &nol_list->entries[i];

				ucv_object_add(nol_entry, "freq",
					       ucv_int64_new(entry->freq));
				ucv_object_add(nol_entry, "bandwidth",
					       ucv_int64_new(entry->bandwidth));
				ucv_object_add(nol_entry, "bitmap",
					       ucv_int64_new(entry->subchan_bitmap));

				ucv_array_push(nol_array, nol_entry);
			}
			ucv_object_add(val, "nol_channels", nol_array);
			wpa_printf(MSG_INFO, "%s: Added %zu NOL entries to event\n",
				   __func__, nol_list->count);
		}
	}

	if (nol_list && nol_list->count > 0) {
		wpa_printf(MSG_INFO, "%s: val object should contain nol_channels array\n",
			   __func__);
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
	u8 nol_ie_buf[256];
	size_t nol_ie_len = 0;
	uc_value_t *nol_channels;
	int cac_abort = 0;

	if (!wpa_s || ucv_type(info) != UC_OBJECT)
		return NULL;

	nol_channels = ucv_object_get(info, "nol_channels", NULL);

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
		   "%s freq=%d chan=%d csa_count=%d new_ch_width=%u ch_seg_0=%u ch_seg_1=%u cac_abort=%d\n",
		   __func__, freq, chan, cs_count, new_ch_width, ch_seg_0, ch_seg_1, cac_abort);

	if (nol_channels && ucv_type(nol_channels) == UC_ARRAY) {
		dfs_nol_ie_list nol_list;
		int encoded_len = 0;
		size_t nol_count = ucv_array_length(nol_channels);
		size_t i;

		if (nol_count > 0 && nol_count <= 8) {
			nol_list.entries = calloc(nol_count,
						  sizeof(dfs_nol_ie_info));
			if (nol_list.entries) {
				nol_list.count = nol_count;

				for (i = 0; i < nol_count; i++) {
					uc_value_t *entry = ucv_array_get(nol_channels, i);

					if (entry && ucv_type(entry) == UC_OBJECT) {
						int64_t val;

						val = ucv_int64_get(ucv_object_get(entry, "freq", NULL));
						nol_list.entries[i].freq = val;

						val = ucv_int64_get(ucv_object_get(entry, "bandwidth", NULL));
						nol_list.entries[i].bandwidth = val;

						val = ucv_int64_get(ucv_object_get(entry, "bitmap", NULL));
						nol_list.entries[i].subchan_bitmap = val;
					}
				}

				encoded_len = dfs_encode_nol_ie(&nol_list,
								nol_ie_buf,
								sizeof(nol_ie_buf));
				if (encoded_len > 0) {
					nol_ie_len = encoded_len;
					wpa_printf(MSG_INFO,
						   "%s: Encoded %zu NOL entries, len=%zu\n",
						   __func__, nol_count, nol_ie_len);
				}

				free(nol_list.entries);
			}
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
				     dfs_nol_ie_list *nol_list)
{
}

void hostapd_ucode_trigger_bhsta_disconnect(struct hostapd_iface *hapd)
{
}
#endif /* UCODE_SUPPORT */
