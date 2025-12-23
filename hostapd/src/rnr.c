// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <math.h>
#include "utils/includes.h"
#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "ap/hostapd.h"
#include "cmn.h"
#include "rnr.h"

bool hostapd_skip_rnr_6ghz_colocated_extn(struct hostapd_data *hapd, u32 type)
{
	struct hostapd_config_extn *conf_extn = &hapd->iconf->conf_extn;
	u8 frame_mask;

	frame_mask = conf_extn->rnr_6ghz_colocated_enable;

	if (!frame_mask)
		return false;

	switch (type) {
	case WLAN_FC_STYPE_BEACON:
		return !(frame_mask & WLAN_RNR_IN_BCN);
	case WLAN_FC_STYPE_PROBE_RESP:
		return !(frame_mask & WLAN_RNR_IN_PRB);
	case WLAN_FC_STYPE_ACTION:
		return !(frame_mask & WLAN_RNR_IN_FILS);
	default:
		return false;
	}
}
