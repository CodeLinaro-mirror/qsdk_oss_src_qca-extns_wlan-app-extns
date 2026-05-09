/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "includes.h"
#include <sys/types.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <net/if.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <linux/rtnetlink.h>
#include <netpacket/packet.h>
#include <linux/errqueue.h>
#include "common.h"
#include "eloop.h"
#include "common/qca-vendor.h"
#include "common/qca-vendor-attr.h"
#include "common/brcm_vendor.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_common.h"
#include "drivers/driver.h"
#include "drivers/driver_nl80211.h"
#include "ap/hostapd.h"
#include "ap/hw_features.h"
#include "esp.h"
#include "dcs.h"
#include "rropinfo.h"


struct hostapd_sta_add_params;

static int mac_config_handler(struct nl_msg *msg, void *arg)
{
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct wpabuf *buf = arg;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlattr *ven_reply, *nl;
	int rem_len;

	if (!buf)
		return NL_SKIP;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	ven_reply = tb[NL80211_ATTR_VENDOR_DATA];
	if (!ven_reply)
		return NL_SKIP;

	if (nla_len(ven_reply) > wpabuf_tailroom(buf)) {
		wpa_printf(MSG_ERROR, "nl80211: no buffer space");
		return NL_SKIP;
	}

	nla_for_each_nested(nl, ven_reply, rem_len) {
		wpabuf_put_data(buf, nla_data(nl), nla_len(nl));
	}

	return NL_SKIP;
}

static int
nl80211_get_6ghz_thresh_priority_freq_handler(struct nl_msg *msg,
					      void *arg)
{
	u16 *val = arg;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *vendor_data;
	struct nlattr *thresh_attr;

	if (nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		      genlmsg_attrlen(gnlh, 0), NULL)) {
		wpa_printf(MSG_ERROR, "nl80211: Failed to parse netlink attributes");
		return NL_SKIP;
	}

	vendor_data = tb[NL80211_ATTR_VENDOR_DATA];
	if (vendor_data) {
		struct nlattr *vendor_tb[QCA_WLAN_VENDOR_ATTR_CONFIG_MAX + 1];

		if (nla_parse(vendor_tb, QCA_WLAN_VENDOR_ATTR_CONFIG_MAX,
			      nla_data(vendor_data), nla_len(vendor_data),
			      NULL)) {
			wpa_printf(MSG_ERROR, "nl80211: Failed to parse netlink attributes");
			return NL_SKIP;
		}

		thresh_attr = vendor_tb[QCA_WLAN_VENDOR_ATTR_CONFIG_6GHZ_VLP_PRIORITY_THRESH_FREQ];
		if (thresh_attr)
			*val = nla_get_u16(thresh_attr);
	}

	return NL_SKIP;
}

int hostapd_get_6ghz_thresh_priority_freq_extn(struct hostapd_iface *iface)
{
	struct hostapd_data *hapd;
	struct i802_bss *bss;
	struct wpa_driver_nl80211_data *drv;
	struct nl_msg *msg = NULL;
	struct nlattr *params;
	u8 radio_idx = NL80211_WIPHY_RADIO_ID_MAX;
	u16 val = 0;
	unsigned int i;
	int ret;

	if (!iface || !iface->bss || !iface->bss[0]) {
		wpa_printf(MSG_ERROR, "invalid iface for threshold fetch");
		return -EINVAL;
	}

	hapd = iface->bss[0];
	bss = hapd->drv_priv;
	if (!bss || !bss->drv) {
		wpa_printf(MSG_ERROR, "driver not initialized");
		return -ENODEV;
	}

	drv = bss->drv;

	if (iface->num_multi_hws) {
		hostapd_set_current_hw_info(iface, iface->freq);
		if (iface->current_hw_info) {
			radio_idx = iface->current_hw_info->hw_idx;
		} else {
			for (i = 0; i < iface->num_multi_hws; i++) {
				if (iface->multi_hw_info[i].start_freq >= 5945 &&
				    iface->multi_hw_info[i].end_freq <= 7125) {
					radio_idx = iface->multi_hw_info[i].hw_idx;
					wpa_printf(MSG_DEBUG,
						   "fallback selected first 6 GHz hw_idx=%u",
						   radio_idx);
					break;
				}
			}

			if (radio_idx == NL80211_WIPHY_RADIO_ID_MAX) {
				wpa_printf(MSG_DEBUG, "no radio_idx");
				return -EINVAL;
			}
		}
	}

	msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR);
	if (!msg ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_GET_WIPHY_CONFIGURATION))
		goto fail;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params)
		goto fail;

	if (nla_put_u16(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_6GHZ_VLP_PRIORITY_THRESH_FREQ,
			0))
		goto fail;

	if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX, radio_idx))
		goto fail;

	nla_nest_end(msg, params);

	ret = send_and_recv_resp(drv, msg,
				 nl80211_get_6ghz_thresh_priority_freq_handler,
				 &val);
	if (ret) {
		wpa_printf(MSG_ERROR,
			   "failed to get 6 GHz threshold freq ret=%d", ret);
		iface->iface_extn.vlp_threshold_freq = 0;
		return ret;
	}

	iface->iface_extn.vlp_threshold_freq = val;
	wpa_printf(MSG_DEBUG, "6 GHz VLP threshold freq=%u", val);

	return 0;

