/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HOSTAPD_RPTR_EXTN_H
#define HOSTAPD_RPTR_EXTN_H

#include "cmn.h"

bool uc_hostapd_compare_channel_params_extn(struct hostapd_config *conf,
					    struct hostapd_freq_params freq_params,
					    int freq);
#endif /* HOSTAPD_RPTR_EXTN_H */
