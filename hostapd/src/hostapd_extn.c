// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/defs.h"
#include "ap/hostapd.h"
#include "cmn.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"

int hostapd_validate_mbssid_group_size_extn(struct hostapd_data *hapd)
{
	if (hapd->conf->mld_ap &&
	    (hapd->iface->conf->group_size == MULTI_MBSSID_GROUP_SIZE_MAX) &&
	    (hapd->iface->max_mgmt_frm_sz < MGMT_MIN_FRAME_SIZE_REQUIRED_MLO_MBSSID)) {
		wpa_printf(MSG_ERROR,
			   "Invalid MBSSID group size (%u) for MLD AP with mgmt frame size (%d)",
			   hapd->iface->conf->group_size, MGMT_MIN_FRAME_SIZE_REQUIRED_MLO_MBSSID);
		return -1;
	}

	return 0;
}
