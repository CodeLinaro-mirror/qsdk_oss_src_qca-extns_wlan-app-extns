// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "esp.h"
#include "utils/os.h"
#include "common/ieee802_11_defs.h"
#include "ap/ap_config.h"
#include "ap/beacon.h"


static int hostapd_ctrl_iface_set_esp_extn(struct hostapd_data *hapd, char *cmd)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	char *param;
	long val_long;
	char *end = NULL;

	if (!iface_extn)
		return -1;

	param = strsep(&cmd, "=");
	if (!param || !cmd)
		return -1;

	if (strcmp(param, "enable_esp") != 0 && !iface_extn->esp.enable) {
		wpa_printf(MSG_ERROR, "ESP feature is not enabled");
		return -1;
	}

	while (*cmd == ' ') cmd++;
	if (*cmd == '\0') {
		wpa_printf(MSG_ERROR, "ESP: Invalid value for %s: empty", param);
		return -1;
	}

	errno = 0;
	val_long = strtol(cmd, &end, 10);
	if (val_long < 0) {
		wpa_printf(MSG_ERROR, "ESP: Negative value not allowed for %s: %ld", param, val_long);
		return -1;
	}

	if (errno != 0 || end == cmd) {
		wpa_printf(MSG_ERROR, "ESP: Invalid numeric value for %s: '%s'", param, cmd);
		return -1;
	}

	while (end && *end == ' ') end++;
	if (end && *end != '\0') {
		wpa_printf(MSG_ERROR, "ESP: Invalid characters in value for %s: '%s'", param, cmd);
		return -1;
	}
	if (os_strcmp(param, "esp_airtime") == 0) {
		if (val_long > 255) {
			wpa_printf(MSG_ERROR, "ESP: Invalid airtime value. Permissible range: 0-255");
			return -1;
		}
	} else if (os_strcmp(param, "esp_ppdu_dur") == 0) {
		if (val_long > 255) {
			wpa_printf(MSG_ERROR, "ESP: Invalid PPDU duration value. Permissible range: 0-255");
			return -1;
		}
	} else if (os_strcmp(param, "esp_ba_window") == 0) {
		if (val_long > 7) {
			wpa_printf(MSG_ERROR, "ESP: Invalid BA window value. Permissible range: 0-7");
			return -1;
		}
	} else if (os_strcmp(param, "enable_esp") == 0) {
		if (val_long != 0 && val_long != 1) {
			wpa_printf(MSG_ERROR, "ESP: Invalid enable_esp value. Permissible values: 0 or 1");
			return -1;
		}
	}

	return hostapd_drv_set_esp_param_extn(hapd, param, (int)val_long);
}


static int hostapd_ctrl_iface_get_esp_extn(struct hostapd_data *hapd,
					   const char *cmd, char *reply,
					   int reply_size)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	int ret;

	if (!iface_extn)
		return -1;

	ret = os_snprintf(reply, reply_size,
			  "airtime=%u "
			  "ppdu_dur=%u "
			  "ba_window=%u "
			  "enable_esp=%u\n",
			  iface_extn->esp.airtime,
			  iface_extn->esp.ppdu_dur,
			  iface_extn->esp.ba_window,
			  iface_extn->esp.enable);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}

static int hostapd_ctrl_set_rnr_6ghz_colocated_extn(struct hostapd_data *hapd, char *cmd)
{
#ifdef NEED_AP_MLME
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	uint8_t rnr_mode, frm_val;
	char *ptr, *endptr;

	if (!hapd->started) {
		wpa_printf(MSG_ERROR, "Interface is not UP.\n");
		return -1;
	}

	rnr_mode = (uint8_t)strtol(cmd, &ptr, 10);
	if (ptr == cmd || rnr_mode > 1) {
		wpa_printf(MSG_ERROR, "Invalid mode. Use 1:Enable 0:Disable");
		return -1;
	}

	frm_val = (uint8_t)strtol(ptr, &endptr, 10);
	if (ptr == endptr || frm_val > 7) {
		wpa_printf(MSG_ERROR, "Invalid frm_val. Valid values:0 to 7");
		return -1;
	}

	if (rnr_mode == 1 && frm_val == 0) {
		wpa_printf(MSG_ERROR, "Mode is enable But frm is not selected. Invalid frm_val");
		return -1;
	}

	if (rnr_mode == 1) {
		/* User rnr mode enable: set frame mask  */
		conf_extn->rnr_6ghz_colocated_enable |= (frm_val & 0x7);
	} else {
		/* User rnr mode disable: clear frame mask */
		conf_extn->rnr_6ghz_colocated_enable &= ~(frm_val & 0x7);
	}
	wpa_printf(MSG_INFO, "rnr_mode:%d frm_val:%d rnr_6ghz_colocated_enable %d",
			rnr_mode, frm_val, conf_extn->rnr_6ghz_colocated_enable);

	/* Update beacon to reflect the config */
	ieee802_11_update_beacons(hapd->iface);

	return 0;
#else /* NEED_AP_MLME */
	return -1;
#endif /* NEED_AP_MLME */
}

