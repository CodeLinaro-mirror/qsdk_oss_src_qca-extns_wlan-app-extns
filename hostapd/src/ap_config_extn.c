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
}

int
hostapd_config_fill_extn(struct hostapd_config *conf,
			 struct hostapd_bss_config *bss,
			 const char *buf, char *pos, int line)
{
	struct hostapd_config_extn *conf_extn = &conf->conf_extn;

	if (os_strcmp(buf, "rnr_member_ess_colocated_en") == 0) {
		int val = atoi(pos);
		if (val != 0 && val != 1) {
			wpa_printf(MSG_ERROR, "Line %d: invalid value for rnr_member_ess_colocated_en %d (expected 0 or 1)",
				line, val);
			return -1;
		}
		conf_extn->rnr_ess_colocated_en = val;
	} else
		return -1;

	return 0;
}
