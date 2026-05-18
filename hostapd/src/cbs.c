// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "utils/os.h"
#include "common/ieee802_11_defs.h"
#include "ap/ap_config.h"
#include "common/hw_features_common.h"
#include "common/wpa_ctrl.h"
#include "drivers/driver.h"
#include "ap/ap_drv_ops.h"
#include "ap/acs.h"
#include "ap/hw_features.h"
#include "utils/eloop.h"
#include "cbs.h"

static int
cbs_print_usage_extn(char *reply, int reply_size)
{
	int ret;

	ret = os_snprintf(
		reply, reply_size,
		"cbs commands:\n"
		"  enable <0|1|2>       : enable/disable cbs scan (0:disable | 1:enable CBS scan once | 2:enable cbs scan to run continuously)\n"
		"  g_enable             : get continuous background scan enable state\n"
		"  resttime <ms>        : set rest time in milliseconds\n"
		"  g_resttime           : get rest time in milliseconds\n"
		"  dwellrest <ms>       : set dwell rest time in milliseconds\n"
		"  g_dwellrest          : get dwell rest time in milliseconds\n"
		"  waittime <ms>        : set wait time in milliseconds\n"
		"  g_waittime           : get wait time in milliseconds\n"
		"  dwellsplit <value>   : set dwell split value\n"
		"  g_dwellsplit         : get dwell split value\n"
		"  totaldwell <value>   : set total dwell value\n"
		"  g_totaldwell         : get total dwell value\n"
		"  csa <1|0>            : enable/disable CSA for CBS\n"
		"  g_csa                : get CSA state for CBS\n"
                );

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_get_enable(struct hostapd_config_extn *conf_extn,
					const char *pos,
					char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS enable: %d\n",
			      conf_extn->cbs_params.cbs_enable);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_set_enable(struct hostapd_data *hapd,
				  struct hostapd_config_extn *conf_extn,
				  const char *pos, char *reply,
				  size_t reply_size)
{
	int val = atoi(pos);
	int acs_ch_list_all = 0;
	int *freq_list = NULL;
	int ret;
	struct hostapd_hw_modes *mode;
	struct cbs_params_extn *cbs_params = &conf_extn->cbs_params;

	if (!(val == 0 || val == 1 || val == 2)) {
		wpa_printf(MSG_ERROR, "CBS: Invalid input: %d", val);
		return -1;
	}

	if (!hapd->driver || !hapd->driver->set_cbs)
		return -1;

	if (!val) {
		cbs_params->cbs_enable = 0;
		return hapd->driver->set_cbs(hapd->drv_priv,
					     cbs_params, NULL,
					     hapd->mld_link_id);
	}

	if (!hapd->iface->current_mode)
		return -1;

	mode = hapd->iface->current_mode;

	/*
	 * If no chanlist config parameter is provided, include all
	 * enabled channels of the selected hw_mode.
	 */
	if (hapd->iface->conf->acs_freq_list_present)
		acs_ch_list_all = !hapd->iface->conf->acs_freq_list.num;
	else
		acs_ch_list_all = !hapd->iface->conf->acs_ch_list.num;

	hostapd_get_hw_mode_any_channels(hapd, mode,
					 acs_ch_list_all,
					 false, &freq_list);
	if (!freq_list) {
		wpa_printf(MSG_ERROR, "CBS: freq_list is empty. Failing CBS trigger.");
		return -1;
	}

	cbs_params->cbs_enable = val;
	cbs_params->best_chan = NULL;
	acs_cleanup(hapd->iface);
	qacs_reset_scan_stats(hapd->iface, mode);

	ret = hapd->driver->set_cbs(hapd->drv_priv,
				    cbs_params, freq_list,
				    hapd->mld_link_id);
	if (ret)
		cbs_params->cbs_enable = 0;

	os_free(freq_list);
	return ret;
}

static int hostapd_cbs_set_resttime(struct hostapd_config_extn *conf_extn,
				    const char *pos, char *reply, size_t reply_size)
{
	int val = atoi(pos);

	if (val >= 0) {
		conf_extn->cbs_params.resttime = val;
		return 0;
	}

	return -1;
}

static int hostapd_cbs_get_resttime(struct hostapd_config_extn *conf_extn,
				    const char *pos, char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS resttime: %d\n",
			      conf_extn->cbs_params.resttime);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_set_dwellrest(struct hostapd_config_extn *conf_extn,
				     const char *pos, char *reply, size_t reply_size)
{
	int val = atoi(pos);

	if (val >= 0) {
		conf_extn->cbs_params.dwellrest = val;
		return 0;
	}

	return -1;
}

static int hostapd_cbs_get_dwellrest(struct hostapd_config_extn *conf_extn,
				     const char *pos, char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS dwellrest: %d\n",
			      conf_extn->cbs_params.dwellrest);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_set_waittime(struct hostapd_config_extn *conf_extn,
				    const char *pos, char *reply, size_t reply_size)
{
	int val = atoi(pos);

	if (val >= 0) {
		conf_extn->cbs_params.waittime = val;
		return 0;
	}

	return -1;
}

static int hostapd_cbs_get_waittime(struct hostapd_config_extn *conf_extn,
				    const char *pos, char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS waittime: %d\n",
			      conf_extn->cbs_params.waittime);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_set_dwellsplit(struct hostapd_config_extn *conf_extn,
				   const char *pos, char *reply, size_t reply_size)
{
	int val = atoi(pos);

	if (val >= 0) {
		conf_extn->cbs_params.dwellsplit = val;
		return 0;
	}

	return -1;
}

static int hostapd_cbs_get_dwellsplit(struct hostapd_config_extn *conf_extn,
				   const char *pos, char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS dwellsplit: %d\n",
			      conf_extn->cbs_params.dwellsplit);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_set_totaldwell(struct hostapd_config_extn *conf_extn,
				      const char *pos, char *reply, size_t reply_size)
{
	int val = atoi(pos);

	if (val >= 0) {
		conf_extn->cbs_params.totaldwell = val;
		return 0;
	}

	return -1;
}

static int hostapd_cbs_get_totaldwell(struct hostapd_config_extn *conf_extn,
				      const char *pos, char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS totaldwell: %d\n",
			      conf_extn->cbs_params.totaldwell);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_cbs_set_csa(struct hostapd_config_extn *conf_extn,
			       const char *pos, char *reply, size_t reply_size)
{
	int val = atoi(pos);

	if (val == 0 || val == 1) {
		conf_extn->cbs_params.csa_enable = val;
		return 0;
	}

	return -1;
}

static int hostapd_cbs_get_csa(struct hostapd_config_extn *conf_extn,
			       const char *pos, char *reply, size_t reply_size)
{
	int ret = os_snprintf(reply, reply_size,
			      "CBS csa: %d\n",
			      conf_extn->cbs_params.csa_enable);

	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

int hostapd_cbs_handle_single_channel_survey(struct hostapd_iface *iface,
					     struct hostapd_channel_data *chan,
					     struct freq_survey *survey)
{
	if (iface->conf->conf_extn.cbs_params.cbs_enable) {
		dl_list_del(&survey->list);
		dl_list_add_tail(&chan->survey_list, &survey->list);
		hostapd_update_nf(iface, chan, survey);
		iface->chans_surveyed++;
		return 0;
	}

	return -1;
}

int hostapd_cbs_handle_scan_complete(struct hostapd_data *hapd,
				     union wpa_event_data *data)
{
	struct cbs_event *cbs_evt;
	struct hostapd_iface *iface = hapd->iface;
	struct hostapd_config_extn *conf_extn = &hapd->iface->conf->conf_extn;

	if (!data) {
		wpa_printf(MSG_ERROR, "WPA event with NULL data");
		return -1;
	}

	cbs_evt = &data->event_data_extn.scan_results_event.cbs_event;

	wpa_printf(MSG_DEBUG, "CBS scan complete: frequency=%u scan_complete=%u",
		   cbs_evt->scan_complete_freq, cbs_evt->status);

	if (!cbs_evt->scan_complete_freq)
		return -1;

	/* Pre processing of scan results per channel */
	if (cbs_evt->status == VENDOR_SCAN_STATUS_NEW_RESULTS ||
	    cbs_evt->status == VENDOR_SPLIT_SCAN_COMPLETE_PER_CHANNEL) {
		if (hostapd_drv_get_survey(hapd, cbs_evt->scan_complete_freq))
			wpa_printf(MSG_ERROR, "CBS: survey results failed");
		if (conf_extn->qacs_enable)
			acs_process_hostapd_scan_data_per_freq(
				iface, cbs_evt->scan_complete_freq);
	}

	if (cbs_evt->status == VENDOR_SCAN_STATUS_NEW_RESULTS) {
		if (conf_extn->qacs_enable) {
			conf_extn->cbs_params.best_chan =
				qacs_find_ideal_chan(iface);
		} else {
			acs_study_options(iface);
			conf_extn->cbs_params.best_chan =
				acs_find_ideal_chan(iface);
		}

		if (conf_extn->cbs_params.cbs_enable == 1)
			conf_extn->cbs_params.cbs_enable = 0;
	}

	return 0;
}


int hostapd_handle_cli_cbs_extn(struct hostapd_data *hapd,
				char *pos, char *buf,
				size_t buflen)
{
	struct hostapd_config_extn *conf_extn;

	if (!hapd->iface || !hapd->iface->conf)
		return -1;

	conf_extn  = &hapd->iface->conf->conf_extn;

	if (os_strncmp(pos, "g_enable", 8) == 0) {
		return hostapd_cbs_get_enable(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "enable ", 7) == 0) {
		return hostapd_cbs_set_enable(hapd,
					      conf_extn,
					      pos + 7, buf, buflen);

	} else if (os_strncmp(pos, "g_resttime", 10) == 0) {
		return hostapd_cbs_get_resttime(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "resttime ", 9) == 0) {
		return hostapd_cbs_set_resttime(conf_extn, pos + 9, buf, buflen);

	} else if (os_strncmp(pos, "g_dwellrest", 11) == 0) {
		return hostapd_cbs_get_dwellrest(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "dwellrest ", 10) == 0) {
		return hostapd_cbs_set_dwellrest(conf_extn, pos + 10, buf, buflen);

	} else if (os_strncmp(pos, "g_waittime", 10) == 0) {
		return hostapd_cbs_get_waittime(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "waittime ", 9) == 0) {
		return hostapd_cbs_set_waittime(conf_extn, pos + 9, buf, buflen);

	} else if (os_strncmp(pos, "g_dwellsplit", 12) == 0) {
		return hostapd_cbs_get_dwellsplit(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "dwellsplit ", 11) == 0) {
		return hostapd_cbs_set_dwellsplit(conf_extn, pos + 11, buf, buflen);

	} else if (os_strncmp(pos, "g_totaldwell", 12) == 0) {
		return hostapd_cbs_get_totaldwell(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "totaldwell ", 11) == 0) {
		return hostapd_cbs_set_totaldwell(conf_extn, pos + 11, buf, buflen);

	} else if (os_strncmp(pos, "g_csa", 5) == 0) {
		return hostapd_cbs_get_csa(conf_extn, pos, buf, buflen);

	} else if (os_strncmp(pos, "csa ", 4) == 0) {
		return hostapd_cbs_set_csa(conf_extn, pos + 4, buf, buflen);

	} else {
		return cbs_print_usage_extn(buf, buflen);
	}
}
