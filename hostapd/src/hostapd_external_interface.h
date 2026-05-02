/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */


#ifndef _HOSTAPD_EXTERNAL_INTERFACE_H
#define _HOSTAPD_EXTERNAL_INTERFACE_H

/*
 * External interface definitions between hostapd and plugin
 */

#include <stdint.h>
#include <stdbool.h>
#include "hostapd_if/hostapd_if_common.h"

/*
 * Optional additional match length macro (disabled)
 *
 * #define MAX_ADDITIONAL_MATCH_LEN 20
 */

#ifndef MAX_MLO_LINKS
#define MAX_MLO_LINKS 8
#endif

#ifndef MAX_INTERFACE_NAME
#define MAX_INTERFACE_NAME 16
#endif

/*
 * Maximum lengths for PTK components passed via get/set APIs
 */
#ifndef MAX_KCK_LEN
#define MAX_KCK_LEN 24
#endif

#ifndef MAX_KEK_LEN
#define MAX_KEK_LEN 64
#endif

#ifndef MAX_TK_LEN
#define MAX_TK_LEN 32
#endif

/*
 * Macros referenced by plugin APIs; redefine locally to avoid extra
 * includes
 */

/*
 * from src/common/wpa_common.h
 */
#ifndef PMK_LEN_MAX
#define PMK_LEN_MAX 64
#endif

/*
 * from src/common/wpa_common.h
 */
#ifndef PMKID_LEN
#define PMKID_LEN 16
#endif

/*
 * match WPA_GTK_MAX_LEN from src/common/wpa_common.h
 */
#ifndef MAX_GTK_LEN
#define MAX_GTK_LEN 32
#endif

/*
 * match WPA_PMK_NAME_LEN from src/common/wpa_common.h
 */
#ifndef PMK_R1_NAME_LEN
#define PMK_R1_NAME_LEN 16
#endif

/*
 * Maximum EAP/RADIUS identity length; matches the check in
 * hostapd/radius.c (identity_len > 512).
 */
#ifndef MAX_IDENTITY_LEN
#define MAX_IDENTITY_LEN 512
#endif

/*
 * Maximum RADIUS Chargeable-User-Identity (CUI) attribute length;
 * bounded by the maximum RADIUS attribute value size (RFC 2865).
 */
#ifndef MAX_RADIUS_CUI_LEN
#define MAX_RADIUS_CUI_LEN 253
#endif

enum hostapd_if_frame_policy {
	HOSTAPD_IF_FRAME_DO_NOTHING,
	HOSTAPD_IF_FRAME_NOTIFY,
	HOSTAPD_IF_FRAME_INVOKE
};

enum hostapd_if_frame_reg_type {
	HOSTAPD_IF_FRAME_TYPE_AUTH,
	HOSTAPD_IF_FRAME_TYPE_ASSOC,
	HOSTAPD_IF_FRAME_TYPE_ACTION,
	HOSTAPD_IF_FRAME_TYPE_DEAUTH,
	HOSTAPD_IF_FRAME_TYPE_DISASSOC,
	HOSTAPD_IF_FRAME_TYPE_PROBE,
	HOSTAPD_IF_FRAME_TYPE_RRB,
	HOSTAPD_IF_FRAME_TYPE_MAX
};

struct hostapd_if_frame_category {
	enum hostapd_if_frame_reg_type type;
	union {
		struct {
			uint8_t category;
			struct action_field action;
		} action;
	} u;
};

/*
 * PMK-R1 container
 *
 * All variable-length fields are stored as fixed-size arrays so that
 * the structure can be copied by value and no individual members need
 * to be freed by the receiver.
 */
struct hostapd_if_pmk_r1 {
	uint8_t pmk_r1[PMK_LEN_MAX];
	uint8_t pmk_r1_len;
	uint8_t pmk_r1_name[PMK_R1_NAME_LEN];
	int pairwise;

	int expires_in;
	int session_timeout;

	uint8_t identity[MAX_IDENTITY_LEN];
	uint8_t identity_len;

	uint8_t radius_cui[MAX_RADIUS_CUI_LEN];
	uint16_t radius_cui_len;
};

struct hostapd_if_frame_ctx {
	int rx_link_id;
	int status_code;

	union {
		struct {
			int allow_reuse;
			uint16_t auth_alg;

			/*
			 * transaction only relevant for SAE algorithm
			 */
			uint16_t auth_transaction;

			/*
			 * status code is outside
			 */
			uint8_t sta_assoc_link_mac[6];
		} auth_req; /* not valid for remote auth (FT-over-DS) */

		struct {
			int allow_reuse;
			uint16_t auth_transaction;
			uint16_t auth_alg;
			uint8_t sta_assoc_link_mac[6];
			struct hostapd_if_pmk_r1 *pmk_r1;

			/*
			 * status code is outside
			 */
			uint8_t *additional_ies;
			uint16_t additional_ies_len;
		} auth_resp;

