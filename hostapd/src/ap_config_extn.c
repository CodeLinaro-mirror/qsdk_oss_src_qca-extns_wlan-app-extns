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
	} else if (os_strcmp(buf, "qacs_enable") == 0) {
		conf_extn->qacs_enable = atoi(pos);
	} else if (os_strcasecmp(buf, "nontx_profile_elem_size") == 0) {
		ret = hostapd_set_nontx_optional_vendor_elem_size_extn(bss, pos);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "Failed to set nontx_profile_elem_size");
			return -1;
		}
		return ret;
	} else {
		return -1;
	}

	return 0;
}
