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
#include "ap/ieee802_11.h"
#include "dcs.h"

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
	conf_extn->qacs_conf.rep_txpower_policy = 0;/* Option pwr disabled */
	conf_extn->qacs_conf.rank_en = 1;           /* rank enabled */
	conf_extn->qacs_conf.min_dwell = 50;        /* msec */
	conf_extn->qacs_conf.max_dwell = 250;       /* msec */
	conf_extn->qacs_conf.dwelltime = 200;   /* msec */
        conf_extn->qacs_conf.dbg_module_bitmap = 0x00; /* bitmap of QACS debug modules id */
	conf_extn->qacs_conf.dbg_level = 0; /* QACS debug level */

	/* DCS defaults: initialize values; valid_mask reflects only user overrides */
	os_memset(&conf_extn->dcs_conf, 0, sizeof(conf_extn->dcs_conf));
	conf_extn->dcs_conf.intr_detection_threshold = DCS_INTR_DETECTION_THR;
	conf_extn->dcs_conf.phyerr_penalty = DCS_PHYERR_PENALTY;
	conf_extn->dcs_conf.phyerr_threshold = DCS_PHYERR_THRESHOLD;
	conf_extn->dcs_conf.radarerr_threshold = DCS_RADARERR_THRESHOLD;
	conf_extn->dcs_conf.txerr_threshold = DCS_TXERR_THRESHOLD;
	conf_extn->dcs_conf.sample_size = DCS_SAMPLE_SIZE;
	conf_extn->dcs_conf.coch_intr_threshold = DCS_COCH_INTR_THRESHOLD;
	conf_extn->dcs_conf.user_max_cu = DCS_USER_MAX_CU;

	conf_extn->dcs_conf.enable_bitmap = 0;   /* DCS disabled */
	/*
	 * Enable random channel selection for AWGN by default; users can
	 * explicitly disable random channel selection via hostapd.conf/CLI by
	 * setting the bitmap to 0.
	 */
	conf_extn->dcs_conf.dcs_random_chan_bitmap = DCS_AWGN_INTF;
}

void
hostapd_config_defaults_bss_extn(struct hostapd_bss_config *bss)
{
	struct hostapd_bss_config_extn *bss_extn = &bss->bss_extn;

	/* Configure bss defaults for extensions */
	bss_extn->nontx_optional_elem_size =
		MBSSID_NON_TX_DEF_OPTIONAL_ELEM_SIZE;
	bss_extn->nontx_vendor_elem_size =
		MBSSID_NON_TX_DEF_VENDOR_ELEM_SIZE;
}

int
hostapd_config_fill_extn(struct hostapd_config *conf,
			 struct hostapd_bss_config *bss,
			 const char *buf, char *pos, int line)
{
	struct hostapd_config_extn *conf_extn = &conf->conf_extn;
	int val;

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
		/* Values are sanitized and set in hostapd_ctrl_iface_set_extn */
		return 0;
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
			val = 0;
		conf_extn->qacs_conf.rep_txpower_policy = val;
	} else if (os_strcmp(buf, "acs_rank_en") == 0) {
		conf_extn->qacs_conf.rank_en = atoi(pos);

	} else if (os_strcmp(buf, "acs_dbgtrace") == 0) {
		/* Expected format: <value>
		 * Lower 0x00FF bits -> debug level
		 * Upper 0xFF00 bits -> module bitmap
		 * Example: "0x0201" means module_bitmap=0x02, dbg_level=0x01
		 */
		char *endptr;
		unsigned long val = strtoul(pos, &endptr, 0);
		if (*endptr) {
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

	} else if (os_strcmp(buf, "dcs_enable") == 0) {
		char *endptr;
		unsigned long v;

		while (*pos == ' ' || *pos == '\t')
			pos++;

		errno = 0;
		v = strtoul(pos, &endptr, 0);
		while (*endptr == ' ' || *endptr == '\t')
			endptr++;
		if (errno != 0 || endptr == pos || *endptr != '\0' ||
		    v > 0xFFFF) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid value for dcs_enable '%s' (expected 16-bit value)",
				   line, pos);
			conf_extn->dcs_conf.enable_bitmap = 0;
			return 0;
		}

		if (v & ~ALLOWED_DCS_MASK) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid value for dcs_enable '%s' (allowed bits mask: 0x%04x)",
				   line, pos, ALLOWED_DCS_MASK);
			conf_extn->dcs_conf.enable_bitmap = 0;
			return 0;
		}

		conf_extn->dcs_conf.enable_bitmap = (u16) v;
	} else if (os_strcmp(buf, "dcs_random_chan_bitmap") == 0) {
		/*
		 * Parse and set DCS random channel enable bitmap from hostapd.conf.
		 * Accept numeric values (decimal or hex like 0x1F). Validate range
		 * and allowed bits (0..4 i.e., 0x001F) to align with CLI handling.
		 */
		unsigned long tmp;
		char *endptr = NULL;
		u16 val;

		errno = 0;
		tmp = strtoul(pos, &endptr, 0);
		if (errno != 0 || endptr == pos) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid dcs random_chan_bitmap value '%s'",
				   line, pos);
			return -1;
		}

		while (endptr && *endptr == ' ')
			endptr++;

		if (endptr && *endptr != '\0') {
			wpa_printf(MSG_ERROR,
				   "Line %d: trailing characters in dcs random_chan_bitmap '%s'",
				   line, pos);
			return -1;
		}

		if (tmp > 0xFFFFUL) {
			wpa_printf(MSG_ERROR,
				   "Line %d: dcs random_chan_bitmap out of range '%s'",
				   line, pos);
			return -1;
		}

		val = (u16) tmp;
		/* Allow only bits 0..4; update mask if needed. */
		if (val & ~0x001Fu) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid dcs random_chan_bitmap 0x%04x (only bits 0..4 allowed)",
				   line, val);
			return -1;
		}

		conf_extn->dcs_conf.dcs_random_chan_bitmap = val;
		wpa_printf(MSG_DEBUG,
			   "DCS: dcs_random_chan_bitmap set to 0x%04x (%u)",
			   conf_extn->dcs_conf.dcs_random_chan_bitmap,
			   conf_extn->dcs_conf.dcs_random_chan_bitmap);
	} else if (os_strcmp(buf, "vap_submode") == 0) {
		u8 val = atoi(pos);
		if (val > QCA_WLAN_VENDOR_ATTR_VAP_SUBMODE_MAX) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid vap_submode: %d",
				   line, val);
			return 1;
		}
		bss->bss_extn.vap_submode = val;
	} else {
		return -1;
	}

	return 0;
}
