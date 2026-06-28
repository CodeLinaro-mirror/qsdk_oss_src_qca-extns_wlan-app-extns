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
#include <ap/ap_drv_ops.h>
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
#define RCSA_NOL_IE_INFO_LEN 4
#define RCSA_NOL_IE_TOTAL_LEN (RCSA_CSA_IE_HDR_LEN + RCSA_NOL_IE_INFO_LEN)
#define RCSA_MIN_FRAME_LEN \
	(RCSA_VENDOR_ACTION_HDR_LEN + RCSA_CSA_IE_HDR_LEN + \
	 IEEE80211_CSA_IE_MIN_LEN)
#define RCSA_MIN_DFS_SUBCHAN_BW 20
#define RCSA_MAX_20M_SUB_CH 8

#define HOSTAPD_RCSA_TX_COUNT 5
#define HOSTAPD_RCSA_SWITCH_MODE 1
#define HAPD_DFS_WAIT_FOR_RCSA_FROM_ROOT_DUR_US(bcn_intval) (HOSTAPD_RCSA_TX_COUNT * (bcn_intval) * 2)
#define HOSTAPD_DFS_BH_DISCONNECT_WAIT_TIME_US 1000
#define HOSTAPD_RCSA_INTVAL_US (100 * 1000)

bool hostapd_rcsa_tx_bh_enabled(struct hostapd_iface *iface)
{
	if (IS_CSH_RCSA_TO_UPLINK_ENABLED(iface->conf->conf_extn.cswopts) &&
	    hostapd_is_backhaul_sta_configured(iface))
		return true;

	return false;
}