static int hostapd_ctrl_get_rnr_6ghz_colocated_extn(struct hostapd_data *hapd,
						    const char *cmd, char *reply,
						    int reply_size)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	int ret;

	ret = os_snprintf(reply, reply_size,
			"rnr_6ghz_colocated=%d\n",
			conf_extn->rnr_6ghz_colocated_enable);
	if (os_snprintf_error(reply_size, ret))
		return -1;

	return ret;
}


int
hostapd_ctrl_iface_receive_process_extn(struct hostapd_data *hapd,
					char *buf, char *reply,
					int reply_size,
					struct sockaddr_storage *from,
					socklen_t fromlen, int *reply_len)
{
	int reply_len_extn = *reply_len;

	if (os_strncmp(buf, "SET_ESP", 7) == 0) {
		if (hostapd_ctrl_iface_set_esp_extn(hapd, buf + 8))
			reply_len_extn = -1;
	} else if (os_strncmp(buf, "GET_ESP", 7) == 0) {
		reply_len_extn = hostapd_ctrl_iface_get_esp_extn(hapd, buf + 7, reply,
								 reply_size);
	} else if (os_strncmp(buf, "RNR_6GHZ_COLOCATED ", 19) == 0) {
		if (hostapd_ctrl_set_rnr_6ghz_colocated_extn(hapd, buf + 19))
			reply_len_extn = -1;
        } else if (os_strncmp(buf, "GET_RNR_6GHZ_COLOCATED ", 23) == 0) {
		reply_len_extn = hostapd_ctrl_get_rnr_6ghz_colocated_extn(hapd, buf + 23, reply,
									  reply_size);
        } else {
		return -1;
	}

	*reply_len = reply_len_extn;

	if (*reply_len < 0) {
		os_memcpy(reply, "FAIL\n", 5);
		*reply_len = 5;
	}

	return 0;
}

int hostapd_ctrl_iface_set_extn(struct hostapd_data *hapd, char *cmd, char *value)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	int val, ret;

	if (os_strcasecmp(cmd, "rnr_member_ess_colocated_en") == 0) {
		val = atoi(value);
		if (val < 0 || val > 1) {
			wpa_printf(MSG_ERROR,
				"rnr_member_ess_colocated_en: Invalid value (expected 0 or 1)");
			return -1;
		}
		if (is_6ghz_freq(hapd->iface->freq)) {
			conf_extn->rnr_ess_colocated_en = val;
			ieee802_11_update_beacons(hapd->iface);
		} else {
			wpa_printf(MSG_ERROR, "rnr_member_ess_colocated_en is valid only for 6 GHz");
			return -1;
		}

	} else if (os_strcasecmp(cmd, "rnr_6ghz_override") == 0) {
		val = atoi(value);
		if (val < 0 || val > 1) {
			wpa_printf(MSG_ERROR, "rnr_6ghz_override: Invalid value (expected 0 or 1)");
			return -1;
		}
		if (!is_6ghz_freq(hapd->iface->freq)) {
			wpa_printf(MSG_ERROR, "rnr_6ghz_override is valid only for 6 GHz");
			return -1;
		}

		conf_extn->rnr_6ghz_override = val;

		ret = ieee802_11_update_beacons(hapd->iface);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "Failed to update beacon");
			return -1;
		}
	}
	return 0;
}

int hostapd_ctrl_iface_status_extn(struct hostapd_data *hapd, char *buf,
				   size_t buflen, size_t curr_len)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	size_t len = curr_len;
	int ret;

	if (is_6ghz_freq(hapd->iface->freq)) {
		ret = os_snprintf(buf + len, buflen - len,
				"rnr_member_ess_colocated_en=%d\n",
				conf_extn->rnr_ess_colocated_en);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;

		ret = os_snprintf(buf + len, buflen - len,
				"rnr_6ghz_override=%d\n",
				conf_extn->rnr_6ghz_override);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;
	}

	return len;
}
