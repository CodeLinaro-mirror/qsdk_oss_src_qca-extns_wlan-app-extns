/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef DCS_H
#define DCS_H

#define ALLOWED_DCS_MASK 0x0013

enum dcs_cmd_type {
	GET_DCS_CONFIG,
	SET_DCS_CONFIG,
};

int hostapd_drv_dcs_config(struct hostapd_data *hapd, u8 link_id,
			   struct driver_dcs_config *params);
#endif
