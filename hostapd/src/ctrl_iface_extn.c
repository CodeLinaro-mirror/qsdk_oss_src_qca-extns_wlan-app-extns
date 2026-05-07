// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "esp.h"
#include "dcs.h"
#include "cmn.h"
#include "utils/os.h"
#include "common/ieee802_11_defs.h"
#include "ap/ap_config.h"
#include "ap/beacon.h"
#include "ap/dfs.h"
#include "ap/hw_features.h"
#include "ap/ap_drv_ops.h"
#include "hostapd_rptr_extn.h"
#include "cmn.h"
#include "ap/ieee802_11.h"

#define DEF_VLP_NON_PRIOR_PENALTY	30

/**
 * hostapd_ctrl_get_hw_info_extn - Return current hardware info
 * @hapd: Pointer to the hostapd instance
 * @buf: Caller-provided output buffer to receive stringified hw info
 * @buflen: Size of @buf in bytes
 *
 * Return: Number of bytes written to @buf (excluding null terminator) on success,
 *         or -1 on failure (e.g., invalid arguments or buffer too small).
 */
int hostapd_ctrl_get_hw_info_extn(struct hostapd_data *hapd, char *buf, size_t buflen)
{
	int ret = -1;
	if (!hapd || !hapd->iface)
		return ret;

	if (hapd->iface->current_hw_info) {
		ret = os_snprintf(buf, buflen,
				  "hw_idx = %d start_freq = %d end_freq =%d\n",
				  hapd->iface->current_hw_info->hw_idx,
				  hapd->iface->current_hw_info->start_freq,
				  hapd->iface->current_hw_info->end_freq);
	}
	return ret;
}

static int hostapd_ctrl_iface_set_ht40intol_extn(struct hostapd_data *hapd, char *pos)
{
	int ret = -1;
	char *end;
	long value;
	bool user_ht40intol, current_ht40intol;

	if (!hapd || !hapd->iconf || !pos)
		return ret;

	value = strtol(pos, &end, 10);
	if (pos == end || *end != '\0' || value < 0 || value > 1) {
		wpa_printf(MSG_ERROR, "Invalid input for ht40intol (expected 0 or 1)\n");
		return ret;
	}

	user_ht40intol = (value == 1);
	current_ht40intol = !!(hapd->iconf->ht_capab & HT_CAP_INFO_40MHZ_INTOLERANT);
	if (current_ht40intol == user_ht40intol) {
		wpa_printf(MSG_DEBUG, "Intolerance is already %d\n", current_ht40intol);
		return 0;
	}

	if (user_ht40intol)
		hapd->iconf->ht_capab |= HT_CAP_INFO_40MHZ_INTOLERANT;
	else
		hapd->iconf->ht_capab &= ~HT_CAP_INFO_40MHZ_INTOLERANT;

	ret = ieee802_11_update_beacons(hapd->iface);
	if (ret)
		wpa_printf(MSG_ERROR, "Failed to update beacons.\n");

	return ret;
}

static int hostapd_ctrl_iface_get_ht40intol_extn(struct hostapd_data *hapd, char *buf,
						 size_t buflen)
{
	int ret = -1;

	if (!hapd || !hapd->iconf)
		return ret;

	ret = os_snprintf(buf, buflen, "ht40intol: %d\n",
			  !!(hapd->iconf->ht_capab & HT_CAP_INFO_40MHZ_INTOLERANT));

	return ret;
}

static int hostapd_ctrl_iface_set_eht_config_ccfs0_extn(struct hostapd_data *hapd,
							char *pos)
{
	char *end;
	long user_input;
	bool eht_config_ccfs0;

	if (!hapd || !hapd->iconf)
		return -1;

	if (!hostapd_is_eht_enabled(hapd)) {
		wpa_printf(MSG_ERROR, "EHT CONFIG CCFS0 is not allowed in current mode");
		return -1;
	}

	user_input = strtol(pos, &end, 10);
	if (pos == end || *end != '\0' || user_input < 0 || user_input > 1) {
		wpa_printf(MSG_ERROR, "Invalid input for set_eht_config_ccfs0\n");
		return -1;
	}

	eht_config_ccfs0 = (user_input == 1);
	hapd->iconf->conf_extn.eht_config_ccfs0 = eht_config_ccfs0;

	return 0;
}

static int hostapd_ctrl_iface_get_eht_config_ccfs0_extn(struct hostapd_data *hapd,
							char *buf, size_t buflen)
{
	int ret = -1;

	if (!hapd || !hapd->iconf || !buf)
		return ret;

	if (!hostapd_is_eht_enabled(hapd)) {
		wpa_printf(MSG_ERROR, "Not supported in current mode");
		return -1;
	}

	ret = os_snprintf(buf, buflen, "eht_config_ccfs0 %d\n",
			  hapd->iconf->conf_extn.eht_config_ccfs0);

	return ret;
}

static int hostapd_ctrl_iface_set_tpe_common_psd_extn(struct hostapd_data *hapd,
						      char *pos)
{
	char *end;
	long user_input;
	bool tpe_common_psd;

	if (!hapd || !hapd->conf)
		return -1;

	user_input = strtol(pos, &end, 10);
	if (pos == end || *end != '\0' || user_input < 0 || user_input > 1) {
		wpa_printf(MSG_ERROR, "Invalid input for set_tpe_common_psd\n");
		return -1;
	}

	tpe_common_psd = (user_input == 1);
	hapd->conf->bss_extn.tpe_common_psd = tpe_common_psd;

	return 0;
}

static int hostapd_ctrl_iface_get_tpe_common_psd_extn(struct hostapd_data *hapd,
						      char *buf, size_t buflen)
{
	int ret = -1;

	if (!hapd || !hapd->conf || !buf)
		return ret;

	ret = os_snprintf(buf, buflen, "tpe_common_psd %d\n",
			  hapd->conf->bss_extn.tpe_common_psd);

	return ret;
}

