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
#include "utils/eloop.h"
#include "qcn_ie_extn.h"

/*
 * RCSA Vendor Specific Action frame:
 * category(1) + Atheros OUI(3) + CSA IE [+ optional QCA NOL IE].
 */
#define RCSA_VENDOR_ACTION_HDR_LEN 4
#define RCSA_CSA_IE_HDR_LEN 2
#define RCSA_MIN_FRAME_LEN \
	(RCSA_VENDOR_ACTION_HDR_LEN + RCSA_CSA_IE_HDR_LEN + \
	 IEEE80211_CSA_IE_MIN_LEN)

#define HOSTAPD_RCSA_TX_COUNT 5
#define HOSTAPD_RCSA_SWITCH_MODE 1
#define HAPD_DFS_WAIT_FOR_RCSA_FROM_ROOT_DUR_US(bcn_intval) (HOSTAPD_RCSA_TX_COUNT * (bcn_intval) * 2)
#define HOSTAPD_DFS_BH_DISCONNECT_WAIT_TIME_SEC 3
#define HOSTAPD_RCSA_INTVAL_US (100 * 1000)

/* RCSA config to be revisited once cswopt is introduced.
 * Hardcoding this to DISABLE for now
 */
static int dfs_is_rcsa_tx_enabled(struct hostapd_iface *iface)
{
	if (!iface || !iface->conf)
		return 0;

	return iface->conf->conf_extn.rcsa_tx;
}

bool hostapd_rcsa_tx_bh_enabled(struct hostapd_iface *iface)
{
	if (dfs_is_rcsa_tx_enabled(iface) &&
	    hostapd_is_backhaul_sta_configured(iface))
		return true;

	return false;
}


void hostapd_set_rcsa_inprogress(struct hostapd_iface *iface, bool value)
{
	iface->iface_extn.rcsa_ctx.rcsa_inprogress = value;
}


static bool hostapd_is_rcsa_inprogress(struct hostapd_iface *iface)
{
	if (iface->iface_extn.rcsa_ctx.rcsa_inprogress)
		return true;

	return false;
}

static bool optional_ml_info_ie_access(u8 *buf, size_t buf_len,
					s8 *link_id, bool set)
{
	u8 *pos = buf;
	size_t rem_len = buf_len;
	u16 link_id_bitmap;
	int i;

	while (pos && rem_len >= 2) {
		size_t ie_len = (size_t) pos[1] + 2;

		if (ie_len > rem_len)
			return false;

		if (hostapd_is_ml_info_ie(pos, ie_len)) {
			/*
			 * ML Info IE for RCSA:
			 *   Element ID   : Extension
			 *   Length       : 3
			 *   Extension ID : Multi-Link
			 *   Link bitmap  : 2-byte LE bitmap
			 */
			if (ie_len < 5)
				return false;

			if (!link_id)
				return true;

			if (set) {
				WPA_PUT_LE16(pos + 3, (BIT(*link_id)));
				return true;
			}
			link_id_bitmap = WPA_GET_LE16(pos + 3);
			for (i = 0; i < 16; i++) {
				if (link_id_bitmap & BIT(i)) {
					*link_id = i;
					return true;
				}
			}

			return true;
		}

		pos += ie_len;
		rem_len -= ie_len;
	}

	return false;
}

