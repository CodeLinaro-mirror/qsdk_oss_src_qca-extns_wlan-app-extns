/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef HOSTAPD_CLI_EXTN
#define HOSTAPD_CLI_EXTN

struct wpa_ctrl;

int hostapd_cli_cmd_set_esp_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_esp_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_set_non_prior_penalty_extn(struct wpa_ctrl *ctrl, int argc,
					       char *argv[]);
int hostapd_cli_cmd_get_non_prior_penalty_extn(struct wpa_ctrl *ctrl, int argc,
					       char *argv[]);
int hostapd_cli_cmd_set_rnr_6ghz_colocated_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_rnr_6ghz_colocated_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_countryie_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_countryie_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);

#ifdef CONFIG_IEEE80211AC
int hostapd_cli_cmd_get_mu_cap_war_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_mu_cap_war_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
#endif /* CONFIG_IEEE80211AC */

int hostapd_cli_cmd_sync_iface_freq_extn(struct wpa_ctrl *ctrl, int argc,
					 char *argv[]);
int hostapd_cli_cmd_dfs_no_wradar_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_dfs_no_wradar_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_dcs_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_set_dcs_wlan_intr_params(struct wpa_ctrl *ctrl, int argc,
					    char *argv[]);
int hostapd_cli_cmd_set_primary_chans(struct wpa_ctrl *ctrl, int argc,
				      char *argv[]);
int hostapd_cli_cmd_get_primary_chans(struct wpa_ctrl *ctrl, int argc,
				      char *argv[]);
int hostapd_cli_cmd_set_obss_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						char *argv[]);
int hostapd_cli_cmd_get_obss_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						char *argv[]);
int hostapd_cli_cmd_set_obss_rx_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						   char *argv[]);
int hostapd_cli_cmd_get_obss_rx_snr_threshold_extn(struct wpa_ctrl *ctrl, int argc,
						   char *argv[]);
int hostapd_cli_cmd_set_autorecovery_after_nol_vapdown(struct wpa_ctrl *ctrl,
						       int argc, char *argv[]);
#ifdef CONFIG_QCN_EXTN
int hostapd_cli_cmd(struct wpa_ctrl *ctrl, const char *cmd,
		    int min_args, int argc, char *argv[]);