static int hostapd_ctrl_iface_set_tpe_tx_pwr_interp_extn(struct hostapd_data *hapd,
							 char *pos)
{
	char *end;
	long user_input;

	if (!hapd || !hapd->conf || !pos)
		return -1;

	user_input = strtol(pos, &end, 10);
	if (pos == end || *end != '\0' ||
	    (user_input != TPE_REG_EIRP_PSD && user_input != TPE_REG_EIRP)) {
		wpa_printf(MSG_ERROR,
			   "Invalid input for set_tpe_tx_pwr_interp "
			   "(expected 0(TPE_REG_EIRP_PSD) or 1(TPE_REG_EIRP))\n");
		return -1;
	}

	hapd->conf->bss_extn.tpe_tx_pwr_interp = (enum tpe_tx_pwr_interp_unit)user_input;

	return 0;
}

static int hostapd_ctrl_iface_get_tpe_tx_pwr_interp_extn(struct hostapd_data *hapd,
							 char *buf, size_t buflen)
{
	int ret = -1;

	if (!hapd || !hapd->conf || !buf)
		return ret;

	if (hapd->conf->bss_extn.tpe_tx_pwr_interp < TPE_REG_EIRP_PSD ||
	    hapd->conf->bss_extn.tpe_tx_pwr_interp > TPE_REG_EIRP) {
		wpa_printf(MSG_ERROR, "Invalid tpe_tx_pwr_interp value %d\n",
			   hapd->conf->bss_extn.tpe_tx_pwr_interp);
		return ret;
	}

	ret = os_snprintf(buf, buflen, "tpe_tx_pwr_interp %d\n",
			  hapd->conf->bss_extn.tpe_tx_pwr_interp);

	return ret;
}

static int hostapd_ctrl_iface_set_tpe_punct_channel_tx_pwr_extn(struct hostapd_data *hapd,
								char *pos)
{
	char *end;
	long user_input;

	if (!hapd || !hapd->conf || !pos)
		return -1;

	user_input = strtol(pos, &end, 10);
	if (pos == end || *end != '\0' || user_input < 0 || user_input > 1) {
		wpa_printf(MSG_ERROR, "Invalid input for set_tpe_punct_channel_tx_pwr\n");
		return -1;
	}

	hapd->conf->bss_extn.tpe_punct_channel_tx_pwr = (user_input == 1);

	return 0;
}

static int hostapd_ctrl_iface_get_tpe_punct_channel_tx_pwr_extn(struct hostapd_data *hapd,
								char *buf, size_t buflen)
{
	int ret = -1;

	if (!hapd || !hapd->conf || !buf)
		return ret;

	ret = os_snprintf(buf, buflen, "tpe_punct_channel_tx_pwr %d\n",
			  hapd->conf->bss_extn.tpe_punct_channel_tx_pwr);

	return ret;
}

static int hostapd_ctrl_iface_set_esp_extn(struct hostapd_data *hapd, char *cmd)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	char *param;
	long val_long;
	char *end = NULL;

	if (!iface_extn)
		return -1;

	if (!hapd->started || hapd->disabled) {
		wpa_printf(MSG_ERROR, "ESP: BSS is disabled");
		return -1;
	}

	param = strsep(&cmd, "=");
	if (!param || !cmd)
		return -1;

	if (strcmp(param, "enable_esp") != 0 && !iface_extn->esp.enable) {
		wpa_printf(MSG_ERROR, "ESP feature is not enabled");
		return -1;
	}

	while (*cmd == ' ') cmd++;
	if (*cmd == '\0') {
		wpa_printf(MSG_ERROR, "ESP: Invalid value for %s: empty", param);
		return -1;
	}

	errno = 0;
	val_long = strtol(cmd, &end, 10);
	if (val_long < 0) {
		wpa_printf(MSG_ERROR, "ESP: Negative value not allowed for %s: %ld", param, val_long);
		return -1;
	}

	if (errno != 0 || end == cmd) {
		wpa_printf(MSG_ERROR, "ESP: Invalid numeric value for %s: '%s'", param, cmd);
		return -1;
	}

	while (end && *end == ' ') end++;
	if (end && *end != '\0') {
		wpa_printf(MSG_ERROR, "ESP: Invalid characters in value for %s: '%s'", param, cmd);
		return -1;
	}
	if (os_strcmp(param, "esp_airtime") == 0) {
		if (val_long > 255) {
			wpa_printf(MSG_ERROR, "ESP: Invalid airtime value. Permissible range: 0-255");
			return -1;
		}
	} else if (os_strcmp(param, "esp_ppdu_dur") == 0) {
		if (val_long > 255) {
			wpa_printf(MSG_ERROR, "ESP: Invalid PPDU duration value. Permissible range: 0-255");
			return -1;
		}
	} else if (os_strcmp(param, "esp_ba_window") == 0) {
		if (val_long > 7) {
			wpa_printf(MSG_ERROR, "ESP: Invalid BA window value. Permissible range: 0-7");
			return -1;
		}
	} else if (os_strcmp(param, "enable_esp") == 0) {
		if (val_long != 0 && val_long != 1) {
			wpa_printf(MSG_ERROR, "ESP: Invalid enable_esp value. Permissible values: 0 or 1");
			return -1;
		}
	}

	return hostapd_drv_set_esp_param_extn(hapd, param, (int)val_long);
}


static int hostapd_ctrl_iface_get_esp_extn(struct hostapd_data *hapd,
					   const char *cmd, char *reply,
					   int reply_size)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	int ret;
	u8 airtime, ppdu_dur, ba_window;

	if (!iface_extn)
		return -1;

	if (!hapd->started || hapd->disabled) {
		wpa_printf(MSG_ERROR, "ESP: BSS is disabled");
		return -1;
	}

	if (iface_extn->esp.enable) {
		if (iface_extn->esp.airtime)
			airtime = iface_extn->esp.airtime;
		else
			airtime = iface_extn->esp.computed_airtime;

		if (iface_extn->esp.ppdu_dur)
			ppdu_dur = iface_extn->esp.ppdu_dur;
		else
			ppdu_dur = ESP_DEFAULT_PPDU_DURATION;

		if (iface_extn->esp.ba_window)
			ba_window = iface_extn->esp.ba_window;
		else
			ba_window = 5;
	} else {
		airtime = 0;
		ppdu_dur = 0;
		ba_window = 0;
	}


	ret = os_snprintf(reply, reply_size,
			  "airtime=%u "
			  "ppdu_dur=%u "
			  "ba_window=%u "
			  "enable_esp=%u\n",
			  airtime,
			  ppdu_dur,
			  ba_window,
			  iface_extn->esp.enable);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int
