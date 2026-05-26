/* SPDX-License-Identifier: BSD-3-Clause */
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*/

#ifndef UCODE_EXTN_H
#define UCODE_EXTN_H
#include "dfs_extn.h"

#ifndef CONFIG_QCN_EXTN
static inline
void hostapd_ucode_trigger_bhsta_disconnect(struct hostapd_iface *hapd)
{
}
static inline
void hostapd_ucode_notify_uplink_csa(struct hostapd_iface *hapd, int event, u8 channel,
				     int freq, int csa_count, u8 new_ch_width,
				     u8 ch_seg_0, u8 ch_seg_1,
				     dfs_nol_ie_list *nol_list)
{
}
#else
void hostapd_ucode_trigger_bhsta_disconnect(struct hostapd_iface *hapd);
void hostapd_ucode_notify_uplink_csa(struct hostapd_iface *hapd, int event, u8 channel,
				     int freq, int csa_count, u8 new_ch_width,
				     u8 ch_seg_0, u8 ch_seg_1,
				     dfs_nol_ie_list *nol_list);
#endif /* CONFIG_QCN_EXTN */
#endif /* UCODE_EXTN_H */