fail:
	if (msg)
		nlmsg_free(msg);
	iface->iface_extn.vlp_threshold_freq = 0;
	return -EINVAL;
}

/* This function sends a NL message only if extension parameters exist;
 * otherwise it just returns.
 */

void wpa_driver_nl80211_sta_add_extn(void *priv,
				     struct hostapd_sta_add_params *params)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct ieee80211_240mhz_vendor_oper_extn *oper;
	struct nl_msg *msg;
	struct nlattr *attr;
	int ret;

	/*currently added only for 240mhz operation*/

	if (!params->params_extn.params_240mhz.eht_240mhz_capab)
		return;

	oper = params->params_extn.params_240mhz.eht_240mhz_capab;

	msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR);
	if (!msg ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_240MHZ_INFO))
		goto fail;


	attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr)
		goto fail;

	/* Add the Vendor-specific attributes*/
	if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_240MHZ_BEAMFORMEE_SS,
		       oper->bfmess320mhz) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_240MHZ_NUM_SOUNDING_DIMENSIONS,
		       oper->numsound320mhz) ||
	    nla_put(msg, QCA_WLAN_VENDOR_ATTR_240MHZ_MCS_MAP, 3,
		    oper->mcs_map_320mhz) ||
	    (oper->nonofdmaulmumimo320mhz &&
	    nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_240MHZ_NON_OFDMA_UL_MUMIMO))
	    || (oper->mubfmr320mhz &&
	    nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_240MHZ_MU_BEAMFORMER)))
		goto fail;

	nla_nest_end(msg, attr);

	if (nla_put_u32(msg, NL80211_ATTR_CENTER_FREQ1, oper->ccfs0) ||
	    nla_put_u32(msg, NL80211_ATTR_CENTER_FREQ2, oper->ccfs1) ||
	    nla_put_u32(msg, NL80211_ATTR_PUNCT_BITMAP, oper->punct_bitmap))
		goto fail;

	if (params->mld_link_addr) {
		if (nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN,
			    params->mld_link_addr))
			goto fail;
	} else {
		if (nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN,
			    params->addr))
			goto fail;
	}

	ret = send_and_recv_cmd(drv, msg);
	if (ret) {
		wpa_printf(MSG_ERROR,
			   "nl80211: Failed to send 240 MHz Vendor NL command: %s, %d",
			   strerror(-ret), ret);
		goto fail;
	}

	wpa_printf(MSG_DEBUG,
		   "nl80211: 240MHz Vendor Oper: ccfs0=%u, ccfs1=%u, punctured=0x%04x, "
		   "is5ghz240mhz=%u, bfmess320mhz=%u, numsound320mhz=%u, "
		   "nonofdmaulmumimo320mhz=%u, mubfmr320mhz=%u, "
		   "mcs_map_320mhz=[0x%02x, 0x%02x, 0x%02x]",
		   oper->ccfs0,
		   oper->ccfs1,
		   oper->punct_bitmap,
		   oper->is5ghz240mhz,
		   oper->bfmess320mhz,
		   oper->numsound320mhz,
		   oper->nonofdmaulmumimo320mhz,
		   oper->mubfmr320mhz,
		   oper->mcs_map_320mhz[0],
		   oper->mcs_map_320mhz[1],
		   oper->mcs_map_320mhz[2]);

	return;

