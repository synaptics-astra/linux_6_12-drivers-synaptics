AVIO_ABS_PATH := $(srctree)/drivers/synaptics/soc/berlin/modules/avio
DRM_ABS_PATH := $(srctree)/drivers/synaptics/soc/berlin/modules/drm
SYNA_DRM_TYPE :=

HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/vpp \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/vpp/ca/include/ \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/common/ \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/ \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/include \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/vpp/include \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/vpp_mem/include \
               -I$(srctree)/drivers/synaptics/soc/berlin/modules/msgQ/ \
               -I$(srctree)/drivers/synaptics/include/



ifneq ($(filter y, $(CONFIG_AVIO_PLATYPUS) $(CONFIG_AVIO_DOLPHIN) $(CONFIG_AVIO_DOLPHIN_A0)), )
    SYNA_DRM_TYPE = vsxxx
else ifneq ($(filter y, $(CONFIG_AVIO_MYNA2) $(CONFIG_AVIO_MYNA) $(CONFIG_AVIO_KLAMATH)), )
    SYNA_DRM_TYPE = asxxx
    ifeq ($(CONFIG_AVIO_MYNA2),y)
    SOC_FOLDER := myna2
    ccflags-y += -DUSE_MYNA=1
    else ifeq ($(CONFIG_AVIO_KLAMATH),y)
    SOC_FOLDER := klamath
    ccflags-y += -DUSE_KLAMATH=1
    endif
endif

SYNA_DRM_DRV_ABS_PATH = $(DRM_ABS_PATH)/$(SYNA_DRM_TYPE)
HEADER_PATH += -I$(AVIO_ABS_PATH)/vpp/ca/include/$(SYNA_DRM_TYPE)
HEADER_PATH += -I$(SYNA_DRM_DRV_ABS_PATH)
HEADER_PATH += -I$(SYNA_DRM_DRV_ABS_PATH)/dsi/
HEADER_PATH += -I$(DRM_ABS_PATH)

ifeq ($(CONFIG_AVIO_DOLPHIN),y)
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/fw_inc/dolphin
ccflags-y += -DUSE_DOLPHIN=1
else ifeq ($(CONFIG_AVIO_DOLPHIN_A0),y)
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/fw_inc/dolphina0
ccflags-y += -DUSE_DOLPHIN=1
else ifeq ($(CONFIG_AVIO_PLATYPUS),y)
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/fw_inc/platypus
ccflags-y += -DUSE_PLATYPUS=1
else ifeq ($(SYNA_DRM_TYPE), asxxx)
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/fw_inc/$(SOC_FOLDER)
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/avio/lcdc/include/$(SOC_FOLDER)
ccflags-y += -DGEN_3
endif

# Rotation can be supported from GPU
# Avoid memory allocation as memory is less in this chips
ccflags-y += -DCONFIG_SYNA_DRM_DISABLE_ROTATION

COMP_K_VER := 5
COMP_K_PLVL := 10
COMP_K_SLVL := 0

KV_GT_5_10_0 := $(shell [ $(VERSION) -gt $(COMP_K_VER) -o \
    \( $(VERSION) -eq $(COMP_K_VER) -a $(PATCHLEVEL) -ge $(COMP_K_PLVL) \) -o \
    \( \( $(VERSION) -eq $(COMP_K_VER) -o $(PATCHLEVEL) -eq $(COMP_K_PLVL) -a \
    $(SUBLEVEL) -ge $(COMP_K_SLVL) \) \) ] && echo "true")


ifeq ($(KV_GT_5_10_0), true)
     ccflags-y += -DKERNEL_VERSION_GT_5_10_0=1
endif

HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/drm/port
ifeq ($(KV_GT_5_10_0), true)
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/drm/port/dmabuf
else
HEADER_PATH += -I$(srctree)/drivers/synaptics/soc/berlin/modules/drm/port/ion
endif

HEADER_PATH += -I$(AVIO_ABS_PATH)/lcdc/include

HEADER_PATH += -I$(SYNA_DRM_TYPE)
ccflags-y += $(HEADER_PATH)
ccflags-y += -Werror

obj-$(CONFIG_SYNA_DRM) +=syna_drm.o
syna_drm-y += drm_syna_crtc.o
syna_drm-y += drm_syna_debugfs.o
syna_drm-y += drm_syna_drv.o
syna_drm-y += drm_syna_gem.o
syna_drm-y += drm_syna_plane.o
syna_drm-y += drm_syna_modeset.o
syna_drm-y += syna_vpp.o
syna_drm-y += syna_vpp_config.o

syna_drm-y += syna_logo_load.o

ifeq ($(SYNA_DRM_TYPE), asxxx)
syna_drm-y += $(SYNA_DRM_TYPE)/drm_syna_lcdc.o
syna_drm-y += $(SYNA_DRM_TYPE)/drm_syna_encoder.o
syna_drm-y += $(SYNA_DRM_TYPE)/syna_drm_priv.o
syna_drm-y += $(SYNA_DRM_TYPE)/panel/panel_lcdc.o

syna_drm-y += $(SYNA_DRM_TYPE)/dsi/dsih_api.o
syna_drm-y += $(SYNA_DRM_TYPE)/dsi/dsih_core.o
syna_drm-y += $(SYNA_DRM_TYPE)/dsi/dsih_dphy.o
syna_drm-y += $(SYNA_DRM_TYPE)/dsi/dsih_hal.o
syna_drm-y += $(SYNA_DRM_TYPE)/dsi/dphy_wrap.o
syna_drm-y += $(SYNA_DRM_TYPE)/dsi/dsih_displays.o

else ifeq ($(SYNA_DRM_TYPE), vsxxx)
syna_drm-y += $(SYNA_DRM_TYPE)/drm_syna_hdmi.o
syna_drm-y += $(SYNA_DRM_TYPE)/drm_syna_dsi.o
syna_drm-y += $(SYNA_DRM_TYPE)/drm_syna_tmds.o
syna_drm-y += $(SYNA_DRM_TYPE)/syna_drm_priv.o
syna_drm-y += $(SYNA_DRM_TYPE)/drm_syna_vpp_debug.o
syna_drm-y += $(SYNA_DRM_TYPE)/syna_hdmi_config.o
endif

syna_drm-y += port/drm_syna_common.o
syna_drm-y += panel/panel_dsi.o
syna_drm-y += bridge/syna_bridge.o
syna_drm-$(CONFIG_SYNA_DRM_BRIDGE_LT9611) += bridge/lontium-lt9611.o
ccflags-$(CONFIG_SYNA_DRM_BRIDGE_LT9611) += -DLT9611_OPTIMIZE_INIT_TIME
ifeq ($(KV_GT_5_10_0), true)
syna_drm-y += port/dmabuf/drm_syna_port.o
else
syna_drm-y += port/ion/drm_syna_port.o
endif
obj-$(CONFIG_SYNA_WSPANEL_1080P) +=panel/panel-waveshare-dsi.o
obj-$(CONFIG_SYNA_WSPANEL_10P1) +=panel/panel-waveshare-dsi-v2.o
