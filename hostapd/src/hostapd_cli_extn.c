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

int hostapd_cli_cmd_set_non_prior_penalty_extn(struct wpa_ctrl *ctrl, int argc,
					       char *argv[])
{
	char buf[64] = {'\0'};
	int res;

	if (argc != 1) {
		printf("Usage: set_vlp_non_prior_penalty <0-100>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_VLP_NON_PRIOR_PENALTY %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("Too long SET_VLP_NON_PRIOR_PENALTY command.\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_non_prior_penalty_extn(struct wpa_ctrl *ctrl, int argc,
					       char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_VLP_NON_PRIOR_PENALTY");
}

int hostapd_cli_cmd_set_rnr_6ghz_colocated_extn(struct wpa_ctrl *ctrl,
						int argc, char *argv[])
{
	return hostapd_cli_cmd(ctrl, "RNR_6GHZ_COLOCATED", 2, argc, argv);
}

int hostapd_cli_cmd_get_rnr_6ghz_colocated_extn(struct wpa_ctrl *ctrl,
						int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_RNR_6GHZ_COLOCATED");
}

int hostapd_cli_cmd_countryie_extn(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[32];
	int ret;

	if (argc != 1) {
		printf("Invalid 'countryie' (0-disable or 1-enable) is needed\n");
		return -1;
	}

	if (os_strcmp(argv[0], "0") != 0 && os_strcmp(argv[0], "1") != 0) {
		printf("Invalid 'countryie' value '%s' - valid values are 0 or 1\n",
		       argv[0]);
		return -1;
	}

	ret = os_snprintf(cmd, sizeof(cmd), "COUNTRY_IE %s", argv[0]);
	if (os_snprintf_error(sizeof(cmd), ret))
		return -1;

	return wpa_ctrl_command(ctrl, cmd);
}

int hostapd_cli_cmd_get_countryie_extn(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc != 0) {
		printf("Invalid 'get_countryie' command - no argument needed\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, "GET_COUNTRY_IE");
}

int hostapd_cli_acs_extn(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc < 1) {
		printf("Invalid ACS command: needs 1 argument atleast\n");
	}

	return hostapd_cli_cmd(ctrl, "ACS", 1, argc, argv);
}

int hostapd_cli_cmd_ignorecac_extn(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	/* With no argument, return current configured value. */
	return hostapd_cli_cmd(ctrl, "IGNORECAC", 0, argc, argv);
}

#ifdef CONFIG_IEEE80211AC
int hostapd_cli_cmd_get_mu_cap_war_extn(struct wpa_ctrl *ctrl,
					     int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_MU_CAP_WAR");
}

int hostapd_cli_cmd_mu_cap_war_extn(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	char buf[32];
	int res;

	if (argc != 1) {
		printf("Usage: mu_cap_war <1/0>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "MU_CAP_WAR %s", argv[0]);

	if (os_snprintf_error(sizeof(buf), res)) {
		printf("mu_cap_war cmd failed\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}
#endif /* CONFIG_IEEE80211AC */

int hostapd_cli_cmd_set_obss_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						char *argv[])
{
	char buf[64] = {'\0'};
	int res;

	if (argc != 1) {
		printf("Usage: set_obss_snr_threshold <snr>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_OBSS_SNR_THRESHOLD %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("Too long SET_OBSS_SNR_THRESHOLD command.\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_obss_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_OBSS_SNR_THRESHOLD");
}

int hostapd_cli_cmd_set_obss_rx_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						   char *argv[])
{
	char buf[64] = {'\0'};
	int res;

	if (argc != 1) {
		printf("Usage: set_obss_rx_snr_threshold <snr>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_OBSS_RX_SNR_THRESHOLD %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("Too long SET_OBSS_RX_SNR_THRESHOLD command.\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_obss_rx_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						   char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_OBSS_RX_SNR_THRESHOLD");
}

int hostapd_cli_cmd_dfs_no_wradar_extn(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	char buf[64];
	int res;

	if (argc != 1) {
		printf("Invalid dfs_no_wradar command: needs one argument (0|1)\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "DFS_NO_WRADAR %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("Too long DFS_NO_WRADAR command.\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_dfs_no_wradar_extn(struct wpa_ctrl *ctrl, int argc,
					   char *argv[])
{
	if (argc != 0) {
		printf("Invalid get_dfs_no_wradar command: no arguments expected\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, "GET_DFS_NO_WRADAR");
}

int hostapd_cli_cmd_dcs_extn(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc < 1) {
		printf("Invalid dcs_enable command: need atleast 1 argument\n");
		return -1;
	}

	return hostapd_cli_cmd(ctrl, "DCS", 1, argc, argv);
}

int hostapd_cli_cmd_sync_iface_freq_extn(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	return wpa_ctrl_command(ctrl, "SYNC_IFACE_FREQ");
}

static int hostapd_cli_send_dcs_param_values(struct wpa_ctrl *ctrl,
					     const char *base,
					     int argc, char *argv[])
{
	char buf[1024];
	int pos, i, r;

	pos = os_snprintf(buf, sizeof(buf), "%s", base);

	if (os_snprintf_error(sizeof(buf), pos))
		return -1;

	for (i = 0; i < argc; i++) {
		r = os_snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
				    (pos > 0 ? " " : ""), argv[i]);
		if (os_snprintf_error(sizeof(buf), pos + r))
			return -1;
		pos += r;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_set_dcs_wlan_intr_params(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	if (argc < 2) {
		printf("Invalid dcs_params: need <key> <val> \n");
		return -1;
	}

	/* Allow odd argc so users can pass tokens like 'phyerr_penalty
	 * 10' etc.
	 * We'll send through as provided; ctrl side will validate.
	 */
	return hostapd_cli_send_dcs_param_values(ctrl, "DCS_PARAMS", argc,
						 argv);
}

int hostapd_cli_cmd_set_primary_chans(struct wpa_ctrl *ctrl,
				      int argc, char *argv[])
{
	char cmd[512];
	int res, i, pos;

	if (argc == 0) {
		/* No channels → clear the primary channel list */
		return wpa_ctrl_command(ctrl, "SET_PRIMARY_CHANS");
	}

	res = os_snprintf(cmd, sizeof(cmd), "SET_PRIMARY_CHANS");
	if (res < 0 || res >= (int)sizeof(cmd))
		return -1;
	pos = res;

	for (i = 0; i < argc; i++) {
		res = os_snprintf(cmd + pos, sizeof(cmd) - pos, " %s", argv[i]);
		if (res < 0 || res >= (int)(sizeof(cmd) - pos))
			return -1;
		pos += res;
	}

	return wpa_ctrl_command(ctrl, cmd);
}

int hostapd_cli_cmd_get_primary_chans(struct wpa_ctrl *ctrl,
				      int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_PRIMARY_CHANS");
}

int hostapd_cli_cmd_set_ht40intol(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char buf[64];
	int res;

	if (argc != 1) {
		printf("Usage: set_ht40intol <value>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "HT40INTOL %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("set_ht40intol cmd failed\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_ht40intol(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_HT40INTOL");
}

int hostapd_cli_cmd_set_autorecovery_after_nol_vapdown(struct wpa_ctrl *ctrl,
						       int argc, char *argv[])
{
	char cmd[64];
	int res;

	if (argc != 1) {
		printf("Invalid set_autorecovery_after_nol_vapdown command\n"
		       "usage: set_autorecovery_after_nol_vapdown <0|1>\n"
		       "  0: Disable VAP auto-recovery after NOL expiry\n"
		       "  1: Enable VAP auto-recovery after NOL expiry (default)\n");
		return -1;
	}

	res = os_snprintf(cmd, sizeof(cmd),
			  "SET_AUTORECOVERY_AFTER_NOL_VAPDOWN %s", argv[0]);
	if (os_snprintf_error(sizeof(cmd), res))
		return -1;

	return wpa_ctrl_command(ctrl, cmd);
}

int hostapd_cli_cmd_set_eht_config_ccfs0(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char buf[64];
	int res;

	if (argc != 1) {
		printf("Usage: set_eht_config_ccfs0 <value>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_EHT_CONFIG_CCFS0 %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("set_eht_config_ccfs0 cmd failed\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_eht_config_ccfs0(struct wpa_ctrl *ctrl,
					 int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_EHT_CONFIG_CCFS0");
}

int hostapd_cli_cmd_set_tpe_common_psd(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char buf[64];
	int res;

	if (argc != 1) {
		printf("Usage: set_tpe_common_psd <value>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_TPE_COMMON_PSD %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("set_tpe_common_psd cmd failed\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_tpe_common_psd(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_TPE_COMMON_PSD");
}

int hostapd_cli_cmd_set_tpe_tx_pwr_interp(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	char buf[64];
	int res;

	if (argc != 1) {
		printf("Usage: set_tpe_tx_pwr_interp <0|1>\n");
		return -1;
	}

	res = os_snprintf(buf, sizeof(buf), "SET_TPE_TX_PWR_INTERP %s", argv[0]);
	if (os_snprintf_error(sizeof(buf), res)) {
		printf("set_tpe_tx_pwr_interp cmd failed\n");
		return -1;
	}

	return wpa_ctrl_command(ctrl, buf);
}

int hostapd_cli_cmd_get_tpe_tx_pwr_interp(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_TPE_TX_PWR_INTERP");
}