fail:
	nlmsg_free(msg);
	return;
}

int nl80211_vendor_event_qca_extn(struct i802_bss *bss,
				  u32 subcmd, u8 *data, size_t len)
{
	switch (subcmd) {
	case QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_CONFIGURATION:
		qca_nl80211_handle_wifi_config_evt_extn(bss, data, len);
		break;
	case QCA_NL80211_VENDOR_SUBCMD_DCS_CONFIG:
		qca_nl80211_handle_dcs_config_evt_extn(bss, data, len);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int qca_nl80211_handle_wifi_config_evt_extn(struct i802_bss *bss,
					    u8 *data, size_t len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_CONFIG_MAX + 1] = {0};
	u8 link_id = 0;

	if (!bss) {
		wpa_printf(MSG_ERROR, "nl80211: bss is NULL!");
		return -EINVAL;
	}

	if (!(data && len)) {
		wpa_printf(MSG_ERROR,
			   "nl80211: Invalid data for WiFi configuration event");
		return -EINVAL;
	}

	if (nla_parse(tb, QCA_WLAN_VENDOR_ATTR_CONFIG_MAX,
		      (struct nlattr *)data, len, NULL)) {
		wpa_printf(MSG_ERROR,
			   "nl80211: Failed to parse WiFi configuration attributes");
		return -EINVAL;
	}

	if (tb[QCA_WLAN_VENDOR_ATTR_CONFIG_MLO_LINK_ID])
		link_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_CONFIG_MLO_LINK_ID]);

	/* Check if ESP params are present */
	if (tb[QCA_WLAN_VENDOR_ATTR_CONFIG_ESP_PARAMS])
		return nl80211_parse_esp_params_extn(bss,
						     tb[QCA_WLAN_VENDOR_ATTR_CONFIG_ESP_PARAMS],
						     link_id);

	return 0;
}

int qca_nl80211_handle_dcs_config_evt_extn(struct i802_bss *bss,
					   u8 *data, size_t len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_DCS_MAX  + 1] = {0};
	union wpa_event_data event = {};
	struct dcs_intf_event *dcs_intf_event;

	if (!bss) {
		wpa_printf(MSG_ERROR, "nl80211: bss is NULL!");
		return -EINVAL;
	}

	if (!(data && len)) {
		wpa_printf(MSG_ERROR,
			   "nl80211: Invalid data for DCS configuration event");
		return -EINVAL;
	}

	dcs_intf_event = &event.event_data_extn.dcs_intf_event;

	if (nla_parse(tb, QCA_WLAN_VENDOR_ATTR_DCS_MAX,
				(struct nlattr *)data, len, NULL)) {
		wpa_printf(MSG_ERROR,
			   "nl80211: Failed to parse DCS configuration attributes");
		return -EINVAL;
	}

	if (tb[QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID])
		dcs_intf_event->link_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID]);

	if (tb[QCA_WLAN_VENDOR_ATTR_DCS_ENABLE])
		dcs_intf_event->type = nla_get_u16(tb[QCA_WLAN_VENDOR_ATTR_DCS_ENABLE]);

	if (tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_BITMAP])
		dcs_intf_event->chan_bw_interference_bitmap = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_BITMAP]);

	wpa_printf(MSG_ERROR,
		   "nl80211: DCS config event parsed: link_id=%u type=0x%04x interference_bitmap=0x%08x (attr_enable_present=%d attr_bitmap_present=%d)",
		   dcs_intf_event->link_id, dcs_intf_event->type,
		   dcs_intf_event->chan_bw_interference_bitmap,
		   !!tb[QCA_WLAN_VENDOR_ATTR_DCS_ENABLE],
		   !!tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_BITMAP]);

	wpa_supplicant_event(bss->ctx, EVENT_DCS_INTF, &event);

	return 0;
}

