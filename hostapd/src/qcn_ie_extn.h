/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCN_IE_EXTN_H
#define QCN_IE_EXTN_H

#define OUI_QCN					0x8cfdf0
#define QCN_OUI_TYPE				0x01

/*
 * 32-bit length:
 * vendor type OUI (3 bytes, big-endian)in bits [31:8]
 * OUI type in bits [7:0]
 */
#define QCN_IE_VENDOR_TYPE			((OUI_QCN << 8) | QCN_OUI_TYPE)
#define QCN_ATTRIB_VERSION			0x01
#define QCN_VER_ATTR_VER			0x01
#define QCN_VER_ATTR_SUBVERSION			0x00

/* Per-attribute overhead: id(1) + len(1) = 2 bytes (QCN_ATTRIB_HDR_LEN) */
#define QCN_ATTRIB_HDR_LEN			2

#define QCN_ATTRIB_HE_240_MHZ_SUPP		0X0B
#define QCN_HE_240_MHZ_MAX_ELEM_LEN		9

/*
 * QCN IE fixed overhead:
 * EID(1) + Len(1) + OUI(3) + type(1) + Version subelement(4) = 10 bytes
 */
#define QCN_IE_HDR_LEN				10

#define QCN_ATTRIB_HE_MCS_12_13_SUPP		0x09
#define QCN_HE_MCS_12_13_SUPP_ATTRIB_LEN	2

/*
 * Wire format in QCN IE (2-byte payload):
 *   byte[0] = (self_cap >> QCN_HE_MCS_12_13_L80_SHIFT) & QCN_HE_MCS_12_13_MASK
 *   byte[1] = (self_cap >> QCN_HE_MCS_12_13_G80_SHIFT) & QCN_HE_MCS_12_13_MASK
 */
#define QCN_HE_MCS_12_13_L80_SHIFT		0
#define QCN_HE_MCS_12_13_G80_SHIFT		8
#define QCN_HE_MCS_12_13_MASK			0xff
#define QCN_HE_MCS_12_13_EXTRACT_NSS(c, s)	(((c) >> (s)) & QCN_HE_MCS_12_13_MASK)


#endif /* QCN_IE_EXTN_H */
