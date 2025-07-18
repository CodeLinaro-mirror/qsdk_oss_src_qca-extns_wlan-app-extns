/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _240MHZ_H
#define _240MHZ_H

struct ieee80211_240mhz_vendor_oper_extn {
	u8 ccfs1;
	u8 ccfs0;
	u16 punct_bitmap;
	u16 is5ghz240mhz          :1,
	    bfmess320mhz          :3,
	    numsound320mhz        :3,
	    nonofdmaulmumimo320mhz:1,
	    mubfmr320mhz          :1;
	u8 mcs_map_320mhz[3];
} STRUCT_PACKED;

struct ieee80211_240mhz_params_extn {
	struct ieee80211_240mhz_vendor_oper_extn *eht_240mhz_capab;
	size_t eht_240mhz_capab_len;
};

struct  hostapd_sta_add_params_extn {
	struct ieee80211_240mhz_params_extn params_240mhz;
};

struct sta_info_extn {
	struct ieee80211_240mhz_params_extn params_240mhz;
};

struct ieee802_11_elems_extn {
	const u8 *eht_240mhz_capab;
	u8 eht_240mhz_capab_len;
};

#define EHT_PHYCAP_BEAMFORMEE_SS_320MHZ_IDX	1
#define EHT_PHYCAP_BEAMFORMEE_SS_320MHZ_MASK	((u8) (BIT(5) | BIT(6) | BIT(7)))
#define EHT_PHYCAP_NUM_SOUND_DIM_320MHZ_IDX	2
#define EHT_PHYCAP_NUM_SOUND_DIM_320MHZ_MASK	((u8) (BIT(6) | BIT(7)))
#define EHT_PHYCAP_NUM_SOUND_DIM_320MHZ_IDX_1	3
#define EHT_PHYCAP_NUM_SOUND_DIM_320MHZ_MASK_1	((u8) (BIT(0)))

#define EHT_PHYCAP_MCS_NSS_LEN_160MHZ		3
#define EXTENSION_320_CHANNELS			5500
#define PUNCTURING_PATTERN_5G_320MHZ		0XF000

#define OUI_QCN					0x8cfdf0
#define QCN_ATTRIB_HE_240_MHZ_SUPP		0X0B
#define QCN_HE_240_MHZ_MAX_ELEM_LEN		9

#define EXT_INVALID				(-1)

#endif /* _240MHZ_H */