int wpa_driver_nl80211_vendor_bss_addr(void *priv, u8 radio_idx, u8 bss_id,
				       u32 mbssid_enabled,
				       enum nl80211_iftype iftype, u32 flags,
				       u8 *addr, const char *ifname)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	struct nlattr *attr;
	struct wpabuf *buf = NULL;
	int ret = -1;

	if (!drv)
		return -EINVAL;

	msg = nlmsg_alloc();
	if (!msg)
		return -ENOMEM;

	if (!genlmsg_put(msg, 0, 0, drv->global->nl80211_id, 0, 0,
			 NL80211_CMD_VENDOR, 0))
		goto fail;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_nametoindex(ifname)) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_DERIVE_LINK_BSS_ADDR))
		goto fail;

	attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr)
		goto fail;

	if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_INDEX,
		       radio_idx) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_BSS_ID,
		       bss_id) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_FLAGS,
			flags) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_IFTYPE,
			iftype))
		goto fail;

	/* Optional MBSSID group details for 6 GHz AP */
	if (mbssid_enabled &&
	    nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_ENABLED))
		goto fail;

	nla_nest_end(msg, attr);

	buf = wpabuf_alloc(ETH_ALEN + 16);
	if (!buf)
		goto fail;

	ret = send_and_recv_resp(drv, msg, mac_config_handler, buf);
	msg = NULL;
	if (ret)
		goto out;

	if (wpabuf_len(buf) < ETH_ALEN) {
		ret = -EMSGSIZE;
		goto out;
	}
	os_memcpy(addr, wpabuf_head(buf), ETH_ALEN);
	ret = 0;
out:
	if (buf)
		wpabuf_free(buf);
	return ret;
fail:
	if (msg)
		nlmsg_free(msg);
	return -ENOBUFS;
}

#ifdef CONFIG_IEEE80211BE
int wpa_driver_nl80211_vendor_cmd_notify_link_repurpose(void *priv, u8 link_id)
{
	struct nl_msg *msg;
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nlattr *params;
	int ret = -ENOBUFS;

	wpa_printf(MSG_DEBUG, "nl80211: Indication of link repurpose");

	if (drv->nlmode != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;

	msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR);
	if (!msg)
		goto error;

	if (nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_REPURPOSE_LINK_INDICATION))
		goto error;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params)
		goto error;
	if (link_id != NL80211_DRV_LINK_ID_NA &&
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_MLO_LINK_ID, link_id))
		goto error;
	nla_nest_end(msg, params);

	ret = send_and_recv(drv, bss->nl_connect, msg, NULL, NULL, NULL, NULL, NULL);
	if (ret) {
		wpa_printf(MSG_DEBUG,
			   "nl80211: link repurpose indication failed err=%d (%s)",
			   ret, strerror(-ret));
	}
	return ret;
error:
	nlmsg_free(msg);
	wpa_printf(MSG_DEBUG,
		   "nl80211: Could not indicate repurpose on link %d",
		   link_id);
	return ret;
}
#endif /* CONFIG_IEEE80211BE */