hostapd_ctrl_iface_set_non_prior_penalty_extn(struct hostapd_data *hapd,
					      const char *cmd)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	char *end = NULL;
	long val;

	if (!cmd)
		return -1;

	while (*cmd == ' ')
		cmd++;
	if (*cmd == '\0')
		return -1;

	errno = 0;
	val = strtol(cmd, &end, 10);
	if (errno != 0 || end == cmd)
		return -1;

	while (end && *end == ' ')
		end++;
	if (end && *end != '\0')
		return -1;

	if (val < 0 || val > 100) {
		wpa_printf(MSG_ERROR,
			   "VLP Non-priority penalty out of range (0-100): %ld",
			   val);
		return -1;
	}

	iface_extn->vlp_non_prior_penalty = (u8)val;
	wpa_printf(MSG_DEBUG, "VLP Non-priority penalty set to %u%%",
		   iface_extn->vlp_non_prior_penalty);
	return 0;
}

static int
hostapd_ctrl_iface_get_non_prior_penalty_extn(struct hostapd_data *hapd,
					      char *reply,
					      int reply_size)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	u8 non_prior_penalty = DEF_VLP_NON_PRIOR_PENALTY;
	int ret;

	if (iface_extn->vlp_non_prior_penalty)
		non_prior_penalty = iface_extn->vlp_non_prior_penalty;
	ret = os_snprintf(reply, reply_size, "%u\n", non_prior_penalty);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_ctrl_iface_country_ie_extn(struct hostapd_data *hapd,
					      const char *value)
{
	char *end;
	long enabled;
	int old_enabled;

	enabled = strtol(value, &end, 10);
	if (value == end || *end != '\0' || (enabled != 0 && enabled != 1)) {
		wpa_printf(MSG_ERROR,
			   "CTRL_IFACE COUNTRY_IE: invalid value '%s' (expected 0 or 1)",
			   value);
		return -1;
	}

	old_enabled = hapd->iconf->ieee80211d;
	if ((int) enabled == old_enabled)
		return 0;

	hapd->iconf->ieee80211d = enabled;
	if (ieee802_11_update_beacons(hapd->iface) < 0) {
		hapd->iconf->ieee80211d = old_enabled;
		return -1;
	}

	return 0;
}