#ifdef UCODE_SUPPORT
static int wpa_drv_notify_rcsa(struct wpa_supplicant *wpa_s, int freq,
			       u32 chan, u8 cs_count, u32 switch_mode,
			       u8 *opt_ie, size_t opt_ie_len)
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

	if (opt_ie && opt_ie_len > 0)
		total_len += opt_ie_len;

	buf = wpabuf_alloc(total_len);
	if (!buf) {
		wpa_printf(MSG_DEBUG, "RCSA: Memory allocation failed");
		return -1;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_VENDOR_SPECIFIC);
	wpabuf_put_be24(buf, OUI_QCOM);
	wpabuf_put_u8(buf, WLAN_EID_CHANNEL_SWITCH);
	wpabuf_put_u8(buf, IEEE80211_CSA_IE_MIN_LEN);
	wpabuf_put_u8(buf, switch_mode);
	wpabuf_put_u8(buf, 36);
	wpabuf_put_u8(buf, cs_count);

	if (opt_ie && opt_ie_len > 0)
		wpabuf_put_data(buf, opt_ie, opt_ie_len);

	/*
	 * hapd and wpa link ids in a repeater may not map to same
	 * band. Derive the link_id to be used for tx of rcs in wpa side.
	 * consider case for splitphy as well.
	 *
	 * if ml_info ie is present, it means, tx should be link agnistic.
	 * So, no need to derive link_id and tx freq
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
	/*
	 * link bitmap in ml info ie is populated by repeater hostapd.
	 * This can't be mapped to root hostapd, so replace bitmap with
	 * wpa_supplicant link bitmap(linkids of repeater wpa and root
	 * hostapd are same).
	 */
	if (optional_ml_info_ie_access(opt_ie, opt_ie_len, &link_id, 1))
		link_id = -1;

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
					int freq, u8 switch_mode,
					const u8 *opt_ie, u8 opt_ie_len)
{
	uc_value_t *val = NULL;
	uc_value_t *iface_registry = NULL;
	struct uc_vm *vm = NULL;
	s8 hw_idx = 0;
	char *opt_ie_hex = NULL;
	struct hostapd_rcsa_ctx *rcsa_ctx;

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

	wpa_printf(MSG_INFO, "RCSA event with radio id %d %s\n",
		   hw_idx, iface->phy);
	uc_value_push(ucv_get(ucv_int64_new(hw_idx)));
	uc_value_push(ucv_get(val));
	uc_value_push(ucv_get(ucv_string_new(event_to_string(EVENT_DFS_RCSA_TX))));
	val = ucv_object_new(vm);
	uc_value_push(ucv_get(val));

	ucv_object_add(val, "channel", ucv_int64_new(channel));
	ucv_object_add(val, "frequency", ucv_int64_new(freq));
	ucv_object_add(val, "csa_count", ucv_int64_new(HOSTAPD_RCSA_TX_COUNT));
	ucv_object_add(val, "switch_mode", ucv_int64_new(switch_mode));

	if (opt_ie && opt_ie_len > 0) {
		opt_ie_hex = os_malloc((opt_ie_len * 2) + 1);
		if (!opt_ie_hex)
			return -1;

		wpa_snprintf_hex(opt_ie_hex, (opt_ie_len * 2) + 1,
				 opt_ie, opt_ie_len);
		ucv_object_add(val, "optional_ie_hex",
			       ucv_string_new(opt_ie_hex));
		os_free(opt_ie_hex);
	}

	ucv_put(wpa_ucode_call(5));
	ucv_gc(vm);

	hostapd_set_rcsa_inprogress(iface, true);
	if (!eloop_is_timeout_registered(hostapd_trigger_backhaul_sta_disconnect,
					 iface, NULL)) {
		eloop_register_timeout(1, HAPD_DFS_WAIT_FOR_RCSA_FROM_ROOT_DUR_US(100000),
				       hostapd_trigger_backhaul_sta_disconnect,
				       iface, NULL);
	}
	rcsa_ctx = &iface->iface_extn.rcsa_ctx;
	if (!rcsa_ctx->rcsa_tx_cnt &&
	    !eloop_is_timeout_registered(hostapd_trigger_rcsa_tx,
					 iface, NULL)) {
		rcsa_ctx->rcsa_tx_cnt = HOSTAPD_RCSA_TX_COUNT;
		eloop_register_timeout(0, HOSTAPD_RCSA_INTVAL_US,
				       hostapd_trigger_rcsa_tx,
				       iface, NULL);
	}
	rcsa_ctx->rcsa_tx_cnt--;

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
	uc_value_t *opt_ie_hex;
	const char *hex;
	u8 *opt_ie = NULL;
	size_t opt_ie_len = 0;

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

	opt_ie_hex = ucv_object_get(info, "optional_ie_hex", NULL);
	if (opt_ie_hex && ucv_type(opt_ie_hex) == UC_STRING) {
		hex = ucv_string_get(opt_ie_hex);
		if (hex && *hex) {
			opt_ie_len = os_strlen(hex) / 2;
			opt_ie = os_malloc(opt_ie_len);
			if (!opt_ie)
				return NULL;
			if (hexstr2bin(hex, opt_ie, opt_ie_len)) {
				os_free(opt_ie);
				return NULL;
			}
		}
	}

	wpa_drv_notify_rcsa(wpa_s, freq, chan, cs_count, switch_mode,
			    opt_ie, opt_ie_len);

	os_free(opt_ie);

	return ucv_boolean_new(1);
}
#else
static int hostapd_ucode_notify_rcsa_tx(struct hostapd_iface *iface, u8 channel,
					int freq, u8 switch_mode,
					const u8 *opt_ie, u8 opt_ie_len)
{
	return -1;
}
#endif