int wpa_driver_nl80211_dcs_config_extn(void *priv, u8 link_id,
				       struct driver_dcs_config *params)
{
	struct nl_msg *msg;
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nlattr *attr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "nl80211: Configure DCS (cmd_type=%u valid_mask=0x%x)",
		   params->cmd_type, params->valid_mask);

	if (drv->nlmode != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;

	if (!(msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR)) ||
	     nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	     nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
	     QCA_NL80211_VENDOR_SUBCMD_DCS_CONFIG)) {
		goto error;
	}

	attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr)
		goto error;
	if ((link_id != NL80211_DRV_LINK_ID_NA &&
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID, link_id)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_DCS_CMD_TYPE,
		       params->cmd_type) ||
	    nla_put_u16(msg, QCA_WLAN_VENDOR_ATTR_DCS_ENABLE,
			params->dcs_enable)) {
		wpa_printf(MSG_DEBUG,"nl80211: Failed to configure DCS params");
		goto error;
	}

	/* Optional params when SET */
	if (params->cmd_type == SET_DCS_CONFIG) {
		if (params->valid_mask & DCS_VALID_INTR_DET_THR)
			if (nla_put_u32(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_THRESHOLD,
			    params->intr_detection_threshold))
				goto error;
		if (params->valid_mask & DCS_VALID_PHYERR_PENALTY)
			if (nla_put_u32(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_PENALTY,
			    params->phyerr_penalty))
				goto error;
		if (params->valid_mask & DCS_VALID_PHYERR_THR)
			if (nla_put_u32(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_THRESHOLD,
			    params->phyerr_threshold))
				goto error;
		if (params->valid_mask & DCS_VALID_RADARERR_THR)
			if (nla_put_u32(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_RADAR_ERR_THRESHOLD,
			    params->radarerr_threshold))
				goto error;
		if (params->valid_mask & DCS_VALID_TXERR_THR)
			if (nla_put_u32(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_TX_ERR_THRESHOLD,
			    params->txerr_threshold))
				goto error;
		if (params->valid_mask & DCS_VALID_SAMPLE_SIZE)
			if (nla_put_u32(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_WINDOW,
			    params->sample_size))
				goto error;
		if (params->valid_mask & DCS_VALID_COCH_THR)
			if (nla_put_u8(msg,
			    QCA_WLAN_VENDOR_ATTR_DCS_COCHANNEL_INTERFERENCE_THRESHOLD,
			    params->coch_intr_threshold))
				goto error;
		if (params->valid_mask & DCS_VALID_USER_MAX_CU)
			if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_DCS_MAX_CU,
			    params->user_max_cu))
				goto error;
	}
	nla_nest_end(msg, attr);

	ret = send_and_recv_cmd(drv, msg);
	if (ret) {
		wpa_printf(MSG_DEBUG,
				"nl80211: DCS config failed=%d (%s)",
				ret, strerror(-ret));
	}
	return 0;
error:
	nlmsg_free(msg);
	wpa_printf(MSG_DEBUG, "nl80211: Could not configure DCS on link %d", link_id);
	return -1;
}

static int rropinfo_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *nl = NULL;
	struct nlattr *nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_MAX + 1];
	int rem = 0, i = 0;
	u32 num_rtplinst = 0;
	struct nl80211_rropinfo *rropinfo = (struct nl80211_rropinfo *)arg;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_VENDOR_DATA])
		goto fail;

	struct nlattr *nl_vendor = tb[NL80211_ATTR_VENDOR_DATA];
	struct nlattr *tb_vendor[QCA_WLAN_VENDOR_ATTR_RROP_INFO_MAX + 1];

	nla_parse(tb_vendor, QCA_WLAN_VENDOR_ATTR_RROP_INFO_MAX,
		  nla_data(nl_vendor), nla_len(nl_vendor), NULL);

	nl = tb_vendor[QCA_WLAN_VENDOR_ATTR_RROP_INFO_RTPL];
	if (!nl)
		goto fail;

	num_rtplinst = 0;
	nla_for_each_nested(nl,
			    tb_vendor[QCA_WLAN_VENDOR_ATTR_RROP_INFO_RTPL],
			    rem) {
		num_rtplinst++;
	}

	wpa_printf(MSG_DEBUG, "nl80211: rropinfo_handler found %u RTPL instances",
		   num_rtplinst);
	if (!num_rtplinst)
		goto fail;

	rropinfo->num_rtplinst = (num_rtplinst > MAX_NUM_CHANNELS) ?
				 MAX_NUM_CHANNELS : num_rtplinst;

	i = 0;
	nla_for_each_nested(nl,
			    tb_vendor[QCA_WLAN_VENDOR_ATTR_RROP_INFO_RTPL],
			    rem) {
		if (i >= MAX_NUM_CHANNELS)
			break;
		if (nla_parse(nl_rtplinst,
			      QCA_WLAN_VENDOR_ATTR_RTPLINST_MAX,
			      nla_data(nl), nla_len(nl), NULL)) {
			wpa_printf(MSG_ERROR,
				   "nl80211: failed to parse RTPL");
			goto fail;
		}

		if (nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_PRIMARY_FREQUENCY])
			rropinfo->rtpl[i].primary_freq =
				nla_get_u32(
				nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_PRIMARY_FREQUENCY]);

		if (nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_THROUGHPUT])
			rropinfo->rtpl[i].txpower_throughput =
				(int)nla_get_u32(
				nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_THROUGHPUT]);

		if (nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_RANGE])
			rropinfo->rtpl[i].txpower_range =
				(int)nla_get_u32(
				nl_rtplinst[QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_RANGE]);

		wpa_printf(MSG_DEBUG,
			   "nl80211: RTPL[%d] primary=%u throughput=%d range=%d",
			   i,
			   rropinfo->rtpl[i].primary_freq,
			   rropinfo->rtpl[i].txpower_throughput,
			   rropinfo->rtpl[i].txpower_range);
		i++;
	}

	return NL_SKIP;