static bool hostapd_rcsa_rx_bh_enabled(struct hostapd_iface *iface)
{
       if (IS_CSH_PROCESS_RCSA_ENABLED(iface->conf->conf_extn.cswopts) &&
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

	if (wpa_s->wpa_state != WPA_COMPLETED && !wpa_s->sta_dfs_en)
		return -1;

	wpa_printf(MSG_DEBUG, "rcsa: freq %u chan %u cs_count %u",
		   freq, chan, cs_count);

	if (opt_ie && opt_ie_len > 0)
		total_len += opt_ie_len;

	buf = wpabuf_alloc(total_len);
	if (!buf) {
		wpa_printf(MSG_DEBUG, "rcsa: Memory allocation failed");
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

	if (!wpa_s->sta_dfs_en) {
		/*
		 * link bitmap in ml info ie is populated by repeater hostapd.
		 * This can't be mapped to root hostapd, so replace bitmap with
		 * wpa_supplicant link bitmap(linkids of repeater wpa and root
		 * hostapd are same).
		 */
		if (optional_ml_info_ie_access(opt_ie, opt_ie_len, &link_id, 1))
			link_id = -1;
	} else {
		tx_freq = freq;
	}
	wpa_printf(MSG_DEBUG, "rcsa: sending on %d link freq %u",
		   link_id, tx_freq);
	wpa_hexdump_buf(MSG_INFO, "rcsa: Action frame payload", buf);
	res = wpa_drv_send_action_extn(wpa_s, tx_freq, 0,
				       wpa_s->bssid,
				       wpa_s->own_addr, wpa_s->bssid,
				       wpabuf_head(buf), wpabuf_len(buf),
				       0, link_id);
	if (res < 0)
		wpa_printf(MSG_ERROR, "rcsa: Failed to send action frame");
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

	wpa_printf(MSG_DEBUG, "rcsa: TX notify: freq=%d channel=%d",
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

	wpa_printf(MSG_DEBUG, "rcsa: event with radio id %d %s\n",
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

	if (!hostapd_is_rcsa_inprogress(iface) &&
	    !eloop_is_timeout_registered(hostapd_trigger_backhaul_sta_disconnect,
					 iface, NULL)) {
		eloop_register_timeout(1, HAPD_DFS_WAIT_FOR_RCSA_FROM_ROOT_DUR_US(100000),
				       hostapd_trigger_backhaul_sta_disconnect,
				       iface, NULL);
	}
	rcsa_ctx = &iface->iface_extn.rcsa_ctx;
	if (!hostapd_is_rcsa_inprogress(iface) &&
	    !eloop_is_timeout_registered(hostapd_trigger_rcsa_tx,
					 iface, NULL)) {
		rcsa_ctx->rcsa_tx_cnt = HOSTAPD_RCSA_TX_COUNT - 1;
		eloop_register_timeout(0, HOSTAPD_RCSA_INTVAL_US,
				       hostapd_trigger_rcsa_tx,
				       iface, NULL);
	}

	hostapd_set_rcsa_inprogress(iface, true);
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

size_t hostapd_build_rcsa_optional_ies(const u8 *nol_ie,
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
		wpa_printf(MSG_DEBUG, "rcsa:copied nol");
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
				u8 *buf, size_t buf_len)
{
	dfs_nol_ie_info *nol_info;
	u8 *pos = buf;
	u8 *length_pos;

	if (!iface->iface_extn.nol_info_valid)
		return -1;

	nol_info = &iface->iface_extn.nol_info;

	wpa_printf(MSG_DEBUG,
		   "rcsa: nol info : bw %u freq %u nol %hx",
		   nol_info->bandwidth, nol_info->freq,
		   nol_info->subchan_bitmap);

	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	length_pos = pos++;
	*pos++ = (u8) nol_info->bandwidth;
	WPA_PUT_LE16(pos, (u16) nol_info->freq);
	pos += 2;
	*pos++ = (u8) nol_info->subchan_bitmap;
	*length_pos = pos - length_pos - 1;

	iface->iface_extn.nol_info_valid = false;

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
				"rcsa: optional IE too long (%zu > %zu)",
				opt_ie_len, sizeof(rcsa_ctx->optional_ie));
		return;
	}
	os_memcpy(rcsa_ctx->optional_ie, opt_ie, opt_ie_len);
	rcsa_ctx->optional_ie_len = opt_ie_len;
}

/**
 * hostapd_validate_rcsa_nol_info - Validate parsed RCSA NOL IE data
 * @iface: hostapd interface receiving the RCSA frame
 * @rcsa_nol: NOL information parsed from the RCSA optional IE
 * @nol_info: output NOL information cleared before bitmap extraction
 *
 * Validates the context and mandatory NOL fields before bitmap
 * extraction. Clears @nol_info to avoid stale data on failure.
 *
 * Return: 0 when the parsed NOL data can be processed, -1 otherwise.
 */
static int hostapd_validate_rcsa_nol_info(struct hostapd_iface *iface,
					  const dfs_nol_ie_info *rcsa_nol,
					  dfs_nol_ie_info *nol_info)
{
	if (!iface || !iface->conf || !rcsa_nol || !nol_info) {
		wpa_printf(MSG_ERROR, "RCSA: invalid NOL reverse context");
		return -1;
	}

	os_memset(nol_info, 0, sizeof(*nol_info));
	if (!rcsa_nol->freq || rcsa_nol->bandwidth != RCSA_MIN_DFS_SUBCHAN_BW ||
	    !rcsa_nol->subchan_bitmap) {
		wpa_printf(MSG_ERROR,
			   "RCSA: invalid NOL freq=%u bw=%u bitmap=0x%04x",
			   rcsa_nol->freq, rcsa_nol->bandwidth,
			   rcsa_nol->subchan_bitmap);
		return -1;
	}

	return 0;
}

/**
 * hostapd_get_rcsa_oper_bw - Get operating bandwidth for RCSA NOL bitmap
 * @iface: hostapd interface with current operating channel information
 * @bandwidth_mhz: output operating bandwidth in MHz
 *
 * Return: 0 on success, -1 otherwise.
 */
static int hostapd_get_rcsa_oper_bw(struct hostapd_iface *iface,
				    int *bandwidth_mhz)
{
	int oper_chwidth;

	oper_chwidth = hostapd_get_oper_chwidth(iface->conf);
	if (dfs_nol_ie_chan_width_to_bw_mhz(oper_chwidth, iface->freq,
					    iface->freq, bandwidth_mhz)) {
		wpa_printf(MSG_ERROR, "RCSA: unsupported oper chwidth=%d",
			   oper_chwidth);
		return -1;
	}

	return 0;
}

/**
 * hostapd_get_rcsa_start_subchan - Find NOL start subchannel
 * @rcsa_nol: NOL information parsed from the RCSA frame
 * @base_freq: primary operating frequency in MHz
 * @n_subchans: number of 20 MHz subchannels in operating bandwidth
 * @start_subchan_idx: output start index in operation-channel bitmap
 *
 * Calculates where the received NOL frequency starts in the operating channel
 * bitmap so the RCSA NOL bitmap can be mapped to local channel state.
 *
 * Return: 0 on success, -1 otherwise.
 */
static int hostapd_get_rcsa_start_subchan(const dfs_nol_ie_info *rcsa_nol,
					  int base_freq, int n_subchans,
					  int *start_subchan_idx)
{
	if (!base_freq || rcsa_nol->freq < (u32) base_freq ||
	    ((int) rcsa_nol->freq - base_freq) % RCSA_MIN_DFS_SUBCHAN_BW) {
		wpa_printf(MSG_ERROR,
			   "RCSA: invalid NOL freq=%u base=%d",
			   rcsa_nol->freq, base_freq);
		return -1;
	}

	*start_subchan_idx = ((int) rcsa_nol->freq - base_freq) /
		RCSA_MIN_DFS_SUBCHAN_BW;
	if (*start_subchan_idx < 0 || *start_subchan_idx >= n_subchans) {
		wpa_printf(MSG_ERROR,
			   "RCSA: invalid NOL start=%d count=%d",
			   *start_subchan_idx, n_subchans);
		return -1;
	}

	return 0;
}

/**
 * hostapd_rcsa_idenitfy_nol_bitmap - Identify operating NOL bitmap
 * @rcsa_bitmap: bitmap parsed from the RCSA NOL IE
 * @rcsa_freq: first NOL subchannel frequency in MHz
 * @start_subchan_idx: start index in operation-channel bitmap
 * @n_subchans: number of 20 MHz subchannels in operating bandwidth
 *
 * Maps the RCSA NOL bitmap to the local operating-channel bitmap.
 *
 * Return: operation-channel NOL bitmap.
 */
static u16 hostapd_rcsa_idenitfy_nol_bitmap(u16 rcsa_bitmap,
					    u32 rcsa_freq,
					    int start_subchan_idx,
					    int n_subchans)
{
	u16 oper_bitmap = 0;
	int bit;

	for (bit = 0; bit < RCSA_MAX_20M_SUB_CH; bit++) {
		int oper_bit;

		if (!(rcsa_bitmap & BIT(bit)))
			continue;

		oper_bit = start_subchan_idx + bit;
		if (oper_bit >= n_subchans) {
			wpa_printf(MSG_WARNING,
				   "RCSA: skip NOL bit=%d rcsa_freq=%u oper_bit=%d",
				   bit, rcsa_freq + bit * RCSA_MIN_DFS_SUBCHAN_BW,
				   oper_bit);
			continue;
		}

		oper_bitmap |= BIT(oper_bit);
	}

	return oper_bitmap;
}

/**
 * hostapd_extract_rcsa_nol_ie_bitmap - Extract NOL IE bitmap from RCSA
 * @iface: hostapd interface receiving the RCSA frame
 * @rcsa_nol: NOL IE information received from the RCSA message
 * @nol_info: output NOL information updated with extracted NOL IE data
 *
 * Extracts the received RCSA NOL IE, identifies the operating NOL bitmap,
 * and fills @nol_info with the extracted NOL IE information.
 *
 * Return: 0 on success, -1 otherwise.
 */
static int hostapd_extract_rcsa_nol_ie_bitmap(struct hostapd_iface *iface,
					      const dfs_nol_ie_info *rcsa_nol,
					      dfs_nol_ie_info *nol_info)
{
	u16 parsed_bitmap;
	u16 nol_ie_bitmap;
	int base_freq;
	int bandwidth_mhz;
	int n_subchans;
	int start_subchan_idx;

	if (hostapd_validate_rcsa_nol_info(iface, rcsa_nol, nol_info))
		return -1;

	parsed_bitmap = rcsa_nol->subchan_bitmap;
	if (hostapd_get_rcsa_oper_bw(iface, &bandwidth_mhz))
		return -1;

	n_subchans = bandwidth_mhz / RCSA_MIN_DFS_SUBCHAN_BW;
	if (n_subchans <= 0 || n_subchans > RCSA_MAX_20M_SUB_CH) {
		wpa_printf(MSG_ERROR,
			   "RCSA: invalid subchannel count=%d bw=%d",
			   n_subchans, bandwidth_mhz);
		return -1;
	}

	base_freq = iface->freq;
	if (hostapd_get_rcsa_start_subchan(rcsa_nol, base_freq, n_subchans,
					   &start_subchan_idx))
		return -1;

	nol_ie_bitmap = hostapd_rcsa_idenitfy_nol_bitmap(parsed_bitmap,
							 rcsa_nol->freq,
							 start_subchan_idx,
							 n_subchans);

	if (!nol_ie_bitmap) {
		wpa_printf(MSG_ERROR,
			   "RCSA: empty NOL bitmap freq=%u base=%d bitmap=0x%04x",
			   rcsa_nol->freq, base_freq, parsed_bitmap);
		return -1;
	}

	nol_info->freq = rcsa_nol->freq - (start_subchan_idx * RCSA_MIN_DFS_SUBCHAN_BW);
	nol_info->bandwidth = bandwidth_mhz;
	nol_info->subchan_bitmap = nol_ie_bitmap;
	wpa_printf(MSG_DEBUG,
		   "RCSA: Parsed NOLIE freq: %d NOL bitmap =0x%04x Identified NOL bitmap =0x%04x NOL bw=%u",
		   nol_info->freq, parsed_bitmap, nol_info->subchan_bitmap, nol_info->bandwidth);

	return 0;
}

/**
 * hostapd_rcsa_get_radar_freq_params - Build radar frequency parameters
 * @hapd: hostapd BSS data
 * @nol_info: decoded RCSA NOL information
 * @freq_params: output frequency parameters for radar notification
 *
 * Derives the current operating channel data, center channel segment, and
 * channel width from the RCSA NOL information and fills @freq_params for
 * hostapd_drv_notify_radar().
 *
 * Return: 0 on success, -1 on failure.
 */
static int hostapd_rcsa_get_radar_freq_params(struct hostapd_data *hapd,
					      const dfs_nol_ie_info *nol_info,
					      struct hostapd_freq_params *freq_params)
{
	struct hostapd_iface *iface = hapd->iface;
	struct hostapd_hw_modes *mode = iface->current_mode;
	struct hostapd_channel_data *chan_data;
	u8 channel;
	u8 oper_chwidth;
	u8 center_seg0_idx;
	int ret;

	freq_params->mode = ieee80211_freq_to_chan(iface->freq, &channel);
	if (freq_params->mode == NUM_HOSTAPD_MODES) {
		wpa_printf(MSG_ERROR, "RCSA: invalid radar freq=%d",
			   iface->freq);
		return -1;
	}

	oper_chwidth = uc_hostapd_bandwidth_to_oper_chwidth_extn(nol_info->bandwidth);
	if (!mode) {
		wpa_printf(MSG_ERROR,
			   "RCSA: current hw mode unavailable freq=%d bw=%u",
			   iface->freq, nol_info->bandwidth);
		return -1;
	}

	chan_data = hw_mode_get_channel(mode, iface->freq, NULL);
	if (!chan_data) {
		wpa_printf(MSG_ERROR,
			   "RCSA: failed to find channel data freq=%d bw=%u",
			   iface->freq, nol_info->bandwidth);
		return -1;
	}

	center_seg0_idx = hostapd_get_center_chan_extn(iface, chan_data,
						       oper_chwidth);
	ret = hostapd_set_freq_params(freq_params, iface->conf->hw_mode,
				      iface->freq, channel,
				      iface->conf->enable_edmg,
				      iface->conf->edmg_channel,
				      iface->conf->ieee80211n,
				      iface->conf->ieee80211ac,
				      iface->conf->ieee80211ax,
				      iface->conf->ieee80211be,
				      iface->conf->ieee80211bn,
				      iface->conf->secondary_channel,
				      oper_chwidth,
				      center_seg0_idx,
				      0,
				      iface->conf->vht_capab,
				      &mode->he_capab[IEEE80211_MODE_AP],
				      &mode->eht_capab[IEEE80211_MODE_AP],
				      &mode->uhr_capab[IEEE80211_MODE_AP],
				      0,
				      hapd->iconf->he_6ghz_reg_pwr_type,
				      0, 0,
				      iface->conf->bandwidth_device,
				      iface->conf->center_freq_device);
	if (ret) {
		wpa_printf(MSG_ERROR,
			   "RCSA: failed to build freq params ret=%d freq=%d bw=%u",
			   ret, iface->freq, nol_info->bandwidth);
		return -1;
	}

#ifdef CONFIG_IEEE80211BE
	freq_params->link_id = hapd->mld_link_id;
#endif /* CONFIG_IEEE80211BE */

	return 0;
}

/**
 * hostapd_rcsa_notify_radar - Propagate radar event to driver based on
 *                             RCSA-derived NOL information
 * @hapd: hostapd BSS data
 *
 * Builds frequency parameters from the decoded RCSA NOL IE, notifies the
 * driver through hostapd_drv_notify_radar(), and then runs hostapd DFS radar
 * handling for the same NOL subchannel bitmap.
 */
static void hostapd_rcsa_notify_radar(struct hostapd_data *hapd)
{
	struct hostapd_freq_params freq_params;
	struct hostapd_iface *iface;
	dfs_nol_ie_info *nol_info;
	int ret;

	if (!hapd || !hapd->iface || !hapd->iface->conf || !hapd->iconf) {
		wpa_printf(MSG_ERROR, "RCSA: invalid radar notify context");
		return;
	}

	iface = hapd->iface;
	nol_info = &iface->iface_extn.nol_info;

	if (!nol_info->freq || !nol_info->bandwidth ||
	    !nol_info->subchan_bitmap) {
		wpa_printf(MSG_ERROR,
			   "RCSA: invalid NOL info freq=%u bw=%u bitmap=0x%04x",
			   nol_info->freq, nol_info->bandwidth,
			   nol_info->subchan_bitmap);
		return;
	}


	if (hapd->iface->conf->use_ru_puncture_dfs &&
	    nol_info->bandwidth < DFS_NOL_IE_BW_80_MHZ) {
		wpa_printf(MSG_DEBUG,
			   "RCSA: Puncturing is not applicable for bandwidth less than 80 MHz");
		return;
	}

	if (hostapd_rcsa_get_radar_freq_params(hapd, nol_info, &freq_params)) {
		wpa_printf(MSG_WARNING,
			   "RCSA: failed to get radar frequency params freq=%u bw=%u bitmap=0x%04x",
			   nol_info->freq, nol_info->bandwidth,
			   nol_info->subchan_bitmap);
		return;
	}

	ret = hostapd_drv_notify_radar(hapd, &freq_params,
				       nol_info->subchan_bitmap);
	if (ret < 0)
		wpa_printf(MSG_WARNING,
			   "RCSA: notify failed ret=%d freq=%d cf1=%d bw=%u",
			   ret, freq_params.freq, freq_params.center_freq1,
			   nol_info->bandwidth);
}

/**
 * hostapd_parse_rcsa_nol_ie - Parse RCSA NOL IE from optional IEs
 * @iface: hostapd interface to update
 * @ies: optional IE buffer following the CSA IE
 * @ies_len: length of optional IE buffer
 *
 * Searches the received RCSA optional IEs for the NOL IE generated by
 * hostapd_build_nol_ie() by repeater. The NOL IE values which were prepared by
 * dfs_prepare_nol_ie_bitmap() from repeater is converted back to the
 * operation-channel bitmap by root AP.
 *
 * On success, updates iface->iface_extn.nol_info and sets
 * iface->iface_extn.nol_info_valid to true. Existing NOL state is cleared
 * before parsing to avoid using stale data.
 *
 * Return: true when a valid NOL IE is parsed, false otherwise.
 */
static bool hostapd_parse_rcsa_nol_ie(struct hostapd_iface *iface,
				      const u8 *ies, size_t ies_len)
{
	const u8 *pos = ies;
	size_t rem_len = ies_len;
	dfs_nol_ie_info parsed_nol_info;
	dfs_nol_ie_info nol_info;

	if (!iface)
		return false;

	iface->iface_extn.nol_info_valid = false;
	os_memset(&iface->iface_extn.nol_info, 0,
		  sizeof(iface->iface_extn.nol_info));
	if (!ies || !ies_len)
		return false;

	while (rem_len >= 2) {
		size_t ie_len = (size_t) pos[1] + 2;

		if (ie_len > rem_len)
			return false;

		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC &&
		    ie_len == RCSA_NOL_IE_TOTAL_LEN) {
			wpa_hexdump(MSG_INFO, "RCSA: NOL IE parse raw", pos, ie_len);
			os_memset(&parsed_nol_info, 0, sizeof(parsed_nol_info));
			parsed_nol_info.bandwidth = pos[2];
			parsed_nol_info.freq = WPA_GET_LE16(pos + 3);
			parsed_nol_info.subchan_bitmap = pos[5];

			if (hostapd_extract_rcsa_nol_ie_bitmap(iface,
							       &parsed_nol_info,
							       &nol_info))
				return false;

			os_memcpy(&iface->iface_extn.nol_info, &nol_info,
				  sizeof(iface->iface_extn.nol_info));
			iface->iface_extn.nol_info_valid = true;

			return true;
		}

		pos += ie_len;
		rem_len -= ie_len;
	}

	return false;
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

	if (rcsa_ctx->rcsa_tx_cnt <= 0)
		return;

	rcsa_ctx->rcsa_tx_cnt--;
	ieee80211_freq_to_chan(iface->freq, &chan);
	hostapd_ucode_notify_rcsa_tx(iface, chan, iface->freq,
				     IEEE80211_CSA_IE_MODE_OFFSET,
				     rcsa_ctx->optional_ie_len ? rcsa_ctx->optional_ie : NULL,
				     rcsa_ctx->optional_ie_len);

	if (rcsa_ctx->rcsa_tx_cnt >= 0)
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
		wpa_printf(MSG_DEBUG,
			   "rcsa: defer TX because channel switch is already in progress");
		return 0;
	}

	if (hostapd_is_rcsa_inprogress(iface)) {
		wpa_printf(MSG_DEBUG,
			   "rcsa: inprogress");
		return -EINVAL;
	}

	nol_ie_len = hostapd_build_nol_ie(iface,
					  nol_ie_buf,
					  sizeof(nol_ie_buf));
	if (nol_ie_len > 0)
		attach_nol_ie = true;

	hostapd_get_local_rcsa_ml_info(iface, &attach_ml_ie, &link_id_bitmap);
	wpa_printf(MSG_DEBUG, "rcsa: attach_ml %u attach_nol %u",
		   attach_ml_ie, attach_nol_ie);

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
	struct hostapd_data *target_hapd;
	struct hostapd_iface *iface;
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

	iface = hapd->iface;
	target_hapd = hapd;
	if (iface->bss[0]->conf->mld_ap &&
	    (*mlinfo_linkid != -1)) {
		target_hapd = switch_link_hapd(hapd, *mlinfo_linkid);
		if (!target_hapd)
			return 0;
	}

	iface = target_hapd->iface;
	if (!iface)
		return 0;

	hostapd_parse_rcsa_nol_ie(iface, pos, rem_len);
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

	wpa_printf(MSG_DEBUG, "rcsa: received RCSA from repeater");

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

	if (iface->iface_extn.nol_info_valid &&
	    !hostapd_is_backhaul_sta_configured(iface)) {
		wpa_printf(MSG_DEBUG, "RCSA: notifying radar from parsed NOL IE");
		hostapd_rcsa_notify_radar(target_hapd);
	}

	if (iface->iface_extn.nol_info_valid) {
		wpa_printf(MSG_DEBUG, "RCSA: clearing parsed NOL IE valid flag");
		iface->iface_extn.nol_info_valid = false;
	}

	if (!hostapd_rcsa_rx_bh_enabled(iface))
		return 1;

	if (hostapd_csa_in_progress(iface)) {
		wpa_printf(MSG_DEBUG,
			   "rcsa: defer forwarding because channel switch is already in progress");
		return 0;
	}

	if (hostapd_is_rcsa_inprogress(iface)) {
		wpa_printf(MSG_DEBUG, "rcsa: inprogress");
		return 1;
	}

	wpa_printf(MSG_DEBUG, "rcsa: proceeding with Tx");

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
		eloop_register_timeout(0, HOSTAPD_DFS_BH_DISCONNECT_WAIT_TIME_US,
				       hostapd_rcsa_trigger_channal_change,
				       iface, NULL);
		return;
	}
	wpa_printf(MSG_DEBUG, "rcsa: CSA timeout: trigger channel switch");
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
		eloop_register_timeout(0, HOSTAPD_DFS_BH_DISCONNECT_WAIT_TIME_US,
				       hostapd_rcsa_trigger_channal_change,
				       iface, NULL);
	}
}