static int hostapd_ctrl_iface_get_country_ie_extn(struct hostapd_data *hapd,
						  char *reply,
						  size_t reply_size)
{
	int ret;

	ret = os_snprintf(reply, reply_size, "%d\n", hapd->iconf->ieee80211d);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_ctrl_set_rnr_6ghz_colocated_extn(struct hostapd_data *hapd, char *cmd)
{
#ifdef NEED_AP_MLME
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	uint8_t rnr_mode, frm_val;
	char *ptr, *endptr;

	if (!hapd->started) {
		wpa_printf(MSG_ERROR, "Interface is not UP.\n");
		return -1;
	}

	rnr_mode = (uint8_t)strtol(cmd, &ptr, 10);
	if (ptr == cmd || rnr_mode > 1) {
		wpa_printf(MSG_ERROR, "Invalid mode. Use 1:Enable 0:Disable");
		return -1;
	}

	frm_val = (uint8_t)strtol(ptr, &endptr, 10);
	if (ptr == endptr || frm_val > 7) {
		wpa_printf(MSG_ERROR, "Invalid frm_val. Valid values:0 to 7");
		return -1;
	}

	if (rnr_mode == 1 && frm_val == 0) {
		wpa_printf(MSG_ERROR, "Mode is enable But frm is not selected. Invalid frm_val");
		return -1;
	}

	if (rnr_mode == 1) {
		/* User rnr mode enable: set frame mask  */
		conf_extn->rnr_6ghz_colocated_enable |= (frm_val & 0x7);
	} else {
		/* User rnr mode disable: clear frame mask */
		conf_extn->rnr_6ghz_colocated_enable &= ~(frm_val & 0x7);
	}
	wpa_printf(MSG_INFO, "rnr_mode:%d frm_val:%d rnr_6ghz_colocated_enable %d",
			rnr_mode, frm_val, conf_extn->rnr_6ghz_colocated_enable);

	/* Update beacon to reflect the config */
	ieee802_11_update_beacons(hapd->iface);

	return 0;
#else /* NEED_AP_MLME */
	return -1;
#endif /* NEED_AP_MLME */
}

static int hostapd_ctrl_get_rnr_6ghz_colocated_extn(struct hostapd_data *hapd,
						    const char *cmd, char *reply,
						    int reply_size)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	int ret;

	ret = os_snprintf(reply, reply_size,
			"rnr_6ghz_colocated=%d\n",
			conf_extn->rnr_6ghz_colocated_enable);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

#ifdef CONFIG_TESTING_OPTIONS
static int hostapd_ctrl_iface_sync_iface_freq_extn(struct hostapd_data *hapd)
{
#ifdef NEED_AP_MLME
	unsigned int freq;

	if (hapd->started) {
		wpa_printf(MSG_ERROR, "SYNC_IFACE_FREQ: Cannot sync frequency on already started interface");
		return -1;
	}

	freq = hostapd_hw_get_freq(hapd, hapd->iface->conf->channel);
	if (!freq)
		return -1;

	hapd->iface->freq = freq;
	return 0;
#else /* NEED_AP_MLME */
	return -1;
#endif /* NEED_AP_MLME */
}
#endif /* CONFIG_TESTING_OPTIONS */

/**
 * hostapd_iface_rep_ap_enable_extn - Handle REP_AP_ENABLE control command
 * @iface: Pointer to hostapd interface on which repeater AP is enabled
 * @pos: Pointer to control command arguments string (e.g., freq/width/etc.)
 *
 * Parse repeater AP enable parameters (frequency, bandwidth, puncturing
 * bitmap, center frequencies, and secondary offset), update interface
 * configuration, and restart or enable the AP with appropriate DFS/CAC
 * handling.
 *
 * Return: 0 on success or -1 on invalid parameters or configuration errors.
 */
int hostapd_iface_rep_ap_enable_extn(struct hostapd_iface *iface, char *pos)
{
	int freq = 0, width = 0, sec_off = 0;
	int cf1 = 0, cf2 = 0;
	u16 punct_bitmap = 0;
	enum oper_chan_width oper_chwidth = CONF_OPER_CHWIDTH_USE_HT;
	u8 op_class = 0, channel = 0;
	enum hostapd_hw_mode hw_mode;
	int i, ret;
	bool skip_cac_rep = false;
	struct hostapd_config *conf;
	char *param;

	if (!iface || !iface->conf || !pos)
		return -1;

	conf = iface->conf;

	/* Parse fields */
	param = os_strstr(pos, "freq=");
	if (param)
		freq = atoi(param + 5);

	param = os_strstr(pos, " width=");
	if (param)
		width = atoi(param + 7);

	param = os_strstr(pos, " punct_bitmap=");
	if (param)
		punct_bitmap = (u16) atoi(param + 14);

	param = os_strstr(pos, " c_freq1=");
	if (param)
		cf1 = atoi(param + 9);

	param = os_strstr(pos, " c_freq2=");
	if (param)
		cf2 = atoi(param + 9);

	param = os_strstr(pos, " sec_off=");
	if (param)
		sec_off = atoi(param + 9);

	if (!freq) {
		wpa_printf(MSG_ERROR, "REP_AP_ENABLE: missing freq");
		return -1;
	}

	/* Map provided bandwidth to oper_chwidth */
	switch (width) {
	case 80:
		oper_chwidth = (cf2 ? CONF_OPER_CHWIDTH_80P80MHZ :
			       CONF_OPER_CHWIDTH_80MHZ);
		break;
	case 160:
		oper_chwidth = CONF_OPER_CHWIDTH_160MHZ;
		break;
	case 320:
		oper_chwidth = CONF_OPER_CHWIDTH_320MHZ;
		break;
	case 40:
	case 20:
	default:
		oper_chwidth = CONF_OPER_CHWIDTH_USE_HT;
		break;
	}

	/* Determine channel/opclass and HW mode from given freq */
	hw_mode = ieee80211_freq_to_channel_ext(freq, sec_off, oper_chwidth,
						&op_class, &channel);
	if (hw_mode == NUM_HOSTAPD_MODES) {
		wpa_printf(MSG_ERROR, "REP_AP_ENABLE: invalid frequency %d", freq);
		return -1;
	}

	/* Set center segment indices */
	ieee80211_freq_to_chan(freq, &channel);
	hostapd_set_oper_centr_freq_seg0_idx(conf, channel);
	if (cf1 == 5935)
		hostapd_set_oper_centr_freq_seg0_idx(conf, (cf1 - 5925) / 5);
	else if (cf1 > 5950)
		hostapd_set_oper_centr_freq_seg0_idx(conf, (cf1 - 5950) / 5);
	else if (cf1 > 5000)
		hostapd_set_oper_centr_freq_seg0_idx(conf, (cf1 - 5000) / 5);
	else if (cf1 > 0)
		ieee80211_freq_to_chan(cf1, (u8 *) &channel);

	hostapd_set_oper_chwidth(conf, oper_chwidth);

	if (cf2 > 0 && oper_chwidth == CONF_OPER_CHWIDTH_80P80MHZ) {
		int seg1_idx = 0;
		if (cf2 == 5935)
			seg1_idx = (cf2 - 5925) / 5;
		else if (cf2 > 5950)
			seg1_idx = (cf2 - 5950) / 5;
		else if (cf2 > 5000)
			seg1_idx = (cf2 - 5000) / 5;
		else
			ieee80211_freq_to_chan(cf2, (u8 *) &seg1_idx);
		hostapd_set_oper_centr_freq_seg1_idx(conf, seg1_idx);
	} else {
		hostapd_set_oper_centr_freq_seg1_idx(conf, 0);
	}

	conf->punct_bitmap = punct_bitmap;
	conf->acs = 0;

	/* Decide DFS/CAC skip if configured to skip */
	if (conf->conf_extn.skip_cac) {
		skip_cac_rep = ieee80211_is_dfs(freq, iface->hw_features,
						iface->num_hw_features);
	}
	wpa_printf(MSG_INFO, "REP_AP_ENABLE: skip_cac_rep = %d", skip_cac_rep);

	iface->freq = freq;

	/* Restart/Enable iface if needed around DFS CAC requirement */
	switch (iface->state) {
	case HAPD_IFACE_ENABLED:
		if (!skip_cac_rep && (!hostapd_is_dfs_required(iface) ||
		    hostapd_is_dfs_chan_available(iface)))
			break;
		wpa_printf(MSG_INFO,
			   "DFS CAC required on new channel, restart interface");
		/* fallthrough */
	default:
		hostapd_disable_iface(iface);
		break;
	}

	if (conf->channel && !iface->freq)
		iface->freq = hostapd_hw_get_freq(iface->bss[0], conf->channel);

	if (iface->state != HAPD_IFACE_ENABLED)
		hostapd_enable_iface(iface);

	hostapd_apply_6ghz_dynamic_puncturing(iface);
	if (is_6ghz_freq(iface->freq) && iface->conf->enable_best_power_mode) {
		u8 best_power_mode;
		best_power_mode = hostapd_get_best_ap_6ghz_power_mode_for_iface(iface);
		if (best_power_mode != NL80211_REG_NUM_POWER_MODES) {
			iface->conf->he_6ghz_reg_pwr_type = best_power_mode;
			wpa_printf(MSG_INFO,
				   "%s: Best power mode for Freq %d is %d",
				   __func__, iface->freq, best_power_mode);
		}
	}

	for (i = 0; i < iface->num_bss; i++) {
		struct hostapd_data *hapd = iface->bss[i];
		hapd->conf->start_disabled = 0;
#ifdef CONFIG_HOSTAPD_SRC_DIR
		ret = hostapd_set_freq(hapd, conf->hw_mode, iface->freq,
				       conf->channel,
				       conf->enable_edmg,
				       conf->edmg_channel,
				       conf->ieee80211n,
				       conf->ieee80211ac,
				       conf->ieee80211ax,
				       conf->ieee80211be,
				       conf->ieee80211bn,
				       conf->secondary_channel,
				       hostapd_get_oper_chwidth(conf),
				       hostapd_get_oper_centr_freq_seg0_idx(conf),
				       hostapd_get_oper_centr_freq_seg1_idx(conf),
				       skip_cac_rep,
				       conf->bandwidth_device,
				       conf->center_freq_device);
#endif
		wpa_printf(MSG_INFO,
			   "REP_AP_ENABLE: set_freq for bssid " MACSTR
			   " ret %d ifname %s",
			   MAC2STR(hapd->own_addr), ret, hapd->conf->iface);
		ret = ieee802_11_set_beacon(hapd);
		wpa_printf(MSG_DEBUG,
			   "REP_AP_ENABLE: set beacon for bssid " MACSTR
			   " ret %d",
			   MAC2STR(hapd->own_addr), ret);
	}

	return 0;
}

static int
hostapd_ctrl_iface_set_obss_snr_threshold_extn(struct hostapd_data *hapd,
					       const char *cmd)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	char *end = NULL;
	long val;

	if (!cmd)
		return -1;

	while (*cmd == ' ')
		cmd++;
	if (*cmd == '\0')
		return -1;

	errno = 0;
	val = strtol(cmd, &end, 10);
	if (errno != 0 || end == cmd)
		return -1;

	while (end && *end == ' ')
		end++;
	if (end && *end != '\0')
		return -1;

	if (val < OBSS_SNR_MIN || val > OBSS_SNR_MAX) {
		wpa_printf(MSG_ERROR,
			   "OBSS SNR threshold out of range (%d-%d): %ld",
			   OBSS_SNR_MIN, OBSS_SNR_MAX, val);
		return -1;
	}

	conf_extn->obss_snr_threshold = (u8)val;
	wpa_printf(MSG_DEBUG, "OBSS SNR threshold set to %u dB",
		   conf_extn->obss_snr_threshold);
	return 0;
}