static void hostapd_get_local_rcsa_ml_info(struct hostapd_iface *iface,
					   bool *include_ml_ie,
					   u16 *link_id_bitmap)
{
	*include_ml_ie = false;
	*link_id_bitmap = 0;

	if (!iface || !iface->bss[0])
		return;

	if (iface->bss[0]->conf->mld_ap && iface->cac_started &&
	    iface->bss[0]->mld_link_id >= 0) {
		*include_ml_ie = true;
		*link_id_bitmap = BIT(iface->bss[0]->mld_link_id);
	}
}

static size_t hostapd_build_rcsa_optional_ies(const u8 *nol_ie,
					      size_t nol_ie_len,
					      bool include_ml_ie,
					      u16 link_id_bitmap,
					      u8 *buf, size_t buf_len)
{
	u8 *pos = buf;
	u8 *ml_pos;

	if (!buf || !buf_len)
		return 0;

	if (nol_ie && nol_ie_len > 0) {
		if (nol_ie_len > (size_t) (buf + buf_len - pos))
			return 0;

		os_memcpy(pos, nol_ie, nol_ie_len);
		wpa_printf(MSG_INFO, "rcsa:copied nol");
		pos += nol_ie_len;
	}

	if (!include_ml_ie)
		return pos - buf;

	ml_pos = add_ml_link_info_ie(pos, buf_len - (pos - buf),
				     link_id_bitmap);
	if (!ml_pos)
		return 0;

	return ml_pos - buf;
}

static int hostapd_build_nol_ie(struct hostapd_iface *iface,
					  int freq,
					  u8 current_vht_oper_chwidth,
					  u8 oper_centr_freq_seg0_idx,
					  u8 oper_centr_freq_seg1_idx,
					  u8 *buf, size_t buf_len)
{
	struct dfs_nol_ie_info nol_info;
	u8 *pos = buf;
	u8 *length_pos;

	if (!iface || !buf || buf_len < 4)
		return -1;

	if (!iface->radar_bit_pattern_extn ||
	    iface->radar_bit_pattern_extn == 0xffff ||
	    dfs_prepare_nol_ie_bitmap(iface, freq,
				      current_vht_oper_chwidth,
				      oper_centr_freq_seg0_idx,
				      oper_centr_freq_seg1_idx,
				      iface->radar_bit_pattern_extn,
				      &nol_info) != 0)
		return -1;

	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	length_pos = pos++;
	*pos++ = (u8) nol_info.bandwidth;
	WPA_PUT_LE16(pos, (u16) nol_info.freq);
	pos += 2;
	*pos++ = (u8) nol_info.subchan_bitmap;
	*length_pos = pos - length_pos - 1;

	return pos - buf;
}