/**
 * wpa_rcsa_get_local_ml_info - Get local ML information for RCSA
 * @wpa_s: wpa_supplicant context for the associated interface
 * @include_ml_ie: output flag, set to true if ML IE should be included
 * @link_id_bitmap: output bitmap of link IDs selected for ML IE
 * @link_id: output link id of the MLD
 */
static void wpa_rcsa_get_local_ml_info(struct wpa_supplicant *wpa_s,
					  bool *include_ml_ie,
					  u16 *link_id_bitmap, u16 *link_id)
{
	u16 i;
	*include_ml_ie = false;
	*link_id_bitmap = 0;

	if (!wpa_s)
		return;

#ifdef CONFIG_IEEE80211BE
	if (!wpa_s->valid_links)
		return;

	for_each_link(wpa_s->valid_links, i) {
		int freq = wpa_s->links[i].freq;

		if (!freq)
			continue;

		if (freq == wpa_s->assoc_freq)
			*link_id = i;

		if (is_5ghz_freq(freq) && (wpa_s->wpa_state == WPA_STACACING ||
		    wpa_s->links[i].pending_ch_switch_freq)) {
			*include_ml_ie = true;
			*link_id_bitmap = BIT(i);
			wpa_printf(MSG_DEBUG,
				   "rcsa: selecting 5G link %u (freq=%d) for ML info IE",
				   i, freq);
			break;
		}
	}
#endif /* CONFIG_IEEE80211BE */
}