static int
hostapd_ctrl_iface_get_obss_snr_threshold_extn(struct hostapd_data *hapd,
					       char *reply, int reply_size)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	int ret;

	ret = os_snprintf(reply, reply_size, "%u\n",
			  conf_extn->obss_snr_threshold);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int
hostapd_ctrl_iface_set_obss_rx_snr_threshold_extn(struct hostapd_data *hapd,
						   const char *cmd)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	char *end = NULL;
	long val;

	if (!cmd)
		return -1;

	while (*cmd == ' ')
		cmd++;
	if (*cmd == '\0')
		return -1;

	errno = 0;
	val = strtol(cmd, &end, 10);
	if (errno != 0 || end == cmd)
		return -1;

	while (end && *end == ' ')
		end++;
	if (end && *end != '\0')
		return -1;

	if (val < OBSS_SNR_MIN || val > OBSS_SNR_MAX) {
		wpa_printf(MSG_ERROR,
			   "OBSS RX SNR threshold out of range (%d-%d): %ld",
			   OBSS_SNR_MIN, OBSS_SNR_MAX, val);
		return -1;
	}

	conf_extn->obss_rx_snr_threshold = (u8)val;
	wpa_printf(MSG_DEBUG, "OBSS RX SNR threshold set to %u dB",
		   conf_extn->obss_rx_snr_threshold);
	return 0;
}

static int
hostapd_ctrl_iface_get_obss_rx_snr_threshold_extn(struct hostapd_data *hapd,
						   char *reply, int reply_size)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	int ret;

	ret = os_snprintf(reply, reply_size, "%u\n",
			  conf_extn->obss_rx_snr_threshold);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int
hostapd_ctrl_iface_set_autorecovery_after_nol_vapdown_extn(struct hostapd_data *hapd,
							   const char *cmd)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	char *end = NULL;
	long val;

	if (!cmd)
		return -1;

	while (*cmd == ' ')
		cmd++;
	if (*cmd == '\0')
		return -1;

	errno = 0;
	val = strtol(cmd, &end, 10);
	if (errno != 0 || end == cmd)
		return -1;

	while (end && *end == ' ')
		end++;
	if (end && *end != '\0')
		return -1;

	if (val < 0 || val > 1) {
		wpa_printf(MSG_ERROR,
			   "SET_AUTORECOVERY_AFTER_NOL_VAPDOWN: invalid value %d",
			   (int)val);
		return -1;
	}

	conf_extn->autorecovery_after_nol_vapdown = (int)val;
	wpa_printf(MSG_INFO,
		   "SET_AUTORECOVERY_AFTER_NOL_VAPDOWN: set to %d",
		   (int)val);
	return 0;
}

#ifdef CONFIG_QCA_LAB_TEST_FEATURES
static int hostapd_ctrl_iface_ignorecac_extn(struct hostapd_data *hapd,
					     const char *cmd)
{
	struct hostapd_config_extn *conf_extn;
	char *end;
	long val;

	if (!hapd || !hapd->iconf || !cmd)
		return -1;

	conf_extn = &hapd->iconf->conf_extn;

	while (*cmd == ' ')
		cmd++;

	val = strtol(cmd, &end, 10);
	if (cmd == end)
		return -1;

	while (*end == ' ')
		end++;
	if (*end != '\0')
		return -1;

	if (val != 0 && val != 1)
		return -1;

	conf_extn->ignorecac = val;
	if (hapd->iface)
		hapd->iface->iface_extn.ignorecac = val;
	if (hapd->iface && hapd->iface->conf)
		hapd->iface->conf->conf_extn.ignorecac = val;
	wpa_printf(MSG_INFO, "IGNORECAC=%ld on interface %s",
		   val, hapd->conf->iface);

	return 0;
}