static void hostapd_rcsa_store_optional_ie(struct hostapd_iface *iface,
					   const u8 *opt_ie,
					   size_t opt_ie_len)
{
	struct hostapd_rcsa_ctx *rcsa_ctx;

	rcsa_ctx = &iface->iface_extn.rcsa_ctx;
	rcsa_ctx->optional_ie_len = 0;

	if (!opt_ie || !opt_ie_len)
		return;

	if (opt_ie_len > sizeof(rcsa_ctx->optional_ie)) {
		wpa_printf(MSG_WARNING,
				"RCSA: optional IE too long (%zu > %zu)",
				opt_ie_len, sizeof(rcsa_ctx->optional_ie));
		return;
	}
	os_memcpy(rcsa_ctx->optional_ie, opt_ie, opt_ie_len);
	rcsa_ctx->optional_ie_len = opt_ie_len;
}

void hostapd_trigger_rcsa_tx(void *eloop_data, void *user_data)
{
	struct hostapd_iface *iface = eloop_data;
	struct hostapd_rcsa_ctx *rcsa_ctx;
	u8 chan;

	if (!iface)
		return;

	rcsa_ctx = &iface->iface_extn.rcsa_ctx;

	if (!hostapd_is_rcsa_inprogress(iface))
		return;

	if (hostapd_csa_in_progress(iface))
		return;

	if (!rcsa_ctx->rcsa_tx_cnt)
		return;

	ieee80211_freq_to_chan(iface->freq, &chan);
	hostapd_ucode_notify_rcsa_tx(iface, chan, iface->freq,
				     IEEE80211_CSA_IE_MODE_OFFSET,
				     rcsa_ctx->optional_ie_len ? rcsa_ctx->optional_ie : NULL,
				     rcsa_ctx->optional_ie_len);

	eloop_register_timeout(0, HOSTAPD_RCSA_INTVAL_US,
			       hostapd_trigger_rcsa_tx,
			       iface, NULL);
}

int hostapd_send_rcsa_extn(struct hostapd_iface *iface,
			   int channel, int freq,
			   int secondary_channel,
			   u8 current_vht_oper_chwidth,
			   u8 oper_centr_freq_seg0_idx,
			   u8 oper_centr_freq_seg1_idx,
			   u16 punct_bitmap)
{
	bool attach_nol_ie = false;
	bool attach_ml_ie = false;
	u16 link_id_bitmap = 0;
	u8 opt_ie[RCSA_MAX_OPTIONAL_IE_LEN];
	u8 nol_ie_buf[RCSA_MAX_OPTIONAL_IE_LEN];
	size_t opt_ie_len = 0;
	int nol_ie_len = 0;

	if (!iface)
		return -EINVAL;

	if (!hostapd_rcsa_tx_bh_enabled(iface))
		return -EINVAL;

	if (hostapd_csa_in_progress(iface)) {
		wpa_printf(MSG_INFO,
			   "RCSA: defer TX because channel switch is already in progress");
		return 0;
	}

	if (hostapd_is_rcsa_inprogress(iface)) {
		wpa_printf(MSG_INFO,
			   "RCSA: inprogress");
		return -EINVAL;
	}

	nol_ie_len = hostapd_build_nol_ie(iface, freq,
					  current_vht_oper_chwidth,
					  oper_centr_freq_seg0_idx,
					  oper_centr_freq_seg1_idx,
					  nol_ie_buf,
					  sizeof(nol_ie_buf));
	if (nol_ie_len > 0)
		attach_nol_ie = true;

	hostapd_get_local_rcsa_ml_info(iface, &attach_ml_ie, &link_id_bitmap);
	wpa_printf(MSG_INFO,"rcsa:attach ml %u",attach_ml_ie);

	opt_ie_len = hostapd_build_rcsa_optional_ies(
				attach_nol_ie ? nol_ie_buf : NULL,
				attach_nol_ie ? (size_t) nol_ie_len : 0,
				attach_ml_ie, link_id_bitmap,
				opt_ie, sizeof(opt_ie));

	hostapd_rcsa_store_optional_ie(iface, opt_ie, opt_ie_len);

	return hostapd_ucode_notify_rcsa_tx(iface, channel, freq,
					    HOSTAPD_RCSA_SWITCH_MODE,
					    opt_ie_len ? opt_ie : NULL,
					    opt_ie_len);
}

