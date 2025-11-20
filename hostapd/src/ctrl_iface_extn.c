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


static int hostapd_ctrl_iface_set_esp_extn(struct hostapd_data *hapd, char *cmd)
{
        struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
        char *param;
        int val;

        if (!iface_extn)
                return -1;

        param = strsep(&cmd, "=");
        if (!param || !cmd)
                return -1;

        if (strcmp(param, "enable_esp") != 0 && !iface_extn->esp.enable) {
                wpa_printf(MSG_ERROR, "ESP feature is not enabled");
                return -1;
        }

        val = atoi(cmd);

        if (os_strcmp(param, "esp_airtime") == 0) {
                if (val < 0 || val > 255) {
                        wpa_printf(MSG_ERROR, "ESP: Invalid airtime value. Permissible range: 0-255");
                        return -1;
                }
        } else if (os_strcmp(param, "esp_ppdu_dur") == 0) {
                if (val < 0 || val > 255) {
                        wpa_printf(MSG_ERROR, "ESP: Invalid PPDU duration value. Permissible range: 0-255");
                        return -1;
                }
        } else if (os_strcmp(param, "esp_ba_window") == 0) {
                if (val < 0 || val > 7) {
                        wpa_printf(MSG_ERROR, "ESP: Invalid BA window value. Permissible range: 0-7");
                        return -1;
                }
        }

        return hostapd_drv_set_esp_param_extn(hapd, param, val);
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