static int hostapd_ctrl_iface_get_ignorecac_extn(struct hostapd_data *hapd,
						 char *reply,
						 size_t reply_size)
{
	int res;

	if (!hapd || !hapd->iface)
		return -1;

	res = os_snprintf(reply, reply_size, "%d\n",
			  hapd->iconf->conf_extn.ignorecac ? 1 : 0);
	if (os_snprintf_error(reply_size, res))
		return -1;

	return res;
}
#endif /* CONFIG_QCA_LAB_TEST_FEATURES */

#ifdef CONFIG_IEEE80211AC
static int hostapd_ctrl_iface_mu_cap_war_extn(struct hostapd_data_extn *hapd_extn,
					    const char *cmd)
{
	int val;

	if (!cmd || sscanf(cmd, "%d", &val) != 1 || (val != 0 && val != 1))
		return -1;

	hapd_extn->mu_cap_war = val ? 1 : 0;

	wpa_printf(MSG_DEBUG, "MU_CAP_WAR state: %s", val ? "enabled":"disabled");

	return 0;
}

static int hostapd_ctrl_iface_get_mu_cap_war_extn(struct hostapd_data_extn *hapd_extn,
						char *reply,
					        size_t reply_size)
{
	int res;

	res = os_snprintf(reply, reply_size, "%s\n",
			  hapd_extn->mu_cap_war ? "Enabled" : "Disabled");

	if (os_snprintf_error(reply_size, res))
		return -1;

	return res;
}
#endif /* CONFIG_IEEE80211AC */

static int hostapd_ctrl_iface_dfs_no_wradar_extn(struct hostapd_data *hapd,
						 const char *value)
{
	char *end = NULL;
	long val;
	struct hostapd_iface_extn *iface_extn;

	if (!hapd || !hapd->iface || !value) {
		wpa_printf(MSG_ERROR, "DFS_NO_WRADAR: Invalid parameters");
		return -1;
	}

	iface_extn = &hapd->iface->iface_extn;

	val = strtol(value, &end, 10);
	if (end == value) {
		wpa_printf(MSG_ERROR, "DFS_NO_WRADAR: Invalid value format");
		return -1;
	}
	while (end && (*end == ' ' || *end == '\n' || *end == '\r' ||
		       *end == '\t'))
		end++;
	if (end && *end != '\0') {
		wpa_printf(MSG_ERROR, "DFS_NO_WRADAR: Trailing characters after value");
		return -1;
	}

	if (val != 0 && val != 1) {
		wpa_printf(MSG_ERROR, "DFS_NO_WRADAR: Value must be 0 or 1");
		return -1;
	}

	iface_extn->dfs_no_wradar = !!val;

	return 0;
}

static int hostapd_ctrl_iface_get_dfs_no_wradar_extn(struct hostapd_data *hapd,
						     char *reply,
						     size_t reply_size)
{
	return os_snprintf(reply, reply_size, "%d\n",
			   hapd && hapd->iface &&
			   hapd->iface->iface_extn.dfs_no_wradar);
}