static int hostapd_parse_rcsa_frame(struct hostapd_data *hapd,
				    const u8 *buf, size_t len,
				    u8 *switch_mode, u8 *new_chan,
				    u8 *csa_count, const u8 *opt_ie,
				    size_t *opt_ie_len,
				    bool *mlinfo_present,
				    s8 *mlinfo_linkid)
{
	const u8 *pos;
	const u8 *end;
	const u8 *cs_ie;
	size_t rem_len;
	size_t copy_len;

	if (len < IEEE80211_HDRLEN + RCSA_VENDOR_ACTION_HDR_LEN) {
		wpa_printf(MSG_ERROR, "rcsa: frame too short");
		return -EINVAL;
	}

	end = buf + len;
	pos = buf + IEEE80211_HDRLEN + RCSA_VENDOR_ACTION_HDR_LEN;
	rem_len = end - pos;

	wpa_printf(MSG_DEBUG, "rcsa: received RCSA");
	cs_ie = get_ie(pos, rem_len, WLAN_EID_CHANNEL_SWITCH);
	if (!cs_ie || cs_ie[1] < IEEE80211_CSA_IE_MIN_LEN) {
		wpa_printf(MSG_ERROR, "rcsa: no valid CSA IE found");
		return -EINVAL;
	}

	*new_chan = cs_ie[IEEE80211_CSA_IE_NEW_CHANNEL_OFFSET];
	*csa_count = cs_ie[IEEE80211_CSA_IE_COUNT_OFFSET];
	*switch_mode = cs_ie[IEEE80211_CSA_IE_MODE_OFFSET];

	pos = cs_ie + IEEE80211_CSA_IE_TOTAL_LEN;
	rem_len = end - pos;

	if (rem_len >= 2 && opt_ie) {
		copy_len = rem_len < RCSA_MAX_OPTIONAL_IE_LEN ? rem_len : RCSA_MAX_OPTIONAL_IE_LEN;

		os_memcpy((u8 *)opt_ie, pos, copy_len);
		*opt_ie_len = copy_len;
	} else
		*opt_ie_len = 0;


	if (rem_len >= 2) {
		opt_ie = pos;
		*opt_ie_len = rem_len;
	}

	*mlinfo_present = optional_ml_info_ie_access((u8 *)opt_ie, *opt_ie_len,
						     mlinfo_linkid, 0);

	wpa_printf(MSG_DEBUG,
		   "rcsa: chan %u, csa_cnt %u, switch_mode %u, chan %u, mlinfo_present %u, linkid %u",
		   *new_chan, *csa_count, *switch_mode, *new_chan,
		   *mlinfo_present, *mlinfo_linkid);

	return 0;
}

