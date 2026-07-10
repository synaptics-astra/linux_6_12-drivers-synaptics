###############################################################################
#
# Common avio kernel module objects
#
###############################################################################
AVIO_ABS_PATH := $(srctree)/drivers/synaptics/soc/berlin/modules/avio
AVIO_INC_PATH := -I$(AVIO_ABS_PATH)
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/include
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/common

AVIO_COMMON_API_OBJS := avio_core.o \
						avio_util.o \
						avio_sub_module.o \

ifneq ($(filter y, $(CONFIG_AVIO_PLATYPUS) $(CONFIG_AVIO_DOLPHIN) $(CONFIG_AVIO_DOLPHIN_A0)), )
ccflags-y += -DCONFIG_SUPPORT_VPP_TA
USE_VPP_TA := 1
else
USE_VPP_TA := 0
ifeq ($(CONFIG_AVIO_KLAMATH),y)
SOC_FOLDER := klamath
ccflags-y += -DUSE_KLAMATH=1
else
SOC_FOLDER := myna2
ccflags-y += -DUSE_MYNA=1
endif
endif
###############################################################################
# Fastcall - avio kernel sub-module objects
###############################################################################
AVIO_FASTCALL_API_OBJS := fastcall/hal_dhub_fastcall_wrap.o
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/fastcall/include

ifeq ($(CONFIG_OPTEE),y)
AVIO_INC_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/vpp/include
else
AVIO_INC_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/tzd/tee/client_api \
				 -I$(srctree)/drivers/synaptics/soc/berlin/modules/tzd/tee/common \
				 -I$(srctree)/drivers/synaptics/soc/berlin/modules/tzd/tz/common \
				 -I$(srctree)/drivers/synaptics/soc/berlin/modules/tzd/include \
				 -I$(srctree)/drivers/synaptics/soc/berlin/modules/tzd
endif

###############################################################################
# AIO - aio kernel sub-module objects
###############################################################################
AVIO_AIO_API_OBJS := aio/drv_aio.o
AVIO_AIO_API_OBJS += aio/aio_common.o
AVIO_AIO_API_OBJS += aio/i2s_common.o
AVIO_AIO_API_OBJS += aio/pdm_common.o
ifneq ($(filter y, $(CONFIG_AVIO_PLATYPUS) $(CONFIG_AVIO_DOLPHIN) $(CONFIG_AVIO_DOLPHIN_A0)), )
AVIO_AIO_API_OBJS += aio/spdifi_common.o
endif
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio

ifeq ($(CONFIG_AVIO_AS370),y)
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio/as370
AVIO_AIO_API_OBJS += aio/as370/i2s.o
AVIO_AIO_API_OBJS += aio/as370/pdm.o
endif

ifeq ($(CONFIG_AVIO_MYNA),y)
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio/myna
AVIO_AIO_API_OBJS += aio/myna/i2s.o
AVIO_AIO_API_OBJS += aio/myna/pdm.o
AVIO_AIO_API_OBJS += aio/myna/dmic.o
AVIO_AIO_API_OBJS += aio/myna/misc.o
endif

ifeq ($(filter y,$(CONFIG_AVIO_KLAMATH) $(CONFIG_AVIO_MYNA2)),y)
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio/$(SOC_FOLDER)
AVIO_AIO_API_OBJS += aio/$(SOC_FOLDER)/i2s.o
AVIO_AIO_API_OBJS += aio/$(SOC_FOLDER)/pdm.o
AVIO_AIO_API_OBJS += aio/$(SOC_FOLDER)/dmic.o
AVIO_AIO_API_OBJS += aio/$(SOC_FOLDER)/misc.o
AVIO_AIO_API_OBJS += aio/$(SOC_FOLDER)/apll.o
endif

ifneq ($(filter y, $(CONFIG_AVIO_PLATYPUS) $(CONFIG_AVIO_DOLPHIN) $(CONFIG_AVIO_DOLPHIN_A0)), )
ifeq ($(CONFIG_AVIO_PLATYPUS),y)
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio/vsxxx/platypus
endif
ifeq ($(CONFIG_AVIO_DOLPHIN),y)
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio/vsxxx/dolphin
else
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/aio/vsxxx/dolphin_a0
endif