int
hostapd_ctrl_iface_receive_process_extn(struct hostapd_data *hapd,
					char *buf, char *reply,
					int reply_size,
					struct sockaddr_storage *from,
					socklen_t fromlen, int *reply_len)
{
	int reply_len_extn = *reply_len;

	if (os_strncmp(buf, "SET_ESP", 7) == 0) {
		if (hostapd_ctrl_iface_set_esp_extn(hapd, buf + 8))
			reply_len_extn = -1;
	} else if (os_strncmp(buf, "GET_ESP", 7) == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_esp_extn(hapd, buf + 7, reply,
								 reply_size);
	} else if (os_strncmp(buf, "RNR_6GHZ_COLOCATED ", 19) == 0) {
		if (hostapd_ctrl_set_rnr_6ghz_colocated_extn(hapd, buf + 19))
			reply_len_extn = -1;
        } else if (os_strncmp(buf, "GET_RNR_6GHZ_COLOCATED", 22) == 0) {
		reply_len_extn = hostapd_ctrl_get_rnr_6ghz_colocated_extn(hapd, buf + 22, reply,
									  reply_size);
	} else if (os_strcmp(buf, "GET_HW_INFO") == 0) {
		reply_len_extn = hostapd_ctrl_get_hw_info_extn(hapd, reply, reply_size);
	} else if (os_strncmp(buf, "REP_AP_ENABLE ", 14) == 0) {
		if (hostapd_iface_rep_ap_enable_extn(hapd->iface, buf + 14))
			reply_len_extn = -1;
#ifdef CONFIG_ACS
	} else if (os_strncmp(buf, "ACS ", 4) == 0) {
		reply_len_extn = hostapd_handle_cli_acs_extn(hapd, buf + 4,
							     reply, reply_size);
#endif
#ifdef CONFIG_QCA_LAB_TEST_FEATURES
	} else if (os_strcmp(buf, "IGNORECAC") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_ignorecac_extn(hapd, reply,
								       reply_size);
	} else if (os_strncmp(buf, "IGNORECAC ", 10) == 0) {
		if (hostapd_ctrl_iface_ignorecac_extn(hapd, buf + 10))
			reply_len_extn = -1;
#endif /* CONFIG_QCA_LAB_TEST_FEATURES */
#ifdef CONFIG_IEEE80211AC
	} else if (os_strncmp(buf, "MU_CAP_WAR ", 11) == 0) {
		if (hostapd_ctrl_iface_mu_cap_war_extn(&hapd->hapd_extn, buf + 11))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_MU_CAP_WAR") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_mu_cap_war_extn(&hapd->hapd_extn, reply,
								      reply_size);
#endif /* CONFIG_IEEE80211AC */
	} else if (os_strncmp(buf, "DFS_NO_WRADAR ", 14) == 0) {
		if (hostapd_ctrl_iface_dfs_no_wradar_extn(hapd, buf + 14))
			reply_len_extn = -1;
		else
			reply_len_extn = os_snprintf(reply, reply_size, "OK\n");
	} else if (os_strcmp(buf, "GET_DFS_NO_WRADAR") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_dfs_no_wradar_extn(
			hapd, reply, reply_size);
	} else if (os_strncmp(buf, "DCS ", 4) == 0) {
		reply_len_extn = hostapd_ctrl_iface_dcs_extn(hapd, buf + 4, reply,
							     reply_size);
#ifdef CONFIG_TESTING_OPTIONS
	} else if (os_strcmp(buf, "SYNC_IFACE_FREQ") == 0) {
		if (hostapd_ctrl_iface_sync_iface_freq_extn(hapd))
			reply_len_extn = -1;
#endif /* CONFIG_TESTING_OPTIONS */
	} else if (os_strncmp(buf, "SET_VLP_NON_PRIOR_PENALTY ", 26) == 0) {
		if (hostapd_ctrl_iface_set_non_prior_penalty_extn(hapd, buf + 26))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_VLP_NON_PRIOR_PENALTY") == 0) {
		reply_len_extn =
			hostapd_ctrl_iface_get_non_prior_penalty_extn(hapd, reply,
								      reply_size);
	} else if (os_strncmp(buf, "SET_PRIMARY_CHANS ", 18) == 0) {
		/*
		 * SET_PRIMARY_CHANS <ch1> [<ch2> ...]
		 * Restrict ACS/DFS/scan to the listed primary channels.
		 * An empty argument list clears the restriction and reverts
		 * to the full regulatory channel set (ChanSel-001..003,011).
		 */
		if (hostapd_set_primary_chanlist(hapd, buf + 18) < 0)
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_PRIMARY_CHANS") == 0) {
		/*
		 * GET_PRIMARY_CHANS
		 * Returns the currently configured primary channel list.
		 */
		reply_len_extn = hostapd_get_primary_chanlist(hapd->iface,
							      reply, reply_size);
		if (reply_len_extn < 0)
			reply_len_extn = -1;
	} else if (os_strncmp(buf, "SET_OBSS_SNR_THRESHOLD ", 23) == 0) {
		if (hostapd_ctrl_iface_set_obss_snr_threshold_extn(hapd,
								   buf + 23))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_OBSS_SNR_THRESHOLD") == 0) {
		reply_len_extn =
			hostapd_ctrl_iface_get_obss_snr_threshold_extn(hapd,
									reply,
									reply_size);
	} else if (os_strncmp(buf, "SET_OBSS_RX_SNR_THRESHOLD ", 26) == 0) {
		if (hostapd_ctrl_iface_set_obss_rx_snr_threshold_extn(hapd,
								      buf + 26))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_OBSS_RX_SNR_THRESHOLD") == 0) {
		reply_len_extn =
			hostapd_ctrl_iface_get_obss_rx_snr_threshold_extn(hapd,
									   reply,
									   reply_size);
	} else if (os_strncmp(buf, "COUNTRY_IE ", 11) == 0) {
		if (hostapd_ctrl_iface_country_ie_extn(hapd, buf + 11))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_COUNTRY_IE") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_country_ie_extn(hapd,
								      reply,
								      reply_size);
	} else if (os_strncmp(buf, "HT40INTOL ", 10) == 0) {
		if (hostapd_ctrl_iface_set_ht40intol_extn(hapd, buf + 10))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_HT40INTOL") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_ht40intol_extn(hapd, reply,
								       reply_size);
	} else if (os_strncmp(buf, "SET_EHT_CONFIG_CCFS0 ", 21) == 0) {
		if (hostapd_ctrl_iface_set_eht_config_ccfs0_extn(hapd, buf + 20))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_EHT_CONFIG_CCFS0") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_eht_config_ccfs0_extn(hapd,
									      reply,
									      reply_size);
	} else if (os_strncmp(buf, "SET_TPE_COMMON_PSD ", 19) == 0) {
		if (hostapd_ctrl_iface_set_tpe_common_psd_extn(hapd, buf + 19))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_TPE_COMMON_PSD") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_tpe_common_psd_extn(hapd,
									    reply,
									    reply_size);
	} else if (os_strncmp(buf, "SET_AUTORECOVERY_AFTER_NOL_VAPDOWN ", 35) == 0) {
		if (hostapd_ctrl_iface_set_autorecovery_after_nol_vapdown_extn(hapd,
									       buf + 35))
			reply_len_extn = -1;
	} else if (os_strncmp(buf, "SET_TPE_TX_PWR_INTERP ", 22) == 0) {
		if (hostapd_ctrl_iface_set_tpe_tx_pwr_interp_extn(hapd, buf + 22))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_TPE_TX_PWR_INTERP") == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_tpe_tx_pwr_interp_extn(hapd,
									       reply,
									       reply_size);
	} else if (os_strncmp(buf, "SET_TPE_PUNCT_CHANNEL_TX_PWR ", 29) == 0) {
		if (hostapd_ctrl_iface_set_tpe_punct_channel_tx_pwr_extn(hapd, buf + 22))
			reply_len_extn = -1;
	} else if (os_strcmp(buf, "GET_TPE_PUNCT_CHANNEL_TX_PWR") == 0) {
		reply_len_extn =
			hostapd_ctrl_iface_get_tpe_punct_channel_tx_pwr_extn(hapd,
									     reply,
									     reply_size);
        } else {
		return -1;
	}

	*reply_len = reply_len_extn;

	if (*reply_len < 0) {
		os_memcpy(reply, "FAIL\n", 5);
		*reply_len = 5;
	}

	return 0;
}

