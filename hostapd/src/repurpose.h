// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef REPURPOSE_H
#define REPURPOSE_H

struct hostapd_config;
struct hostapd_bss_config;
struct hostapd_data;

enum repurpose_mode {
	REPURPOSE_11AC	= 1,
	REPURPOSE_11AX	= 2,
	REPURPOSE_11BE	= 3,

	/* Add new modes above and update MAX accordingly */
	REPURPOSE_MAX	= REPURPOSE_11BE,
};

#ifndef CONFIG_QCN_EXTN
static inline int
hostapd_config_check_bss_repurpose_mode_extn(const struct hostapd_config *conf,
					     const struct hostapd_bss_config *bss)
{
	return 0;
}

static inline bool
hostapd_is_repurpose_disabled_11ax_extn(const struct hostapd_bss_config *bss)
{
	return false;
}

static inline bool
hostapd_is_repurpose_disabled_11be_extn(const struct hostapd_bss_config *bss)
{
	return false;
}

static inline int
hostapd_drv_notify_link_repurpose_extn(struct hostapd_data *hapd, u8 link_id)
{
	return 0;
}

static inline u8
hostapd_get_repurposed_links_bitmap_extn(struct hostapd_data *hapd,
					 u16 *repurposed_links)
{
	if (repurposed_links)
		*repurposed_links = 0;

	return 0;
}

static inline int
hostapd_link_remove_repurposed_bss_extn(struct hostapd_data *hapd,
					u32 removal_type)
{
	return 0;
}

static inline struct hostapd_data *
hostapd_get_non_repurposed_link_of_mld_extn(struct hostapd_data *hapd)
{
	return hapd;
}
#else /* CONFIG_QCN_EXTN */
int
hostapd_config_check_bss_repurpose_mode_extn(const struct hostapd_config *conf,
					     const struct hostapd_bss_config *bss);

static inline bool
hostapd_is_valid_repurpose_mode_extn(int mode)
{
	return (mode >= REPURPOSE_11AC && mode <= REPURPOSE_MAX);
}

bool
hostapd_is_repurpose_disabled_11ax_extn(const struct hostapd_bss_config *bss);

bool
hostapd_is_repurpose_disabled_11be_extn(const struct hostapd_bss_config *bss);

int
hostapd_drv_notify_link_repurpose_extn(struct hostapd_data *hapd, u8 link_id);

u8 hostapd_get_repurposed_links_bitmap_extn(struct hostapd_data *hapd,
					    u16 *repurposed_links);

int
hostapd_link_remove_repurposed_bss_extn(struct hostapd_data *hapd,
					u32 removal_type);

struct hostapd_data *
hostapd_get_non_repurposed_link_of_mld_extn(struct hostapd_data *hapd);
#endif /* CONFIG_QCN_EXTN */
#endif /* REPURPOSE_H */
