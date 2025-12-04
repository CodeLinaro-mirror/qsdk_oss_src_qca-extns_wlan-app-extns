/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef HOSTAPD_CLI_EXTN
#define HOSTAPD_CLI_EXTN

struct wpa_ctrl;

#ifdef CONFIG_QCN_EXTN
int hostapd_cli_cmd(struct wpa_ctrl *ctrl, const char *cmd,
		    int min_args, int argc, char *argv[]);

#define HOSTAPD_CLI_CMDS_EXTN \
/*Add extn CLI command here
 *	{ "temp_cmd", hostapd_cli_extn_cmd_temp, NULL, \
 *	  "= command helper\n" }
 */

#else
#define HOSTAPD_CLI_CMDS_EXTN

#endif /* CONFIG_QCN_EXTN */

#endif /* HOSTAPD_CLI_EXTN */