int hostapd_set_nontx_optional_vendor_elem_size_extn(struct hostapd_bss_config *conf,
						     char *value)
{
	char *end;
	unsigned long v;
	u8 optional_elem_size, vendor_elem_size;

	v = strtoul(value, &end, 0); /* accepts 0x-prefixed hex or decimal */
	if (end == value || *end != '\0') {
		wpa_printf(MSG_ERROR, "CTRL: nontx_profile_ie_size: invalid value '%s'", value);
		return -1;
	}

	if (v > 0xFFFF) {
		wpa_printf(MSG_ERROR, "CTRL: nontx_profile_ie_size: out of range '%s'", value);
		return -1;
	}

	optional_elem_size = (v >> 8) & 0xFF;
	vendor_elem_size = v & 0xFF;

	wpa_printf(MSG_INFO,
		   "CTRL: Optional elem size: %u max limit: %d vendor elem size: %u max limit: %d",
		   optional_elem_size, MBSSID_NON_TX_DEF_OPTIONAL_ELEM_SIZE, vendor_elem_size,
		   MBSSID_NON_TX_DEF_VENDOR_ELEM_SIZE);

	if (optional_elem_size > MBSSID_NON_TX_DEF_OPTIONAL_ELEM_SIZE) {
		wpa_printf(MSG_ERROR, "CTRL: Optional elem size %u bytes exceeds max limit %d",
			   optional_elem_size, MBSSID_NON_TX_DEF_OPTIONAL_ELEM_SIZE);
		return -1;
	}

	if (vendor_elem_size > MBSSID_NON_TX_DEF_VENDOR_ELEM_SIZE) {
		wpa_printf(MSG_ERROR, "CTRL: Vendor elem size %u bytes exceeds max limit %d",
			   vendor_elem_size, MBSSID_NON_TX_DEF_VENDOR_ELEM_SIZE);
		return -1;
	}

	conf->bss_extn.nontx_optional_elem_size = optional_elem_size;
	conf->bss_extn.nontx_vendor_elem_size = vendor_elem_size;

	return 0;
}

int hostapd_ctrl_iface_set_extn(struct hostapd_data *hapd, char *cmd, char *value)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	int val, ret;

	if (os_strcasecmp(cmd, "rnr_member_ess_colocated_en") == 0) {
		val = atoi(value);
		if (val < 0 || val > 1) {
			wpa_printf(MSG_ERROR,
				"rnr_member_ess_colocated_en: Invalid value (expected 0 or 1)");
			return -1;
		}
		if (is_6ghz_freq(hapd->iface->freq)) {
			conf_extn->rnr_ess_colocated_en = val;
			ieee802_11_update_beacons(hapd->iface);
		} else {
			wpa_printf(MSG_ERROR, "rnr_member_ess_colocated_en is valid only for 6 GHz");
			return -1;
		}

	} else if (os_strcasecmp(cmd, "rnr_6ghz_override") == 0) {
		val = atoi(value);
		if (val < 0 || val > 1) {
			wpa_printf(MSG_ERROR, "rnr_6ghz_override: Invalid value (expected 0 or 1)");
			return -1;
		}
		if (!is_6ghz_freq(hapd->iface->freq)) {
			wpa_printf(MSG_ERROR, "rnr_6ghz_override is valid only for 6 GHz");
			return -1;
		}

		conf_extn->rnr_6ghz_override = val;

		ret = ieee802_11_update_beacons(hapd->iface);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "Failed to update beacon");
			return -1;
		}

	} else if (os_strcasecmp(cmd, "nontx_profile_elem_size") == 0) {
		ret = hostapd_set_nontx_optional_vendor_elem_size_extn(hapd->conf, value);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "Failed to set nontx_profile_elem_size");
			return -1;
		}
		return ret;
	}

	return 0;
}

int hostapd_ctrl_iface_get_extn(struct hostapd_data *hapd, char *cmd,
				char *buf, size_t buflen)
{
	int res;

	if (os_strcasecmp(cmd, "nontx_profile_elem_size") == 0) {
		res = os_snprintf(buf, buflen, "Optional elem size = %u\nVendor elem size = %u\n",
				  hapd->conf->bss_extn.nontx_optional_elem_size,
				  hapd->conf->bss_extn.nontx_vendor_elem_size);
		if (os_snprintf_error(buflen, res))
			return -1;
		return res;
	}

	return -1;
}


int hostapd_ctrl_iface_status_extn(struct hostapd_data *hapd, char *buf,
				   size_t buflen, size_t curr_len)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	size_t len = curr_len;
	int ret;

	if (is_6ghz_freq(hapd->iface->freq)) {
		ret = os_snprintf(buf + len, buflen - len,
				"rnr_member_ess_colocated_en=%d\n",
				conf_extn->rnr_ess_colocated_en);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;

		ret = os_snprintf(buf + len, buflen - len,
				"rnr_6ghz_override=%d\n",
				conf_extn->rnr_6ghz_override);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;
	}

	return len;
}

#ifdef HOSTAPD
bool hostapd_5ghz_eht_320_channel_bw_extn(struct hostapd_hw_modes *mode,
					  int channel_idx)
{
	static const int allowed[] = { 100, 104, 108, 112, 116, 120,
				       124, 128, 132, 136, 140, 144 };
	struct hostapd_channel_data *chan = &mode->channels[channel_idx];
	bool is_allowed_primary = false;
	size_t k;

	if (!(chan->allowed_bw & HOSTAPD_CHAN_WIDTH_320))
		return false;

	for (k = 0; k < ARRAY_SIZE(allowed); k++) {
		int c = allowed[k];
		int idx;

		if (chan->chan == c)
			is_allowed_primary = true;

		idx = hostapd_get_channel_idx(mode, c);
		if (idx == -1 ||
		    (mode->channels[idx].flag & HOSTAPD_CHAN_DISABLED))
			return false;
	}

	return is_allowed_primary;
}
#endif /* HOSTAPD */