fail:
	rropinfo->num_rtplinst = 0;
	return NL_SKIP;
}

int driver_nl80211_vendor_get_chan_rropinfo(void *ctx,
					    struct nl80211_rropinfo *rropinfo,
					    int radio_idx)
{
	int ret = -1;
	struct nl_msg *msg = NULL;
	struct i802_bss *bss = ctx;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nlattr *params;

	wpa_printf(MSG_DEBUG, "nl80211: driver_nl80211_vendor_get_chan_rropinfo start: radio_idx: %d", radio_idx);

	msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR);
	if (!msg ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_GET_RROP_INFO))
		goto fail;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params)
		goto fail;
	if (radio_idx != NL80211_WIPHY_RADIO_ID_MAX &&
	    nla_put_u8(msg,QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX, radio_idx))
		goto fail;
	nla_nest_end(msg, params);

	ret = send_and_recv_resp(drv, msg, rropinfo_handler, rropinfo);
	msg = NULL;
	if (ret) {
		wpa_printf(MSG_ERROR, "nl80211: Vendor get RROP info request failed: ret=%d (%s)",
			   ret, strerror(-ret));
		goto fail;
	}

	wpa_printf(MSG_DEBUG, "nl80211: RROP info received, num_rtplinst=%u",
		   rropinfo ? rropinfo->num_rtplinst : 0);
	return 0;

fail:
	if (msg)
		nlmsg_free(msg);
	return ret;
}

int wpa_driver_nl80211_dcs_sim_extn(void *priv, u8 link_id,
				    struct driver_dcs_sim *params)
{
	struct nl_msg *msg;
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nlattr *attr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "nl80211: Configure DCS SIM");
	if (drv->nlmode != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;

	if (!(msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_DCS_SIM)) {
		goto error;
	}

	attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr)
		goto error;

	if ((link_id != NL80211_DRV_LINK_ID_NA &&
	     nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_DCS_SIM_LINK_ID, link_id)) ||
	     nla_put_u16(msg, QCA_WLAN_VENDOR_ATTR_DCS_SIM_TYPE,
			 params->type))
		goto error;

	if (nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_DCS_SIM_INTERFERENCE_BITMAP, params->intf_bitmap))
		goto error;

	nla_nest_end(msg, attr);

	ret = send_and_recv_cmd(drv, msg);
	if (ret) {
		wpa_printf(MSG_DEBUG,
				"nl80211: DCS SIM failed=%d (%s)",
				ret, strerror(-ret));
	}
	return 0;
error:
	nlmsg_free(msg);
	wpa_printf(MSG_DEBUG, "nl80211: Could not configure DCS SIM on link %d", link_id);
	return -1;
}

int hostapd_drv_mark_vap_submode(struct hostapd_data *hapd,
				 enum qca_wlan_vendor_vap_submode_type submode)
{
	if (submode == QCA_WLAN_VENDOR_ATTR_VAP_SUBMODE_NONE)
		return 0;
	else
		return hostapd_drv_mark_vap_submode_extn(hapd->drv_priv,
				OUI_QCA,
				QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION,
				hapd->conf->iface,
				submode);

}

int hostapd_drv_mark_vap_submode_extn(void *priv, unsigned int vendor_id,
					      unsigned int subcmd,
					      const char *ifname,
					      u8 vap_submode)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	struct nlattr *attr;
	int ret;
	int ifidx;

	if (!bss || !drv)
		return -EINVAL;

	ifidx = if_nametoindex(ifname);

	msg = nlmsg_alloc();
	if (!msg)
		return -EINVAL;

	if (!genlmsg_put(msg, 0, 0, drv->global->nl80211_id,
			0,  0, NL80211_CMD_VENDOR, 0))
		goto fail;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifidx) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, vendor_id) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD, subcmd))
		goto fail;

	attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr)
		goto fail;
	if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_VAP_SUBMODE,
		       vap_submode))
		goto fail;

	nla_nest_end(msg, attr);

	ret = send_and_recv_cmd(drv, msg);
	if (ret)
		wpa_printf(MSG_ERROR, "nl80211: vendor command - vap_submode: %dfailed err=%d",
			   vap_submode, ret);
	else {
		wpa_printf(MSG_INFO, "nl80211: vendorcmd vap_submode: ifname %s vap_submode %d",
			   ifname, vap_submode);
	}
	return ret;