		struct {
			int is_reassoc;
			int rssi;
			uint32_t valid_link_bitmap;
			uint8_t sta_link_mac[MAX_MLO_LINKS][6];
			uint8_t sta_assoc_link_mac[6];
		} assoc_req;

		struct {
			int is_reassoc;
			int rssi;
			uint8_t sta_assoc_link_mac[6];
			uint8_t *additional_ies;
			uint16_t additional_ies_len;
		} assoc_resp;

		struct {
			uint8_t category;
			uint8_t action_code;
		} action;
	} data;
};

enum hostapd_if_event_type {
	HOSTAPD_IF_EVENT_AUTH_TX_COMPLETE,
	HOSTAPD_IF_EVENT_ASSOC_TX_COMPLETE,
	HOSTAPD_IF_EVENT_AUTHORIZE_COMPLETION,
	/*
	 * deauth / disassoc should be sent in tx-status also
	 */
	HOSTAPD_IF_EVENT_DEAUTH,
	HOSTAPD_IF_EVENT_DISASSOC,
	HOSTAPD_IF_EVENT_SA_QUERY_COMPLETION,
	HOSTAPD_IF_EVENT_GTK_COMPLETION,
	HOSTAPD_IF_EVENT_EAPOL_M2_RECEIVED,
	HOSTAPD_IF_EVENT_ACTION_COMPLETION,
	HOSTAPD_IF_EVENT_INBOUND_CALL_ERROR,
	HOSTAPD_IF_EVENT_MAX
};

struct hostapd_if_event {
	enum hostapd_if_event_type type;

	char ifname[MAX_INTERFACE_NAME];

	/*
	 * sta_mac not relevant for HOSTAPD_IF_EVENT_GTK_COMPLETION
	 */
	uint8_t sta_mac[6];

	union {
		struct {
			int ok;
			uint16_t status;
			uint16_t aid;
		} assoc_resp_completion;

		struct {
			int link_id;
			uint8_t link_mac[6];
			bool is_tx_status;
			int tx_status_ok;
			enum hostapd_if_disconnect_type type;
			uint16_t reason_code;
		} deauth_disassoc;

		struct {
			enum hostapd_if_sa_query_status status;
		} sa_query;

		struct {
			int authorized;
		} authorize_completion;
		struct {
			enum HOSTAPD_IF_INBOUND_ERROR {
				HOSTAPD_IF_AUTH_RESPONSE_ERROR,
				HOSTAPD_IF_ASSOC_RESPONSE_ERROR,
				HOSTAPD_IF_SEND_DISCONNECT_ERROR,
				HOSTAPD_IF_TRIGGER_EAPOL_M3_ERROR,
				HOSTAPD_IF_SET_BEACON_PROBE_VENDOR_IES_ERROR,
				HOSTAPD_IF_SET_PMK_ERROR,
				HOSTAPD_IF_SET_PTK_ERROR,
				HOSTAPD_IF_SET_GTK_ERROR,
				HOSTAPD_IF_START_SA_QUERY_ERROR,
			} type;
			const char *func;
			int line_num;
		} inbound_call_error;
	} data;
};


struct hostapd_external_app_object {
	/*
	 * Northbound
	 */
	void (*init)();

	void (*interface_create)(char *ifname, void *ctx);
	void (*invoke_assoc)(char *ifname, uint8_t *sta_mac,
			     const uint8_t *frame, uint16_t frame_len,
			     struct hostapd_if_frame_ctx *ctx);

	void (*invoke_auth)(char *ifname, uint8_t *sta_mac,
			    const uint8_t *frame, uint16_t frame_len,
			    struct hostapd_if_frame_ctx *ctx);

	void (*notify_assoc)(char *ifname, uint8_t *sta_mac,
			     const uint8_t *frame, uint16_t frame_len,
			     struct hostapd_if_frame_ctx *ctx);

	void (*notify_auth)(char *ifname, uint8_t *sta_mac,
			    const uint8_t *frame, uint16_t frame_len,
			    struct hostapd_if_frame_ctx *ctx);

	void (*notify_deauth)(char *ifname, uint8_t *sta_mac,
			      const void *frame, size_t frame_len,
			      struct hostapd_if_frame_ctx *ctx);

	void (*notify_disassoc)(char *ifname, uint8_t *sta_mac,
				const void *frame, size_t frame_len,
				struct hostapd_if_frame_ctx *ctx);

	void (*notify_event)(struct hostapd_if_event *event);

	void (*eapol_rx)(char *ifname, uint8_t link_id,
			 uint8_t *frame, uint16_t frame_len);

	void (*eapol_key_rx)(char *ifname, uint8_t link_id,
			     uint8_t *frame, uint16_t frame_len);

	void (*invoke_action)(char *ifname, uint8_t *sta_mac,
			      uint8_t *frame, uint16_t frame_len,
			      struct hostapd_if_frame_ctx *ctx);

	void (*invoke_remote_auth)(char *ifname, uint8_t *sta_mac,
				   uint8_t *auth_body,
				   uint16_t auth_body_len,
				   struct hostapd_if_frame_ctx *ctx);

