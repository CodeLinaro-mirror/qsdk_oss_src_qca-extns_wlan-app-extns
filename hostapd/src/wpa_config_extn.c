// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "../wpa_supplicant/config.h"
#include "cmn.h"

void
wpa_config_alloc_empty_extn(struct wpa_config *config)
{
	struct wpa_config_extn *conf_extn = &config->conf_extn;

	conf_extn->he_mcs_12_13_enabled = DEFAULT_HE_MCS_12_13_SUPPORT;
}