fail:
	nlmsg_free(msg);
	return -ENOBUFS;
}

int nl80211_set_he_mcs_12_13_peer_cap_extn(void *priv, u8 radio_idx,
					   u16 peer_cap)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	struct nlattr *params;
	int ret;

	wpa_printf(MSG_DEBUG,
		   "nl80211: Set HE_MCS_12_13 peer capability = 0x%04x for radio_idx: %d",
		   peer_cap, radio_idx);
	msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR);
	if (!msg)
		return -ENOMEM;

	if (nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION))
		goto fail;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params)
		goto fail;

	if (nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_COMMAND,
			QCA_NL80211_VENDOR_SUBCMD_HE_MCS_12_13_SUPP) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX,
		       radio_idx) ||
	    nla_put(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_DATA,
		    sizeof(peer_cap), &peer_cap)) {
		nla_nest_end(msg, params);
		goto fail;
	}

	nla_nest_end(msg, params);
	ret = send_and_recv_cmd(drv, msg);
	if (ret)
		wpa_printf(MSG_ERROR,
			   "nl80211: Setting HE_MCS_12_13_PEER_CAP failed: %d (%s)",
			   ret, strerror(-ret));
	return ret;
fail:
	nlmsg_free(msg);
	return -ENOBUFS;
}

static int nl80211_get_he_mcs_12_13_handler(struct nl_msg *msg, void *arg)
{
	u16 *radio_cap = arg;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlattr *tb_vendor[QCA_WLAN_VENDOR_ATTR_CONFIG_MAX + 1];
	struct nlattr *data_attr;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_VENDOR_DATA])
		return NL_SKIP;

	if (nla_parse_nested(tb_vendor, QCA_WLAN_VENDOR_ATTR_CONFIG_MAX,
			     tb[NL80211_ATTR_VENDOR_DATA], NULL))
		return NL_SKIP;

	data_attr = tb_vendor[QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_DATA];
	if (!data_attr || nla_len(data_attr) < (int)sizeof(u16))
		return NL_SKIP;

	*radio_cap = *(u16 *)nla_data(data_attr);
	return NL_SKIP;
}

int nl80211_get_he_mcs_12_13_extn(void *priv, u8 radio_idx, u16 *radio_cap)
{
	struct i802_bss *bss = priv;
	struct wpa_driver_nl80211_data *drv = bss->drv;
	struct nl_msg *msg;
	struct nlattr *params;
	u16 cap = 0;
	int ret;

	wpa_printf(MSG_DEBUG,
		   "nl80211: Get the HE_MCS_12_13 hardware capability for radio_idx: %d",
		   radio_idx);
	msg = nl80211_bss_msg(bss, 0, NL80211_CMD_VENDOR);
	if (!msg)
		return -ENOMEM;

	if (nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_CONFIGURATION))
		goto fail;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params)
		goto fail;

	if (nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_COMMAND,
			QCA_NL80211_VENDOR_SUBCMD_HE_MCS_12_13_SUPP) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX,
		       radio_idx)) {
		nla_nest_end(msg, params);
		goto fail;
	}

	nla_nest_end(msg, params);
	ret = send_and_recv_resp(drv, msg,
				 nl80211_get_he_mcs_12_13_handler, &cap);
	if (ret) {
		wpa_printf(MSG_ERROR,
			   "nl80211: GET_HE_MCS_12_13 failed: %d (%s) for radio_idx: %d",
			   ret, strerror(-ret), radio_idx);
		return ret;
	}

	*radio_cap = cap;
	return 0;
fail:
	nlmsg_free(msg);
	return -ENOBUFS;
}