bool hostapd_rcsa_rx_hdl(struct hostapd_data *hapd,
			 const u8 *buf, size_t len)
{
	u8 opt_ie[RCSA_MAX_OPTIONAL_IE_LEN] = {0};
	u8 rebuilt_opt_ie[RCSA_MAX_OPTIONAL_IE_LEN] = {0};
	struct hostapd_iface *iface;
	struct hostapd_data *target_hapd;
	u8 switch_mode = 0;
	u8 new_chan = 0;
	u8 csa_count = 0;
	int ret = -1;
	s8 mlinfo_linkid = -1;
	size_t opt_ie_len = 0;
	bool mlinfo_present = false;
	size_t rebuilt_opt_ie_len = 0;
	bool include_ml_ie = false;
	u16 link_id_bitmap = 0;
	iface = hapd->iface;

	wpa_printf(MSG_INFO, "RCSA: received RCSA from repeater");

	if (hostapd_parse_rcsa_frame(hapd, buf, len,
				     &switch_mode, &new_chan,
				     &csa_count,
				     opt_ie, &opt_ie_len,
				     &mlinfo_present, &mlinfo_linkid))
		return 0;

	target_hapd = hapd;
	if (iface->bss[0]->conf->mld_ap &&
	    (mlinfo_linkid != -1)) {
		target_hapd = switch_link_hapd(hapd, mlinfo_linkid);
		if (!target_hapd)
			return 0;
	}

	iface = target_hapd->iface;
	if (!iface)
		return 0;

	if (!iface->conf->conf_extn.process_rcsa)
		return 1;

	if (hostapd_csa_in_progress(iface)) {
		wpa_printf(MSG_INFO,
			   "RCSA: defer forwarding because channel switch is already in progress");
		return 0;
	}

	if (hostapd_is_rcsa_inprogress(iface)) {
		wpa_printf(MSG_INFO, "RCSA: inprogress");
		return 1;
	}

	wpa_printf(MSG_INFO, "RCSA: proceeding with Tx");

	/* strip off ml-info iE */
	if (mlinfo_present &&  (opt_ie_len >= 5))
		opt_ie_len -= 5;

	hostapd_get_local_rcsa_ml_info(iface, &include_ml_ie, &link_id_bitmap);
	rebuilt_opt_ie_len = hostapd_build_rcsa_optional_ies(opt_ie_len ? opt_ie : NULL,
							     opt_ie_len,
							     include_ml_ie, link_id_bitmap,
							     rebuilt_opt_ie,
							     sizeof(rebuilt_opt_ie));

	hostapd_rcsa_store_optional_ie(iface, rebuilt_opt_ie, rebuilt_opt_ie_len);

	ret = hostapd_ucode_notify_rcsa_tx(iface, new_chan, iface->freq,
					   switch_mode,
					   rebuilt_opt_ie_len ? rebuilt_opt_ie : NULL,
					   rebuilt_opt_ie_len);

	return ret;
}

void hostapd_rcsa_trigger_channal_change(void *eloop_data, void *user_data)
{
	struct hostapd_iface *iface = eloop_data;
	struct hostapd_rcsa_ctx *rcsa_ctx;

	if (!iface)
		return;

	rcsa_ctx = &iface->iface_extn.rcsa_ctx;

	if (!hostapd_is_rcsa_inprogress(iface))
		return;


	if ((rcsa_ctx->bh_discon_wait_cnt <= 0) ||
	    (hostapd_csa_in_progress(iface))) {
		hostapd_set_rcsa_inprogress(iface, false);
		return;
	}

	rcsa_ctx->bh_discon_wait_cnt--;
	if (hostapd_is_backhaul_sta_configured(iface)) {
		eloop_register_timeout(HOSTAPD_DFS_BH_DISCONNECT_WAIT_TIME_SEC, 0,
				       hostapd_rcsa_trigger_channal_change,
				       iface, NULL);
		return;
	}
	wpa_printf(MSG_INFO,"RCSA: CSA timeout: trigger channel switch");
	hostapd_dfs_start_channel_switch(iface);
	hostapd_set_rcsa_inprogress(iface, false);
}

void hostapd_rcsa_handle_csa_timeout(struct hostapd_iface *iface)
{
	struct hostapd_rcsa_ctx *rcsa_ctx = &iface->iface_extn.rcsa_ctx;

	eloop_cancel_timeout(hostapd_trigger_rcsa_tx, iface, NULL);

	if(!hostapd_is_rcsa_inprogress(iface))
		return;

	if (!iface->conf->conf_extn.ind_rptr) {
		hostapd_set_rcsa_inprogress(iface, false);
		return;
	}

	rcsa_ctx->bh_discon_wait_cnt = 3;
	/* triggering CSA without waiting for BH disconnect would result
	 * in different chan ctx (BH's old chan ctx and new chan ctx) leading
	 * to csa failure in driver
	 */
	if (!eloop_is_timeout_registered(hostapd_rcsa_trigger_channal_change,
					 iface, NULL)) {
		eloop_register_timeout(HOSTAPD_DFS_BH_DISCONNECT_WAIT_TIME_SEC, 0,
				       hostapd_rcsa_trigger_channal_change,
				       iface, NULL);
	}
}