	void (*notify_action)(char *ifname, uint8_t *sta_mac,
			      uint8_t *frame, uint16_t frame_len,
			      int link_id);

	void (*notify_remote_auth)(char *ifname, uint8_t *sta_mac,
				   uint8_t *auth_body,
				   uint16_t auth_body_len,
				   int link_id);

	/*
	 * Southbound
	 */

	/*
	 * All southbound calls should be originated from this callback
	 * handler, since this will be called from the eloop
	 */
	void (*register_frame)(void *ifname_ctx,
			       struct hostapd_if_frame_category *cat,
			       enum hostapd_if_frame_policy policy);

	void (*register_event)(void *ifname_ctx,
			       enum hostapd_if_event_type type,
			       bool set);

	/*
	 * ASYNC: Association response
	 */
	int (*assoc_response)(char *ifname, uint8_t *sta_mac,
			      struct hostapd_if_frame_ctx *ctx);

	/*
	 * ASYNC: Authentication response
	 */
	int (*auth_response)(char *ifname, uint8_t *sta_mac,
			     struct hostapd_if_frame_ctx *ctx);

	/*
	 * ASYNC: Send Deauthentication
	 */
	int (*send_deauth)(char *ifname, uint8_t *sta_mac,
			   uint16_t reason_code, int link_id,
			   uint8_t *added_data,
			   uint8_t added_data_len);

	/*
	 * ASYNC: Send Disassociation
	 */
	int (*send_disassoc)(char *ifname, uint8_t *sta_mac,
			     uint16_t reason_code, int link_id,
			     uint8_t *added_data,
			     uint8_t added_data_len);

	/*
	 * ASYNC: Trigger EAPOL M3
	 */
	int (*trigger_eapol_m3)(char *ifname, uint8_t *sta_mac);

	/*
	 * ASYNC: Set beacon/probe vendor IEs
	 */
	int (*set_beacon_probe_vendor_ies)(char *ifname,
					   uint8_t *buf, size_t buf_len,
					   int link_id);

	/*
	 * ASYNC: Set PMK
	 */
	int (*set_pmk)(char *ifname, uint8_t *sta_mac,
		       uint8_t *pmk, size_t pmk_len, uint8_t *pmkid);

	int (*get_pmk)(char *ifname, uint8_t *sta_mac,
		       uint8_t pmk[PMK_LEN_MAX], size_t *pmk_len,
		       uint8_t pmkid[PMKID_LEN]);

	/*
	 * ASYNC: Set PTK
	 */
	int (*set_ptk)(char *ifname, uint8_t *sta_mac,
		       uint8_t *kck, size_t kck_len,
		       uint8_t *kek, size_t kek_len,
		       uint8_t *tk, size_t tk_len);

	int (*get_ptk)(char *ifname, uint8_t *sta_mac,
		       uint8_t kck[MAX_KCK_LEN], size_t *kck_len,
		       uint8_t kek[MAX_KEK_LEN], size_t *kek_len,
		       uint8_t tk[MAX_TK_LEN], size_t *tk_len);

	/*
	 * ASYNC: Set GTK
	 */
	int (*set_gtk)(char *ifname, int link_id, int gtk_idx,
		       uint8_t *gtk, size_t gtk_len);

	int (*get_gtk)(char *ifname, int link_id, int *gtk_idx,
		       const uint8_t gtk[MAX_GTK_LEN], size_t *gtk_len);

	/*
	 * ASYNC: Start SA Query
	 */
	int (*start_sa_query)(char *ifname, uint8_t *sta_mac, int link_id);

	void (*set_authorized)(char *ifname, uint8_t *sta_mac);

	/*
	 * ASYNC: Send action frame
	 */
	void (*send_action)(char *ifname, uint8_t *sta_mac,
			    uint8_t *frame, int frame_len,
			    struct hostapd_if_frame_ctx *ctx);

	/*
	 * ASYNC: Remote auth response
	 */
	void (*remote_auth_response)(char *ifname, uint8_t *sta_mac,
				     struct hostapd_if_frame_ctx *ctx);

	/*
	 * ASYNC: EAPOL Tx
	 */
	void (*eapol_tx)(char *ifname, uint8_t link_id, uint8_t *frame,
			 uint16_t frame_len);

	/*
	 * ASYNC: EAPOL-Key Tx
	 */
	void (*eapol_key_tx)(char *ifname, uint8_t link_id,
			     uint8_t *frame, uint16_t frame_len);

	/*
	 * ASYNC: Send generic frame
	 */
	void (*send_frame)(char *ifname, uint8_t link_id, uint8_t *frame,
			   uint16_t frame_len);

	/*
	 * PMK-R1 setter (disabled)
	 *
	 * void (*set_pmk_r1)(char *ifname, uint8_t *sta_mac,
	 *                    struct hostapd_if_pmk_r1 *);
	 */
};

/*
 * Call from library constructor. This has to be called before
 * hostapd's main.
 */
void hostapd_plugin_register(struct hostapd_external_app_object *plugin);

#endif /* _HOSTAPD_EXTERNAL_INTERFACE_H */
