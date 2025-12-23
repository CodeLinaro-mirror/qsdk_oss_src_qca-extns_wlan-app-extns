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
}

int
hostapd_config_fill_extn(struct hostapd_config *conf,
			 struct hostapd_bss_config *bss,
			 const char *buf, char *pos, int line)
{
	struct hostapd_config_extn *conf_extn = &conf->conf_extn;

	if (!conf_extn)
		return -1;
/*
	if (os_strcmp(buf, "temp_enable") == 0)
		conf_extn->temp_enable = atoi(pos);
	else

 * Return -1 if no extension configuration is parsed.
 * This allows the parent API hostapd_config_fill to continue
 * processing its logic.
 */
	return -1;

/*
 * Return 0 if an extension configuration is successfully handled.
 * In this case, the parent API hostapd_config_fill will return immediately.

	return 0;
 */
}