/**
 * wpa_rcsa_prepare_nol_ie - Build NOL IE from DFS radar event
 * @radar: DFS radar event describing the detected radar
 * @nol_ie_buf: output buffer for the constructed NOL IE
 * @nol_ie_buf_len: length of the output buffer
 *
 * Returns: length of the NOL IE written to @nol_ie_buf on success,
 * or -1 on error.
 */
static int wpa_rcsa_prepare_nol_ie(const struct dfs_event *radar,
				   u8 *nol_ie_buf,
				   size_t nol_ie_buf_len)
{
	enum dfs_nol_ie_bw_mhz bw_mhz;
	int bandwidth_mhz;
	int n_subchans;
	u16 bitmap_mask;
	u16 radar_bitmap_oper;
	int start_idx;
	int end_idx;
	int contiguous_count;
	u16 contiguous_bitmap;
	u8 *pos, *len_pos;
	size_t needed_len;

	bw_mhz = channel_width_to_int(radar->chan_width);
	bandwidth_mhz = (int) bw_mhz;
	n_subchans = bandwidth_mhz / MIN_DFS_SUBCHAN_BW;
	if (n_subchans <= 0 || n_subchans > DFS_MAX_20M_SUB_CH) {
		wpa_printf(MSG_DEBUG,
			   "rcsa: invalid subchannel count %d for bw=%d",
			   n_subchans, bandwidth_mhz);
		return -1;
	}

	bitmap_mask = DFS_NOL_IE_BITMAP_MASK(n_subchans);
	radar_bitmap_oper = radar->radar_bitmap & bitmap_mask;

	if (!radar_bitmap_oper) {
		wpa_printf(MSG_DEBUG,
			   "rcsa: radar_bitmap is 0 after masking (0x%04x)",
			   radar->radar_bitmap);
		return -1;
	}

	start_idx = 0;
	while (start_idx < n_subchans &&
			!(radar_bitmap_oper & (1U << start_idx)))
		start_idx++;

	if (start_idx >= n_subchans) {
		wpa_printf(MSG_DEBUG,
				"rcsa: no radar-affected subchannel found");
		return -1;
	}

	end_idx = start_idx;
	while (end_idx < n_subchans &&
			(radar_bitmap_oper & (1U << end_idx)))
		end_idx++;

	contiguous_count = end_idx - start_idx;
	contiguous_bitmap = (1U << contiguous_count) - 1;

	wpa_printf(MSG_DEBUG,
		   "rcsa: STA NOL IE base_freq=%d bw=%u bitmap=0x%02x"
		   " (start_idx=%d count=%d)",
		   radar->freq, (unsigned int) DFS_NOL_IE_BW_20_MHZ,
		   (u8) (contiguous_bitmap & 0xFF),
		   start_idx, contiguous_count);

	/* Build NOL IE — same wire format as hostapd_build_nol_ie():
	 *   EID_VENDOR_SPECIFIC | len | bw(1) | freq_le16(2) | bitmap(1)
	 * bw is MIN_DFS_SUBCHAN_BW (20 MHz), freq is the primary radar subchan,
	 * bitmap is the contiguous radar subchannel mask from the event.
	 */
	needed_len = 2 + 1 + 2 + 1;
	if (nol_ie_buf_len < needed_len)
		return -1;

	pos = nol_ie_buf;
	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	len_pos = pos++;
	*pos++ = (u8) RCSA_MIN_DFS_SUBCHAN_BW;
	WPA_PUT_LE16(pos, (u16)radar->freq +
		    start_idx * MIN_DFS_SUBCHAN_BW);
	pos += 2;
	*pos++ = (u8)(contiguous_bitmap & 0xFF);
	*len_pos = pos - len_pos - 1;

	return pos - nol_ie_buf;
}

