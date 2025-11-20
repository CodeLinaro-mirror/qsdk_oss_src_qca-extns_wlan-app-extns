// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include <dirent.h>

#include "common/wpa_ctrl.h"
#include "common/ieee802_11_defs.h"
#include "hostapd_cli_extn.h"
#include "utils/os.h"

/* Add your cli handling for extensions here */
int hostapd_cli_cmd_set_esp_extn(struct wpa_ctrl *ctrl, int argc,
				 char *argv[])
{
	char buf[128] = {'\0'};
	int res;

	if (argc != 2) {
		printf("Invalid 'set_esp' command - usage: set_esp <param> <value>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_ESP %s=%s", argv[0], argv[1]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("Too long SET_ESP command.\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}


int hostapd_cli_cmd_get_esp_extn(struct wpa_ctrl *ctrl, int argc,
				 char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_ESP");
}
