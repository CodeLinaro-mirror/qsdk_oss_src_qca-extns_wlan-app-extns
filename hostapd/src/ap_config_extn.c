// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include <math.h>

#include "utils/common.h"
#include "utils/list.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "drivers/driver.h"
#include "ap/hostapd.h"
#include "ap/ap_drv_ops.h"
#include "common/ieee802_11_defs.h"
#include "ap/ap_config.h"
#include "cmn.h"

void
hostapd_config_defaults_extn(struct hostapd_config *conf)
{
	struct hostapd_config_extn *conf_extn = &conf->conf_extn;

	/* Configure defaults for extensions */
	conf_extn->rnr_6ghz_colocated_enable = 0;
	conf_extn->rnr_ess_colocated_en = false;
	conf_extn->rnr_6ghz_override = true;

	/* Repeater defaults */
	conf_extn->skip_cac = 0;
	conf_extn->ind_rptr = 0;

	/*configure qacs_default here*/
	conf_extn->qacs_enable = 0;                 /* QACS disabled */
	conf_extn->qacs_conf.wradar = 1;            /* wradar reject enabled */
	conf_extn->qacs_conf.rep_txpower_policy = 1;/* Option pwr Tput */
	conf_extn->qacs_conf.rank_en = 1;           /* rank enabled */
	conf_extn->qacs_conf.min_dwell = 50;        /* msec */
	conf_extn->qacs_conf.max_dwell = 250;       /* msec */
	conf_extn->qacs_conf.dwelltime = 200;   /* msec */
        conf_extn->qacs_conf.dbg_module_bitmap = 0x0004; /* QACS_MODULE_ID_SELECTOR */
	conf_extn->qacs_conf.dbg_level = 2; /* QACS_DEBUG_LEVEL_DEFAULT */
}

void
hostapd_config_defaults_bss_extn(struct hostapd_bss_config *bss)
{
	struct hostapd_bss_config_extn *bss_extn = &bss->bss_extn;

	/* Configure bss defaults for extensions */
	bss_extn->nontx_optional_elem_size =
		MBSSID_NONTX_OPTIONAL_ELEM_SIZE;
	bss_extn->nontx_vendor_elem_size =
		MBSSID_NONTX_VENDOR_ELEM_SIZE;
}

int
hostapd_config_fill_extn(struct hostapd_config *conf,
			 struct hostapd_bss_config *bss,
			 const char *buf, char *pos, int line)
{
	struct hostapd_config_extn *conf_extn = &conf->conf_extn;
	int val, ret;

	if (!conf_extn)
		return -1;

	if (os_strcmp(buf, "rnr_member_ess_colocated_en") == 0) {
		val = atoi(pos);
		if (val != 0 && val != 1) {
			wpa_printf(MSG_ERROR, "Line %d: invalid value for rnr_member_ess_colocated_en %d (expected 0 or 1)",
				line, val);
			return -1;
		}
		conf_extn->rnr_ess_colocated_en = val;
	} else if (os_strcmp(buf, "rnr_6ghz_override") == 0) {
		val = atoi(pos);
		if (val != 0 && val != 1) {
			wpa_printf(MSG_ERROR, "Line %d: invalid value for rnr_6ghz_override %d (expected 0 or 1)", line, val);
			return -1;
		}
		conf_extn->rnr_6ghz_override = val;
	} else if (os_strcmp(buf, "athnewind") == 0) {
		conf_extn->ind_rptr = atoi(pos);
		return 0;
	} else if (os_strcmp(buf, "skip_cac") == 0) {
		conf_extn->skip_cac = atoi(pos);
		return 0;
	} else if (os_strcmp(buf, "uplink_csa") == 0) {
		conf_extn->uplink_csa = atoi(pos);
		return 0;
	} else if (os_strcmp(buf, "qacs_enable") == 0) {
		conf_extn->qacs_enable = atoi(pos);
	} else if (os_strcasecmp(buf, "nontx_profile_elem_size") == 0) {
		ret = hostapd_set_nontx_optional_vendor_elem_size_extn(bss, pos);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "Failed to set nontx_profile_elem_size");
			return -1;
		}
		return ret;
	} else if (os_strcmp(buf, "repurpose_mode") == 0) {
		int mode = atoi(pos);

		if (!hostapd_is_valid_repurpose_mode_extn(mode)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid repurpose_mode %d (allowed 1..3)",
				   line, mode);
			return -1;
		}
		bss->bss_extn.repurpose_mode = (u8) mode;
	} else if (os_strcmp(buf, "acs_wradar") == 0) {
		conf_extn->qacs_conf.wradar = atoi(pos);
	} else if (os_strcmp(buf, "acs_txpwr_opt") == 0) {
		int val = atoi(pos);
		if (val != 1 && val != 2)
			val = 1;
		conf_extn->qacs_conf.rep_txpower_policy = val;
	} else if (os_strcmp(buf, "acs_rank_en") == 0) {
		conf_extn->qacs_conf.rank_en = atoi(pos);

	} else if (os_strcmp(buf, "acs_dbgtrace") == 0) {
		/* Expected format: <value>
		 * Lower 0x00FF bits -> debug level
		 * Upper 0xFF00 bits -> module bitmap
		 * Example: "0x0201" means module_bitmap=0x02, dbg_level=0x01
		 */
		char *endptr = NULL;
		unsigned long val = strtoul(pos, &endptr, 0);
		if (endptr == pos) {
			wpa_printf(MSG_ERROR, "%s: Invalid acs_dbgtrace value '%s' (0xFF00=module mask, 0x00FF=debug level)", __func__, pos);
			return -1;
		}
		conf_extn->qacs_conf.dbg_module_bitmap = (u_int16_t)((val & 0xFF00) >> 8);
		conf_extn->qacs_conf.dbg_level = (int)(val & 0x00FF);

	} else if (os_strcmp(buf, "acsmin_dwell") == 0) {
		conf_extn->qacs_conf.min_dwell = atoi(pos);
		if (conf_extn->qacs_conf.min_dwell < 50)
			conf_extn->qacs_conf.min_dwell = 50;
	} else if (os_strcmp(buf, "acsmax_dwell") == 0) {
		conf_extn->qacs_conf.max_dwell = atoi(pos);
	} else if (os_strcmp(buf, "dwelltime") == 0) {
		int dt = atoi(pos);
		/* Enforce dwelltime bounds: must be between min_dwell and max_dwell */
		if (conf_extn->qacs_conf.min_dwell &&
		    dt <= conf_extn->qacs_conf.min_dwell)
			dt = conf_extn->qacs_conf.min_dwell;
		if (conf_extn->qacs_conf.max_dwell &&
		    dt >= conf_extn->qacs_conf.max_dwell)
			dt = conf_extn->qacs_conf.max_dwell;

		conf_extn->qacs_conf.dwelltime = dt;

	} else {
		return -1;
	}

	return 0;
}
