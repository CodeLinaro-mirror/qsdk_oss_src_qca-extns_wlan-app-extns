// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "cmn.h"

int
hostapd_ctrl_iface_receive_process_extn(struct hostapd_data *hapd,
					char *buf, char *reply,
					int reply_size,
					struct sockaddr_storage *from,
					socklen_t fromlen, int *reply_len)
{
/*
	if (os_strncmp(buf, "TEMP_CMD ", 5) == 0) {
		reply_len_extn = hostapd_tempcmd_handle_cli(hapd, buf + 5,
							 reply, reply_size);
	} else {

* Return -1 if no extension CLI command is parsed.
* This allows the parent API hostapd_ctrl_iface_receive_process_extn to
* continue processing its logic.
*/
	return -1;

/*
 * Handle as below and Return 0 if an extension configuration is handled.
 * In this case, the parent API hostapd_ctrl_iface_receive_process
 * will return immediately.

	*reply_len = reply_len_extn;

	if (*reply_len < 0) {
		os_memcpy(reply, "FAIL\n", 5);
		*reply_len = 5;
	}

	return 0;
*/
}
