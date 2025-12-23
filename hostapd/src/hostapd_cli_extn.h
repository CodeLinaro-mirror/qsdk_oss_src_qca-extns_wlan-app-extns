/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef HOSTAPD_CLI_EXTN
#define HOSTAPD_CLI_EXTN

struct wpa_ctrl;

int hostapd_cli_cmd_set_esp_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_esp_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_set_rnr_6ghz_colocated_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_rnr_6ghz_colocated_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);

#ifdef CONFIG_QCN_EXTN
int hostapd_cli_cmd(struct wpa_ctrl *ctrl, const char *cmd,
		    int min_args, int argc, char *argv[]);

int wpa_ctrl_command(struct wpa_ctrl *ctrl, const char *cmd);

#define HOSTAPD_CLI_CMDS_EXTN \
	{ "set_esp", hostapd_cli_cmd_set_esp_extn, NULL, \
		"<param> <value> = set ESP param (esp_airtime 0-255, " \
		"esp_ppdu_dur 0-255, esp_ba_window 0-7, enable_esp 0-1)" }, \
	{ "get_esp", hostapd_cli_cmd_get_esp_extn, NULL, \
		"= get ESP parameters from driver" }, \
	{ "rnr_6ghz_colocated", hostapd_cli_cmd_set_rnr_6ghz_colocated_extn, NULL, \
		"<rnr_mode> <frm_val> = config rnr 6ghz colocated" }, \
	{ "get_rnr_6ghz_colocated", hostapd_cli_cmd_get_rnr_6ghz_colocated_extn, NULL, \
		"= get rnr 6ghz colocated" },
#else
#define HOSTAPD_CLI_CMDS_EXTN

#endif /* CONFIG_QCN_EXTN */

#endif /* HOSTAPD_CLI_EXTN */


