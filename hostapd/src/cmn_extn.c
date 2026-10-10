/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "includes.h"
#include "utils/common.h"
#include "ap/hostapd.h"
#include "cmn.h"

void hostapd_iface_init_extn(struct hostapd_iface *iface)
{
	if (!iface)
		return;

	iface->iface_extn.check_hw_blocklist = true;
	iface->iface_extn.agile_capable = true;
	iface->iface_extn.vlp_non_prior_penalty = DEF_VLP_NON_PRIOR_PENALTY;
}

void hostapd_iface_deinit_extn(struct hostapd_iface *iface)
{
	if (!iface)
		return;

	hostapd_free_hw_blocklist_info_extn(iface->iface_extn.hw_blocklist_info,
					    iface->iface_extn.num_hw_blocklist);
	iface->iface_extn.hw_blocklist_info = NULL;
	iface->iface_extn.num_hw_blocklist = 0;

	iface->iface_extn.is_waiting_for_afc_regd = false;
}

void hostapd_iface_free_extn(struct hostapd_iface *iface)
{
#ifdef CONFIG_QCN_APP_EXTN
	struct qacs_data_extn *data_extn;
#endif

	if (!iface)
		return;

#ifdef CONFIG_QCN_APP_EXTN
	data_extn = ICM_GET_EXTN_DATA_PTR(iface);
	free(data_extn->neighbor_report);
	data_extn->neighbor_report = NULL;
	data_extn->num_neighbor_entries = 0;
#endif
}

u8 hostapd_get_cbs_enable_extn(struct hostapd_config *conf)
{
	if (!conf)
		return 0;

	return conf->conf_extn.cbs_params.cbs_enable;
}

void hostapd_set_cbs_enable_extn(struct hostapd_config *conf, u8 enable)
{
	if (!conf)
		return;

	conf->conf_extn.cbs_params.cbs_enable = enable;
}