ifeq ($(CONFIG_AVIO_PLATYPUS),y)
AVIO_AIO_API_OBJS += aio/vsxxx/platypus/aio_priv.o
AVIO_AIO_API_OBJS += aio/vsxxx/platypus/apll.o
endif
# dolphinz1 share the same aio_priv.c with platypus
ifeq ($(CONFIG_AVIO_DOLPHIN),y)
AVIO_AIO_API_OBJS += aio/vsxxx/platypus/aio_priv.o
AVIO_AIO_API_OBJS += aio/vsxxx/dolphin/apll.o
endif
ifeq ($(CONFIG_AVIO_DOLPHIN_A0),y)
AVIO_AIO_API_OBJS += aio/vsxxx/dolphin_a0/aio_priv.o
AVIO_AIO_API_OBJS += aio/vsxxx/earc.o
AVIO_AIO_API_OBJS += aio/vsxxx/dolphin_a0/apll.o
endif

AVIO_AIO_API_OBJS += aio/vsxxx/i2s.o
AVIO_AIO_API_OBJS += aio/vsxxx/pdm.o
AVIO_AIO_API_OBJS += aio/vsxxx/dmic.o
AVIO_AIO_API_OBJS += aio/vsxxx/misc.o
endif

###############################################################################
# VPP - avio kernel sub-module objects
###############################################################################
###############################################################################
# VPP : code specific to tz version
###############################################################################
ifeq ($(USE_VPP_TA), 1)
AVIO_VPP_API_OBJS := vpp/drv_vpp.o
ifeq ($(CONFIG_OPTEE),y)
AVIO_VPP_API_OBJS += vpp/ca/source/tee_ca_vpp_optee.o
else
AVIO_VPP_API_OBJS += vpp/ca/source/tee_ca_vpp.o
endif
AVIO_VPP_API_OBJS += vpp/ca/source/hal_vpp_tz.o
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/vpp/ca/include/vsxxx
endif

###############################################################################
# VPP : code specific to ntz version
###############################################################################
ifeq ($(USE_VPP_TA), 0)

###############################################################################
# LCDC - VPP kernel sub-module objects
###############################################################################
LCDC_ABS_PATH := $(AVIO_ABS_PATH)/lcdc

LCDC_INC_PATH := -I$(LCDC_ABS_PATH)/include/

ccflags-y += $(LCDC_INC_PATH)

AVIO_INC_PATH  += -I$(AVIO_ABS_PATH)/lcdc/include/
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/vpp/common/include
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/vpp/ca/include/asxxx
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/lcdc/include

AVIO_VPP_API_OBJS += vpp/ca/source/hal_vpp_ntz.o
AVIO_VPP_API_OBJS += vpp/common/source/bcmbuf.o
AVIO_VPP_API_OBJS += vpp/common/source/framequeue.o
AVIO_VPP_API_OBJS += lcdc/source/syna_lcdc_dlr.o
AVIO_VPP_API_OBJS += lcdc/source/syna_lcdc_dev.o
AVIO_VPP_API_OBJS += lcdc/source/$(SOC_FOLDER)/lcdc_cfg_prv.o
endif

AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/lcdc/rpmsg
ifeq ($(CONFIG_AVIO_KLAMATH)$(CONFIG_SYNA_RPMSG),yy)
AVIO_VPP_API_OBJS += lcdc/rpmsg/rpmsg_client_disp.o
else
AVIO_VPP_API_OBJS += lcdc/rpmsg/dummy/rpmsg_client_disp.o
endif

###############################################################################
# VPP : Common code for tz & ntz version
###############################################################################
AVIO_VPP_API_OBJS += vpp/ca/source/hal_vpp_wrap.o

ifeq ($(USE_VPP_TA), 1)
AVIO_VPP_API_OBJS += vpp/ca/source/dummy/hal_vpp_ntz.o
else
AVIO_VPP_API_OBJS += vpp/ca/source/dummy/hal_vpp_tz.o
endif

AVIO_INC_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/vpp/include
AVIO_INC_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/vpp_mem/include
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/vpp/ca/include
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/vpp

###############################################################################
# DHUB - avio kernel sub-module objects
###############################################################################
AVIO_DHUB_API_OBJS := dhub/drv_dhub.o
AVIO_DHUB_API_OBJS += dhub/avio_dhub_drv.o
AVIO_DHUB_API_OBJS += dhub/hal/hal_dhub_wrap.o
AVIO_DHUB_API_OBJS += dhub/hal/hal_dhub.o
ifeq ($(CONFIG_OPTEE),y)
AVIO_DHUB_API_OBJS += dhub/ca/tee_ca_dhub_optee.o
else
AVIO_DHUB_API_OBJS += dhub/ca/tee_ca_dhub.o
endif

AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/dhub
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/dhub/ca
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/dhub/hal
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/dhub/include

###############################################################################
# HDMITX - avio kernel sub-module objects
###############################################################################
AVIO_HDMITX_API_OBJS := hdmitx/drv_hdmitx.o
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/hdmitx

###############################################################################
# regmap - avio kernel sub-module objects
###############################################################################
AVIO_REGMAP_API_OBJS := common/regmap/regmap_base.o
AVIO_REGMAP_API_OBJS += common/regmap/avio_regmap.o
AVIO_INC_PATH += -I$(AVIO_ABS_PATH)/common/regmap

###############################################################################

AVIO_API_OBJS := $(AVIO_COMMON_API_OBJS) $(AVIO_VPP_API_OBJS) $(AVIO_DHUB_API_OBJS)
AVIO_API_OBJS += $(AVIO_FASTCALL_API_OBJS) $(AVIO_REGMAP_API_OBJS)
AVIO_API_OBJS += $(AVIO_AIO_API_OBJS) $(AVIO_HDMITX_API_OBJS)

###############################################################################
#
# Chip specific section
#
###############################################################################
ifeq ($(CONFIG_AVIO_AS370),y)
AVIO_CFG_PATH += -I$(AVIO_ABS_PATH)/fw_inc/as370
AVIO_DHUB_CFG_OBJS += dhub/configs/as370/avio_dhub_cfg.o
endif

ifeq ($(USE_VPP_TA), 0)
AVIO_CFG_PATH += -I$(AVIO_ABS_PATH)/lcdc/include/$(SOC_FOLDER)
AVIO_CFG_PATH += -I$(AVIO_ABS_PATH)/fw_inc/$(SOC_FOLDER)
AVIO_DHUB_CFG_OBJS += dhub/configs/$(SOC_FOLDER)/avio_dhub_cfg.o
AVIO_DHUB_CFG_OBJS += dhub/configs/$(SOC_FOLDER)/hal_dhub_ext.o
else
ifeq ($(CONFIG_AVIO_PLATYPUS),y)
AVIO_CFG_PATH += -I$(AVIO_ABS_PATH)/fw_inc/platypus
AVIO_DHUB_CFG_OBJS += dhub/configs/vsxxx/platypus/dhub_priv.o
else ifneq ($(filter y, $(CONFIG_AVIO_DOLPHIN) $(CONFIG_AVIO_DOLPHIN_A0)), )
ccflags-y += -DCONFIG_SUPPORT_DHUB_TA
ifeq ($(CONFIG_AVIO_DOLPHIN),y)
AVIO_CFG_PATH += -I$(AVIO_ABS_PATH)/fw_inc/dolphin
else ifeq ($(CONFIG_AVIO_DOLPHIN_A0),y)
AVIO_CFG_PATH += -I$(AVIO_ABS_PATH)/fw_inc/dolphina0
endif
AVIO_DHUB_CFG_OBJS += dhub/configs/vsxxx/dolphin/dhub_priv.o
endif
AVIO_DHUB_CFG_OBJS += vpp/configs/vsxxx/drv_vpp_cfg.o
AVIO_DHUB_CFG_OBJS += dhub/configs/vsxxx/avio_dhub_cfg.o
AVIO_DHUB_CFG_OBJS += dhub/configs/vsxxx/hal_dhub_ext.o
endif

AVIO_CFG_OBJS := $(AVIO_DHUB_CFG_OBJS)

###############################################################################
#
# Common MsgQ kernel module objects
#
###############################################################################
MSGQ_INC_PATH := -I$(srctree)/drivers/synaptics/soc/berlin/modules/msgQ

EXTERN_INC_PATH := $(MSGQ_INC_PATH)


###############################################################################
#
# Common macro/defines
#
###############################################################################

###############################################################################
#Final part of the Makefile
###############################################################################

AVIO_ALL_OBJS := $(AVIO_API_OBJS) $(AVIO_CFG_OBJS)

avio-objs += $(AVIO_ALL_OBJS) \


HEADER_PATH +=  $(AVIO_INC_PATH)    \
				$(EXTERN_INC_PATH)  \
				$(AVIO_CFG_PATH)    \

HEADER_PATH  += -I$(srctree)/drivers/synaptics/include/


ccflags-y += $(HEADER_PATH)
ccflags-y += -Werror

obj-$(CONFIG_BERLIN_AVIO) += avio.o
