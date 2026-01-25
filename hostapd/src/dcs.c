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