/**
 * wpa_rcsa_build_opt_ies - Build optional IEs for RCSA from local ML info
 * @wpa_s: wpa_supplicant context for the associated interface
 * @nol_ie: pointer to NOL IE buffer, or NULL if not present
 * @nol_ie_len: length of the NOL IE buffer
 * @opt_ie: output buffer for the constructed optional IEs
 * @opt_ie_buf_len: length of the output buffer
 * @link_id: link id of the MLD
 *
 * Returns: length of the optional IEs written to @opt_ie.
 */
static size_t wpa_rcsa_build_opt_ies(struct wpa_supplicant *wpa_s,
				     const u8 *nol_ie, size_t nol_ie_len,
				     u8 *opt_ie, size_t opt_ie_buf_len,
				     u16 *link_id)
{
	bool include_ml_ie;
	u16 link_id_bitmap;

	wpa_rcsa_get_local_ml_info(wpa_s, &include_ml_ie,
				   &link_id_bitmap, link_id);

	return hostapd_build_rcsa_optional_ies(
		nol_ie_len > 0 ? nol_ie : NULL,
		nol_ie_len > 0 ? nol_ie_len : 0,
		include_ml_ie, link_id_bitmap,
		opt_ie, opt_ie_buf_len);
}

void wpa_rcsa_handle_radar(struct wpa_supplicant *wpa_s,
			   const struct dfs_event *radar)
{
	u8 nol_ie_buf[RCSA_MAX_OPTIONAL_IE_LEN];
	u8 opt_ie[RCSA_MAX_OPTIONAL_IE_LEN];
	int nol_ie_len;
	size_t opt_ie_len;
	u8 chan;
	u16 link_id = -1;
	unsigned int tx_freq;

	nol_ie_len = wpa_rcsa_prepare_nol_ie(radar,
					     nol_ie_buf,
					     sizeof(nol_ie_buf));
	if (nol_ie_len < 0)
		return;

	opt_ie_len = wpa_rcsa_build_opt_ies(wpa_s,
					    nol_ie_buf,
					    (size_t) nol_ie_len,
					    opt_ie,
					    sizeof(opt_ie), &link_id);

	if (link_id < 0)
		return;

	if (wpa_s->links[link_id].pending_ch_switch_freq)
		tx_freq = wpa_s->links[link_id].pending_ch_switch_freq;
	else
		tx_freq = wpa_s->assoc_freq;
	ieee80211_freq_to_chan(tx_freq, &chan);
	wpa_printf(MSG_INFO,
		   "rcsa: radar detected freq %d [%d], sending RCSA Txfreq=%d  bw=%u"
		   "bitmap=0x%04x opt_len=%zu wpa_s->assoc_freq %d chan %d",
		   radar->freq, link_id, tx_freq, radar->chan_width,
		   radar->radar_bitmap, opt_ie_len,wpa_s->assoc_freq, chan);

	/* Currently only one RCSA sent. TODO sending RCSA for 5 TBTT */
#ifdef UCODE_SUPPORT
	wpa_drv_notify_rcsa(wpa_s,
			    tx_freq,
			    chan,
			    HOSTAPD_RCSA_TX_COUNT,
			    HOSTAPD_RCSA_SWITCH_MODE,
			    opt_ie_len ? opt_ie : NULL,
			    opt_ie_len);
#endif
}
