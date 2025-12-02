/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "cmn.h"


int hostapd_wpa_event_extn(void *ctx, enum wpa_event_type event,
			   union wpa_event_data *data)
{
	switch (event) {
	default:
		return -EINVAL;
	}

	return 0;
}
