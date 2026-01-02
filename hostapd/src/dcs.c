// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include <netlink/genl/genl.h>
#include "utils/common.h"
#include "common/qca-vendor.h"
#include "drivers/driver.h"
#include "drivers/driver_nl80211.h"
#include "common/ieee802_11_common.h"
#include "ap/hostapd.h"
#include "ap/beacon.h"
#include "common/wpa_ctrl.h"
#include "cmn.h"
#include "dcs.h"

static int
dcs_print_usage_extn(char *reply, int reply_size)
{
	int ret;

	ret = os_snprintf(
		reply, reply_size,
		"dcs extn commands:\n"
		"  dcs enable           : set DCS configuration\n"
		);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

int hostapd_drv_dcs_config(struct hostapd_data *hapd, u8 link_id,
			   struct driver_dcs_config *params)
{
	if (!hapd->driver || !hapd->driver->dcs_config)
		return -1;

	return hapd->driver->dcs_config(hapd->drv_priv, link_id, params);
}

static int hostapd_ctrl_iface_dcs_config(struct hostapd_data *hapd,
					 const char *cmd, char *reply,
					 int reply_size)
{
	struct driver_dcs_config drv_dcs_conf;
	char *end;
	unsigned long v;

	(void) reply;
	(void) reply_size;

	if (!hapd || !hapd->iface || !cmd)
		return -1;

	while (*cmd == ' ')
		cmd++;
	if (*cmd == '\0') {
		wpa_printf(MSG_ERROR, "CTRL: DCS_ENABLE: empty value");
		return -1;
	}

	errno = 0;
	v = strtoul(cmd, &end, 0);
	while (*end == ' ')
		end++;
	if (errno != 0 || end == cmd || *end != '\0' || v > 0xFFFF) {
		wpa_printf(MSG_ERROR, "CTRL: DCS_ENABLE: invalid value '%s'", cmd);
		return -1;
	}

	if (v & ~ALLOWED_DCS_MASK) {
		wpa_printf(MSG_ERROR,
			   "CTRL: DCS_ENABLE: invalid value 0x%lx (allowed bits: 0(CW IM), 1(WLAN IM), 4(OBSS IM))",
			   v);
		return -1;
	}

	drv_dcs_conf.dcs_enable = (u16) v;
	drv_dcs_conf.cmd_type = SET_DCS_CONFIG;

	return hostapd_drv_dcs_config(hapd, hapd->mld_link_id, &drv_dcs_conf);
}

int hostapd_ctrl_iface_dcs_extn(struct hostapd_data *hapd,
				const char *cmd, char *reply,
				int reply_size)
{
	if (os_strncmp(cmd, "enable ", 7) == 0) {
		return hostapd_ctrl_iface_dcs_config(hapd, cmd + 7,
						     reply, reply_size);
	} else {
		return dcs_print_usage_extn(reply, reply_size);
	}
}

int find_random_channel(struct hostapd_iface *iface, int *new_freq,
			u32 freq, u32 cf1, u32 cf2, u32 chan_bw_interference_bitmap,
			enum chan_width chan_width, int *new_chan_width,
			int *new_centre_freq)
{
	int ret;
	u32 _rand;
	u32 chan_idx;
	unsigned int i;
	int temp_width;
	int current_start_freq;
	int num_available_chandefs = 0;
	struct hostapd_channel_data *chan_data = NULL;
	struct hostapd_hw_modes *mode = iface->current_mode;
	int cw_interference_freqs[BW_INTERFERENCE_MAXBITS] = {};
	struct hostapd_channel_data **available_chandef_list = NULL;

	available_chandef_list = os_zalloc(sizeof(struct hostapd_channel_data *) *
					   mode->num_channels);

	if (!available_chandef_list) {
		wpa_printf(MSG_ERROR, "available_chandef_list memory allocation failed");
		return -1;
	}

	current_start_freq = (cf1 - channel_width_to_int(chan_width) / 2) + 10;
	for (i = 0; i < BW_INTERFERENCE_MAXBITS; i++) {
		if ((1 << i) & chan_bw_interference_bitmap) {
			wpa_printf(MSG_DEBUG,
				   "CW: found cw interference in frequency %d",
				   current_start_freq + (20 * i));
			cw_interference_freqs[i] = current_start_freq + (20 * i);
		}
	}

	temp_width = chan_width;

	while (temp_width > CHAN_WIDTH_20_NOHT) {
		num_available_chandefs =
			intf_awgn_find_channel_list(iface, temp_width,
						    &available_chandef_list,
						    cw_interference_freqs);
		if (num_available_chandefs > 0)
			break;
		temp_width = get_next_max_width(temp_width);
	}

	if (num_available_chandefs == 0) {
		wpa_printf(MSG_ERROR, "CW Intf: no available_chandefs");
		goto exit;
	}


	if (os_get_random((u8 *)&_rand, sizeof(_rand)) < 0) {
		wpa_printf(MSG_ERROR, "CW Intf: couldn't get random number");
		goto exit;
	}

	chan_idx = _rand % num_available_chandefs;

	chan_data = available_chandef_list[chan_idx];

	if (!chan_data) {
		wpa_printf(MSG_ERROR, "CW Intf: channel info not available for chan_idx : %d",
				chan_idx);
		goto exit;
	}

	*new_chan_width = temp_width;

	wpa_printf(MSG_DEBUG, "CW Interference: got random channel %d (%d)",
		   chan_data->freq, chan_data->chan);

	if (*new_chan_width > CHAN_WIDTH_20) {
		ret = get_centre_freq_6g(chan_data->chan, *new_chan_width,
					 new_centre_freq);
		if (ret) {
			wpa_printf(MSG_ERROR,
				   "CW : couldn't find centre freq for chan : %d"
				   " chan_width : %d", chan_data->chan, *new_chan_width);
			goto exit;
		}
	} else {
		*new_centre_freq = chan_data->freq;
	}

	*new_freq = chan_data->freq;

	return 0;

exit:
	os_free(available_chandef_list);
	return -1;
}

int hostapd_dcs_channel_change(struct csa_settings *settings,
                               struct hostapd_iface *iface, int new_chan_width,
                               int new_centre_freq)
{
	int i, ret = 0;

	settings->cs_count = 5;

	switch (new_chan_width) {
	case CHAN_WIDTH_40:
		settings->freq_params.bandwidth = 40;
		break;
	case CHAN_WIDTH_80P80:
	case CHAN_WIDTH_80:
		settings->freq_params.bandwidth = 80;
		break;
	case CHAN_WIDTH_160:
		settings->freq_params.bandwidth = 160;
		break;
	case CHAN_WIDTH_320:
		settings->freq_params.bandwidth = 320;
		break;
	default:
		settings->freq_params.bandwidth = 20;
		break;
	}

	settings->freq_params.center_freq1 = new_centre_freq;
	settings->freq_params.ht_enabled = iface->conf->ieee80211n;
	settings->freq_params.vht_enabled = iface->conf->ieee80211ac;
	settings->freq_params.he_enabled = iface->conf->ieee80211ax;
	settings->freq_params.eht_enabled= iface->conf->ieee80211be;
	settings->power_mode = -1;

	if (is_6ghz_freq(settings->freq_params.freq) &&
			 iface->conf->enable_best_power_mode) {
		int best_power_mode;

		best_power_mode =
			hostapd_get_best_ap_6ghz_power_mode(iface,
					settings->freq_params.freq,
					settings->freq_params.center_freq1,
					settings->freq_params.bandwidth,
					settings->freq_params.punct_bitmap);
		if (best_power_mode != NL80211_REG_NUM_POWER_MODES) {
			settings->power_mode = best_power_mode;
			wpa_printf(MSG_DEBUG, "%s: Best power mode for Freq %d is %d",
					__func__,
					settings->freq_params.freq,
					settings->power_mode);
		} else {
			wpa_printf(MSG_DEBUG, "%s: Failed to get BPM for Freq %d",
					__func__, settings->freq_params.freq);
			return -1;
		}
	}

	for (i = 0; i < iface->num_bss; i++) {
		hostapd_chan_switch_config(iface->bss[i],
				&settings->freq_params);

		wpa_printf(MSG_DEBUG,
			   "channel=%u, freq=%d, bw=%d, center_freq1=%d puncture_bitmap=0x%x",
			   settings->freq_params.channel,
			   settings->freq_params.freq,
			   settings->freq_params.bandwidth,
			   settings->freq_params.center_freq1,
			   settings->freq_params.punct_bitmap);

		ret = hostapd_switch_channel(iface->bss[i], settings);
	}

	return ret;
}

void hostapd_dcs_intf_event_extn(struct hostapd_data *hapd,
                                union wpa_event_data *data)
{
	enum chan_width ch_width;
	u32 freq, cf1, cf2, intf_bitmap;
	struct csa_settings settings = {};
	int new_chan_width, new_centre_freq, new_freq, ret;

	freq = hapd->iface->freq;
	cf1 = hapd->iface->conf->conf_extn.cur_chan_params.cf1;
	cf2 = hapd->iface->conf->conf_extn.cur_chan_params.cf2;
	ch_width = hapd->iface->conf->conf_extn.cur_chan_params.chan_width;

	intf_bitmap = DCS_SEG_PRI20;

	ret = find_random_channel(hapd->iface, &new_freq, freq, cf1, cf2, intf_bitmap, ch_width, &new_chan_width, &new_centre_freq);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "finding random channel failed, dropping event");
		return;
	}

	settings.freq_params.freq = new_freq;
	settings.freq_params.punct_bitmap = intf_bitmap;

	wpa_printf(MSG_DEBUG, "input freq=%d, ch_width=%d, cf1=%d cf2=%d intf_bitmap:0x%x", freq, ch_width, cf1, cf2, intf_bitmap);

	ret = hostapd_dcs_channel_change(&settings, hapd->iface, new_chan_width, new_centre_freq);
	return;
}

void update_chan_params(struct hostapd_data *hapd, int cf1, int cf2, enum chan_width chwidth)
{
	hapd->iface->conf->conf_extn.cur_chan_params.cf1 = cf1;
        hapd->iface->conf->conf_extn.cur_chan_params.cf2 = cf2;
        hapd->iface->conf->conf_extn.cur_chan_params.chan_width = chwidth;
}
