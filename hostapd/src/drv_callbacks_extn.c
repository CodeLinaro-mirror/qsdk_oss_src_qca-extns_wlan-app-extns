// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "esp.h"


int hostapd_wpa_event_extn(void *ctx, enum wpa_event_type event,
			   union wpa_event_data *data)
{
	struct hostapd_data *hapd = ctx;

	if (hapd == NULL)
                return -EINVAL;

	switch (event) {
	case EVENT_ESP_UPDATE:
		hostapd_update_esp_params_extn(hapd, data);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
