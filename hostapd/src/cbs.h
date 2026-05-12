/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef CBS_H
#define CBS_H

#include <stdbool.h>

struct cbs_params_extn {
	u8 cbs_enable; /* enable/disable cbs scan (0:disable | 1:enable CBS scan once | 2:enable cbs scan to run continuously)*/
	int dwellsplit; /* dwell split time on a foreign channel for one scan (msec) */
	int totaldwell; /* total dwell time for scan on single channel (msec) */
	int dwellrest; /* time to rest before issuing the next consecutive scan on the same channel (msec) */
	int resttime; /* time to wait between scans on different channels (msec) */
	int waittime; /* time to wait after scanning all channels and before starting the next scan (msec) */
	bool csa_enable; /* enable CSA for best channel obtained after CBS scan */
};

int hostapd_handle_cli_cbs_extn(struct hostapd_data *hapd,
				char *pos, char *buf,
				size_t buflen);

#endif
