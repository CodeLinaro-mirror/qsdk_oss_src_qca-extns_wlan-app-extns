CFLAGS += -DCONFIG_QCN_EXTN

OBJS+= \
	qcn_extns/240mhz.o \
	qcn_extns/driver_nl80211_extn.o \
	qcn_extns/ap_config_extn.o \
	qcn_extns/ctrl_iface_extn.o \
	qcn_extns/drv_callbacks_extn.o \
	qcn_extns/hostapd_rptr_extn.o \
	qcn_extns/esp.o \
	qcn_extns/rnr.o \
	qcn_extns/macaddr.o \
	qcn_extns/dfs_extn.o \
	qcn_extns/ucode_extn.o \
	qcn_extns/wpa_supplicant_extn.o \
	qcn_extns/acs_extn.o \
	qcn_extns/mucapwar.o \
	qcn_extns/dcs.o

ifdef CONFIG_EXTERNAL_PLUGIN
CFLAGS += -DHOSTAPD_EXTERNAL_PLUGIN
OBJS += qcn_extns/hostapd_if_eloop.o
endif

ifneq ($(wildcard ../qcn_extns/qacs/qacs.c),)
	CFLAGS += -DCONFIG_QCN_APP_EXTN
	OBJS += qcn_extns/qacs/qacs.o
endif

