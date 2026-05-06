/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "ap/hostapd.h"
#include "ap/sta_info.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "../wpa_supplicant/bss.h"
#include "cmn.h"
#include "qcn_ie_extn.h"
#include "../wpa_supplicant/config.h"
#include "240mhz.h"

/**
 * qcn_ie_begin - Start a new QCN Vendor IE.
 * @pos:     write pointer into the frame buffer
 * @len_ptr: output — pointer to the Len byte.
 *
 * Writes: EID(0xDD), Len(placeholder=0), OUI(8C:FD:F0), type(0x01),
 *         Version subelement(id=0x01, len=2, ver=1, subver=0).
 *
 * Returns the updated write pointer.
 */
static u8 *qcn_ie_begin(u8 *pos, u8 **len_ptr)
{
	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	*len_ptr = pos;
	*pos++ = 0;
	WPA_PUT_BE24(pos, OUI_QCN);
	pos += 3;
	*pos++ = QCN_OUI_TYPE;
	*pos++ = QCN_ATTRIB_VERSION;
	*pos++ = 2;
	*pos++ = QCN_VER_ATTR_VER;
	*pos++ = QCN_VER_ATTR_SUBVERSION;
	return pos;
}

/**
 * qcn_ie_end - Finalise a QCN IE by back-filling the Len byte.
 * @len_ptr: pointer saved by qcn_ie_begin()
 * @end:     current write pointer (one past the last byte written)
 *
 * The Len field covers everything after itself: OUI(3)+type(1)+attrs.
 */
static void qcn_ie_end(u8 *len_ptr, const u8 *end)
{
	*len_ptr = (u8)(end - len_ptr - 1);
}

int ieee802_11_parse_vendor_specific_elems_extn(struct ieee802_11_elems *elems,
						unsigned int oui_flag,
						const u8 *pos, size_t elen)
{
	int ret;
	switch (oui_flag) {
	case OUI_QCN:
		ret = ieee802_11_parse_vendor_specific_eht_240mhz_cap_extn(
				elems, oui_flag, pos, elen);
		return ret;
	default:
		return -1;
	}
}

size_t hostapd_modify_buflen_for_qcn_ie_extn(struct hostapd_data *hapd)
{
	size_t attr_len;

	attr_len = hostapd_qcn_buflen_add_240mhz_attr(hapd);
	if (attr_len)
		attr_len += QCN_IE_HDR_LEN;

	return attr_len;
}

u8 * hostapd_eid_qcn_vendor_ie_extn(struct hostapd_data *hapd, u8 *eid,
				    enum ieee80211_op_mode opmode)
{
	u8 *pos = eid;
	u8 *len_ptr = NULL;

	if (!eid)
		return eid;

	pos = qcn_ie_begin(pos, &len_ptr);
	pos = hostapd_qcn_eid_add_240mhz_attr(hapd, pos, opmode);
	if (pos == eid + QCN_IE_HDR_LEN)
		return eid;

	qcn_ie_end(len_ptr, pos);
	return pos;
}
