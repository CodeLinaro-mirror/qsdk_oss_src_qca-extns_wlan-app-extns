/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "ap/hostapd.h"
#include "ap/sta_info.h"
#include "cmn.h"
#include "wds_ie.h"
#include "qcn_ie_extn.h"
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
u8 *qcn_ie_begin(u8 *pos, u8 **len_ptr)
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
void qcn_ie_end(u8 *len_ptr, const u8 *end)
{
	*len_ptr = (u8)(end - len_ptr - 1);
}

static int wpa_parse_qcn_he_mcs_12_13_cap(struct ieee802_11_elems_extn *elems_extn,
					  const u8 *val, u8 tlen)
{
	if (tlen < QCN_HE_MCS_12_13_SUPP_ATTRIB_LEN) {
		wpa_printf(MSG_DEBUG,
			   "QCN IE: HE MCS 12/13 TLV too short (len=%u, need %u)",
			   tlen, QCN_HE_MCS_12_13_SUPP_ATTRIB_LEN);
		return -1;
	}

	elems_extn->he_mcs_12_13_peer_cap = ((u16)val[1] << 8) | val[0];
	return 0;
}

static int ieee802_11_parse_qcn_he_mcs_12_13_ie(struct ieee802_11_elems_extn *elems_extn,
						const u8 *pos, size_t elen)
{
	size_t off = 0;
	int ret = -1;

	if (!elems_extn || !pos)
		return ret;

	/* Need at least OUI(3) + OUI-type(1)*/
	if (elen < 4 || pos[3] != QCN_OUI_TYPE)
		return ret;

	pos  += 4;
	elen -= 4;
	while (off + 2 <= elen) {
		u8 id   = pos[off];
		size_t tlen = pos[off + 1];

		if (off + (size_t)2 + (size_t)tlen > elen) {
			wpa_printf(MSG_DEBUG,
				   "QCN IE MCS 12/13: truncated TLV id=0x%02x len=%zu",
				   id, tlen);
			break;
		}

		if (id == QCN_ATTRIB_HE_MCS_12_13_SUPP) {
			ret = wpa_parse_qcn_he_mcs_12_13_cap(elems_extn, pos + off + 2, tlen);
			return ret;
		}

		off += (size_t)2 + (size_t)tlen;
	}

	return 0;
}

int ieee802_11_parse_vendor_specific_elems_extn(struct ieee802_11_elems *elems,
						unsigned int oui_flag,
						const u8 *pos, size_t elen)
{
	int ret = -1;
	switch (oui_flag) {
	case OUI_QCN:
		ret = ieee802_11_parse_vendor_specific_eht_240mhz_cap_extn(
				elems, oui_flag, pos, elen);
		ret = ieee802_11_parse_qcn_he_mcs_12_13_ie(&elems->elems_extn, pos, elen);
		return ret;
	case OUI_WDS_IE:
		/*
		 * WDS vendor IE: OUI(3) + Type(1) + Cap(1) + Ver(1) = 6 bytes.
		 * pos[3] is the OUI type byte.
		 */
		if (elen >= WDS_IE_PAYLOAD_LEN &&
		    pos[3] == WDS_IE_OUI_TYPE) {
			elems->elems_extn.wds_ie     = pos;
			elems->elems_extn.wds_ie_len = (u8)elen;
			wpa_printf(MSG_DEBUG,
				   "WDS IE: parsed vendor IE from frame "
				   "(cap=0x%02x ver=%u)",
				   pos[4], pos[5]);
			return 0;
		}
		return -1;
	default:
		return ret;
	}
}

static size_t hostapd_qcn_buflen_add_he_mcs_12_13_attr(struct hostapd_data *hapd)
{
	if (!hapd->iconf->conf_extn.he_mcs_12_13_enabled ||
	    !hostapd_is_he_enabled(hapd))
		return 0;

	return QCN_ATTRIB_HDR_LEN + QCN_HE_MCS_12_13_SUPP_ATTRIB_LEN;
}

size_t hostapd_modify_buflen_for_qcn_ie_extn(struct hostapd_data *hapd)
{
	size_t attr_len = 0;

	attr_len += hostapd_qcn_buflen_add_240mhz_attr(hapd);
	attr_len += hostapd_qcn_buflen_add_5ghz_320mhz_csa_attr(hapd);
	attr_len += hostapd_qcn_buflen_add_he_mcs_12_13_attr(hapd);
	if (attr_len)
		attr_len += QCN_IE_HDR_LEN;

	return attr_len;
}


u8 * qcn_eid_add_he_mcs_12_13_attr(u16 self_cap, bool is_enabled, u8 *pos)
{
	u8 l80_nss, g80_nss;

	if (!is_enabled)
		return pos;

	l80_nss = QCN_HE_MCS_12_13_EXTRACT_NSS(self_cap, QCN_HE_MCS_12_13_L80_SHIFT);
	g80_nss = QCN_HE_MCS_12_13_EXTRACT_NSS(self_cap, QCN_HE_MCS_12_13_G80_SHIFT);

	*pos++ = QCN_ATTRIB_HE_MCS_12_13_SUPP;
	*pos++ = QCN_HE_MCS_12_13_SUPP_ATTRIB_LEN;
	*pos++ = l80_nss; /* byte[0]: L80 NSS bitmap (≤80 MHz) */
	*pos++ = g80_nss; /* byte[1]: G80 NSS bitmap (>80 MHz) */
	return pos;
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
	pos = hostapd_qcn_eid_add_5ghz_320mhz_csa_attr(hapd, pos);
	pos = qcn_eid_add_he_mcs_12_13_attr(hapd->iface->iface_extn.he_mcs_12_13_radio_cap,
					   hapd->iconf->conf_extn.he_mcs_12_13_enabled &&
					   hostapd_is_he_enabled(hapd),
					   pos);

	if (pos == eid + QCN_IE_HDR_LEN)
		return eid;

	qcn_ie_end(len_ptr, pos);
	return pos;
}


void hostapd_drv_set_peer_he_mcs_12_13_cap_extn(struct hostapd_data *hapd,
						struct ieee802_11_elems_extn *elems_extn)
{
	struct hostapd_iface_extn *iface_extn = &hapd->iface->iface_extn;
	if (!elems_extn || !hapd->iconf->conf_extn.he_mcs_12_13_enabled)
		return;

	iface_extn->he_mcs_12_13_peer_cap = elems_extn->he_mcs_12_13_peer_cap;
	hostapd_set_he_mcs_12_13_peer_cap_extn(hapd);
	return;
}