int wpa_ctrl_command(struct wpa_ctrl *ctrl, const char *cmd);
int hostapd_cli_acs_extn(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_set_ht40intol(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_ht40intol(struct wpa_ctrl *ctrl, int argc, char *argv[]);

int hostapd_cli_cmd_set_eht_config_ccfs0(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_eht_config_ccfs0(struct wpa_ctrl *ctrl, int argc, char *argv[]);

int hostapd_cli_cmd_set_tpe_common_psd(struct wpa_ctrl *ctrl, int argc, char *argv[]);
int hostapd_cli_cmd_get_tpe_common_psd(struct wpa_ctrl *ctrl, int argc, char *argv[]);
#define HOSTAPD_CLI_CMDS_EXTN \
	{ "set_esp", hostapd_cli_cmd_set_esp_extn, NULL, \
		"<param> <value> = set ESP param (esp_airtime 0-255, " \
		"esp_ppdu_dur 0-255, esp_ba_window 0-7, enable_esp 0-1)" }, \
	{ "get_esp", hostapd_cli_cmd_get_esp_extn, NULL, \
		"= get ESP parameters from driver" }, \
	{ "set_vlp_non_prior_penalty", hostapd_cli_cmd_set_non_prior_penalty_extn, NULL, \
		"<0-100> = set 6 GHz VLP non-priority channel penalty (%)" }, \
	{ "get_vlp_non_prior_penalty", hostapd_cli_cmd_get_non_prior_penalty_extn, NULL, \
		"= get 6 GHz VLP non-priority channel penalty (%)" }, \
	{ "rnr_6ghz_colocated", hostapd_cli_cmd_set_rnr_6ghz_colocated_extn, NULL, \
		"<rnr_mode> <frm_val> = config rnr 6ghz colocated" }, \
	{ "get_rnr_6ghz_colocated", hostapd_cli_cmd_get_rnr_6ghz_colocated_extn, NULL, \
		"= get rnr 6ghz colocated" }, \
	{ "countryie", hostapd_cli_cmd_countryie_extn, NULL, \
		"<0|1> = disable/enable Country IE in beacon" }, \
	{ "get_countryie", hostapd_cli_cmd_get_countryie_extn, NULL, \
		"= get Country IE advertisement status" }, \
	{ "acs", hostapd_cli_acs_extn, NULL, \
		"get_status           : get ACS current status\n" \
		"rank_en <1|0>        : enable/disable channel ranking\n" \
		"get_rank_en          : get channel ranking enable state\n" \
		"qacs_enable <1|0>    : enable/disable QACS extension\n" \
		"get_qacs_enable      : get QACS extension enable state\n" \
		"noscan <1|0>         : enable/disable noscan mode\n" \
		"get_noscan           : get noscan mode state\n" \
		"dfs_exclude <1|0>    : enable/disable DFS channel exclusion\n" \
		"get_dfs_exclude      : get DFS channel exclusion state\n" \
		"dwelltime <ms>       : set dwell time in milliseconds\n" \
		"get_dwell            : get dwell time in milliseconds\n" \
		"dbgtrace <value>     : set debug (0x00FF=level, 0xFF00=module mask)\n" \
		"get_dbgtrace         : get debug/trace mask\n" \
		"wradar <0|1>         : enable/disable excluding weather radar channels\n" \
		"get_wradar           : get weather radar handling state\n" \
		"txpwr_opt <0|1|2>    : set the tx pwr optimization state(0 = disable, 1 = optimize throughput, 2 = optimize range)\n" \
		"get_txpwr_opt        : get tx power optimization state\n" \
		"6g_only_psc <1|0>    : restrict 6 GHz to PSC channels only\n" \
		"get_6g_only_psc      : get the state of restricting 6 GHz to PSC channels only\n" \
		"acs invoke <0|1>     : invoke ACS (0=dynamicACS+CSA)|(1=DynamicACS)\n"}, \
	{ "mu_cap_war", hostapd_cli_cmd_mu_cap_war_extn, NULL, \
		"enable/disable VHT MU-MIMO capability for MU_CAP_WAR clients" }, \
	{ "sync_iface_freq", hostapd_cli_cmd_sync_iface_freq_extn, NULL, \
		"= sync runtime interface frequency from current config" }, \
	{ "get_mu_cap_war", hostapd_cli_cmd_get_mu_cap_war_extn, NULL, \
		"get MU_CAP_WAR status" }, \
	{ "set_obss_snr_threshold", hostapd_cli_cmd_set_obss_snr_threshold_extn, NULL, \
		"<snr> = set OBSS SNR threshold in dB (per-radio)" }, \
	{ "get_obss_snr_threshold", hostapd_cli_cmd_get_obss_snr_threshold_extn, NULL, \
		"= get current OBSS SNR threshold in dB" }, \
	{ "set_obss_rx_snr_threshold", hostapd_cli_cmd_set_obss_rx_snr_threshold_extn, NULL, \
		"<snr> = set OBSS RX SNR threshold in dB (per-radio)" }, \
	{ "get_obss_rx_snr_threshold", hostapd_cli_cmd_get_obss_rx_snr_threshold_extn, NULL, \
		"= get current OBSS RX SNR threshold in dB" }, \
	{ "dfs_no_wradar", hostapd_cli_cmd_dfs_no_wradar_extn, NULL, \
		"<0|1> = avoid ETSI weather radar channels in DFS random channel selection" }, \
	{ "get_dfs_no_wradar", hostapd_cli_cmd_get_dfs_no_wradar_extn, NULL, \
		"= get dfs_no_wradar state (0|1)" }, \
	{ "dcs", hostapd_cli_cmd_dcs_extn, NULL, \
		"enable		: enable DCS configuration\n" \
		"bw_reduction_ctrl	: <mask> = set DCS bw reduction control\n" \
		"csa_tbtt	: CSA TBTT value for DCS\n" \
		"event_action	: <mask> = set DCS event action bitmap (bit set = process, bit clear = discard)\n" \
		"event_notify	: <mask> set DCS event notification bitmap\n" \
		"wlan_intr_params	: phyerr_penalty <val> phyerr_threshold <val> radarerr_threshold <val> coch_intr_threshold <val> txerr_threshold <val> user_max_cu <val> intr_detection_threshold <val> sample_size <val> = set DCS WLAN INTR params\n" \
		"get_event_action	: get DCS event action bitmap\n" \
		"get_event_notify	: get DCS event notification\n" \
		"set_dcs_enable_timer	: <sec> = set DCS re-enable time\n" \
		"get_dcs_enable_timer	: get DCS re-enable time\n" \
		"sim            : simulate DCS interference\n" \
	}, \
	{ "set_primary_chans", hostapd_cli_cmd_set_primary_chans, NULL, \
	  "[<ch1> <ch2> ...]\n" \
	  "  = set primary channel list (space-separated channel numbers);" \
	  " no args clears list" }, \
	{ "get_primary_chans", hostapd_cli_cmd_get_primary_chans, NULL, \
	  "= get current primary channel list" }, \
	{ "set_ht40intol", hostapd_cli_cmd_set_ht40intol, NULL, \
		"<value 0|1> = set ht40 intolerant bit" }, \
	{ "get_ht40intol", hostapd_cli_cmd_get_ht40intol, NULL, \
		" = get current ht40 intolerant bit value" }, \
	{ "set_eht_config_ccfs0", hostapd_cli_cmd_set_eht_config_ccfs0, NULL, \
		"<value 0|1> :Set EHT OP ccfs0 field config" }, \
	{ "get_eht_config_ccfs0", hostapd_cli_cmd_get_eht_config_ccfs0, NULL, \
		"Get EHT OP ccfs0 field config" }, \
	{ "set_tpe_common_psd", hostapd_cli_cmd_set_tpe_common_psd, NULL, \
		"<value 0|1> :Set config for common psd power in TPE IE" }, \
	{ "get_tpe_common_psd", hostapd_cli_cmd_get_tpe_common_psd, NULL, \
		"Get config for common psd power in TPE IE"}, \
	{ "set_autorecovery_after_nol_vapdown", \
	  hostapd_cli_cmd_set_autorecovery_after_nol_vapdown, NULL, \
	  "<1|0> = enable/disable VAP auto-recovery after NOL expiry" },
#else
#define HOSTAPD_CLI_CMDS_EXTN

#endif /* CONFIG_QCN_EXTN */

#endif /* HOSTAPD_CLI_EXTN */
