// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 */

#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/debugfs.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_edid.h>
#include "drm_syna_drv.h"
#include "syna_drm_priv.h"
#include "drm_syna_hdmi.h"
#include "vpp_cmd.h"
#include "vpp_api.h"
#include "syna_hdmi_config.h"
#include "avio_common.h"
#include "avio_core.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
struct drm_edid {
        /* Size allocated for edid */
        size_t size;
        const struct edid *edid;
};
#endif

#define MAX_EDID_BLOCKS 8 //Max EDID blocks supported by syna driver
#define VPP_HDMI_SINKCAP_BITMASK_FALLBACK 0xFFFFFFFF //If no resolution is supported, default fallback will be 480p
#define VPP_CHECK_BITMASK(VAL, MASK) (((VAL) & (MASK)) == (MASK))
#define VPP_IS_SINK_SUPPORT_MODE(I, N, SINK_CAPS) \
        ((I == (N - 1)) || VPP_CHECK_BITMASK(SINK_CAPS, supported_forced_mode[I].sinkcap_bitmask))

static char preferred_mode_name[DRM_DISPLAY_MODE_LEN] = "1920x1080";
static char forced_mode[DRM_DISPLAY_MODE_LEN] = "\0";
//TODO: add 4K60/50 after interop tests
static const struct syna_hdmi_mode_map supported_forced_mode[] = {
	{ "4K30", RES_4Kx2K30,   (1 << VPP_HDMI_SINKCAP_BITMASK_4K30) },
	{ "2K60", RES_1080P60,   ((1 << VPP_HDMI_SINKCAP_BITMASK_FHD) | (1 << VPP_HDMI_SINKCAP_BITMASK_60FPS)) },
	{ "2K50", RES_1080P50,   ((1 << VPP_HDMI_SINKCAP_BITMASK_FHD) | (1 << VPP_HDMI_SINKCAP_BITMASK_50FPS)) },
	{ "720p", RES_720P60,    ((1 << VPP_HDMI_SINKCAP_BITMASK_720P)| (1 << VPP_HDMI_SINKCAP_BITMASK_60FPS)) },
	{ "576p", RES_625P50,    (1 << VPP_HDMI_SINKCAP_BITMASK_576P) },
	{ "480p", RES_525P60,    VPP_HDMI_SINKCAP_BITMASK_FALLBACK },
};

/* hdmi_preferred_mode specifies the preferred UI mode for output Compositors/Display
   managers generally pick preferred mode */
module_param_string(hdmi_preferred_mode,
			preferred_mode_name, DRM_DISPLAY_MODE_LEN, 0444);

MODULE_PARM_DESC(hdmi_preferred_mode,
		 "Specify the preferred mode (if supported), e.g. 1920x1080");

/* hdmi_force_mode specifies the forced output mode for HDMI output
   user can force a specific resolution of their choice, if the connected
   sink supports it then this resolution will be forced all the time
   if unused, driver will auto select the resolution */
module_param_string(hdmi_force_mode,
			forced_mode, DRM_DISPLAY_MODE_LEN, 0644);
MODULE_PARM_DESC(hdmi_force_mode,
		"Force a specific resolution in fixed mode.\
		 Current support:4K60, 4K50, 4K30, 2K60, 2K50, 720p, 576p, 480p");

static int force_persistent_res;
module_param(force_persistent_res, int, 0444);
MODULE_PARM_DESC(force_persistent_res,
		"force persistent flag: 1 if u-boot applied persistent res_id, 0 otherwise");

MODULE_LICENSE("Dual MIT/GPL");

static int hpd_handle_state_get(void *data, u64 *val)
{
	struct drm_connector *connector = data;
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);

	*val = syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled;
	return 0;
}

static int hpd_handle_state_set(void *data, u64 val)
{
	struct drm_connector *connector = data;
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);

	if (val) {
		syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled = true;
	} else {
		syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled = false;
	}

	DRM_DEBUG_DRIVER("%s notification of HPD via drm core\n", val?"enabling":"disabling");
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(hpd_handle_fops, hpd_handle_state_get, hpd_handle_state_set, "%llu\n");

static int res_handle_state_get(void *data, u64 *val)
{
	struct drm_connector *connector = data;
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);

	*val = syna_hdmi->drm_priv->modeset_enabled;
	return 0;
}

static int res_handle_state_set(void *data, u64 val)
{
	struct drm_connector *connector = data;
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);
	if (val) {
		DRM_DEBUG_DRIVER("enabling resolution change support!\n");
		syna_hdmi->drm_priv->modeset_enabled = true;
		syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.fixedModeSet = false;
	} else {
		DRM_DEBUG_DRIVER("use fixed output mode!\n");
		syna_hdmi->drm_priv->modeset_enabled = false;
		syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.fixedModeSet = true;
	}
	/* takes effect on next HPD*/
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(res_handle_fops, res_handle_state_get, res_handle_state_set, "%llu\n");

static int syna_hdmi_get_edid (void *data, u8 *buf, unsigned int block, size_t len)
{
	VPP_HDMI_RAW_EDID *edid_buffer;
	int retVal = 0;

	edid_buffer = kzalloc(sizeof(VPP_HDMI_RAW_EDID), GFP_KERNEL);
	if (!edid_buffer) {
		DRM_ERROR("Memory could not be allocated for EDID\n");
		return -ENOMEM;
	}

	retVal = wrap_MV_VPPOBJ_GetHDMIRawEdid(edid_buffer);
	if(retVal) {
		DRM_ERROR("Fail to get EDID from driver:%d\n",retVal);
		goto get_edid_exit;
	}

	if (!edid_buffer->IsValid) {
		DRM_ERROR("Invalid EDID\n");
		retVal = -EINVAL;
		goto get_edid_exit;
	}

	if (block < MAX_EDID_BLOCKS) {
		memcpy(buf, edid_buffer->DataBuf + (block * 128), len);
	} else {
		DRM_ERROR("Could not access requested block\n");
		retVal = -EINVAL;
		goto get_edid_exit;
	}

get_edid_exit:
	kfree(edid_buffer);
	return retVal;
}

static int syna_hdmi_connector_helper_get_modes(struct drm_connector *connector)
{
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);
	int len = strlen(preferred_mode_name);
	uint32_t num_modes;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
	const struct drm_edid *hdmi_edid = drm_edid_read_custom(connector,
						syna_hdmi_get_edid, NULL);
#else
	struct edid *hdmi_edid = drm_do_get_edid(connector,
						syna_hdmi_get_edid, NULL);
#endif

	if (len)
		DRM_DEBUG_DRIVER("detected hdmi_preferred_mode=%s\n",
				 preferred_mode_name);
	else
		DRM_DEBUG_DRIVER("no hdmi_preferred_mode\n");

	if (hdmi_edid == NULL) {
		drm_connector_update_edid_property(connector, NULL);
		if (!syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled) {
			/* Keep legacy behavior: use default mode list when HPD handling is disabled. */
			num_modes = drm_add_modes_noedid(connector, 1920, 1080);
			DRM_ERROR("Failed to read EDID, fallback to default mode list\n");
			if (!len) {
				strncpy(preferred_mode_name, "1920x1080", DRM_DISPLAY_MODE_LEN);
				len = strlen(preferred_mode_name);
			}
		} else {
			/* With HPD handling enabled, avoid dummy modes on transient sink failures. */
			DRM_ERROR("Failed to read EDID with HPD handling enabled\n");
			num_modes = 0;
		}

#if IS_ENABLED(CONFIG_CEC_CORE)
		cec_notifier_phys_addr_invalidate(syna_hdmi->cec);
#endif
	} else {
		DRM_DEBUG_DRIVER("Edid obtained and parsed \n");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
		drm_connector_update_edid_property(connector, hdmi_edid->edid);
		num_modes = drm_add_edid_modes(connector, (struct edid*)hdmi_edid->edid);
#if IS_ENABLED(CONFIG_CEC_CORE)
		cec_notifier_set_phys_addr_from_edid(syna_hdmi->cec, hdmi_edid->edid);
#endif
		drm_edid_free(hdmi_edid);
#else
		drm_connector_update_edid_property(connector, hdmi_edid);
		num_modes = drm_add_edid_modes(connector, hdmi_edid);
#if IS_ENABLED(CONFIG_CEC_CORE)
		cec_notifier_set_phys_addr_from_edid(syna_hdmi->cec, hdmi_edid);
#endif
		kfree(hdmi_edid);
#endif
	}


	if (num_modes && len) {
		struct drm_display_mode *pref_mode_user = NULL;
		struct drm_display_mode *pref_mode_edid = NULL;

		struct drm_display_mode *mode, *t;
		list_for_each_entry_safe(mode, t, &connector->probed_modes, head) {
			if (mode->type & DRM_MODE_TYPE_PREFERRED) {
				DRM_DEBUG_DRIVER("preferred mode from EDID:%s\n", mode->name);
				pref_mode_edid = mode;
				mode->type &= ~DRM_MODE_TYPE_PREFERRED;
			}

			if ((pref_mode_user == NULL) && !strcmp(mode->name, preferred_mode_name)) {
				if (!(mode->flags & DRM_MODE_FLAG_INTERLACE)) {
					pref_mode_user = mode;
				}
			}
		}

		if (pref_mode_user)
			pref_mode_user->type |= DRM_MODE_TYPE_PREFERRED;
		else if (pref_mode_edid)
			pref_mode_edid->type |= DRM_MODE_TYPE_PREFERRED;
	}

	drm_mode_sort(&connector->probed_modes);
	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s] found %d modes\n",
					connector->base.id, connector->name, num_modes);

	return num_modes;
}

static int syna_check_mode_enabled(int res_id, struct syna_conn_hdmi *syna_hdmi)
{
	int i;
	for (i = 1; i < syna_hdmi->count_format_supported; i++)
		if (res_id == syna_hdmi->supported_formats[i])
			return 0;

	return -1;
}

static enum drm_mode_status syna_hdmi_connector_helper_mode_valid(struct drm_connector *connector,
						struct drm_display_mode *mode)
{
	int res_id = MV_VPP_GetResIndex(mode->hdisplay, mode->vdisplay,
			mode->flags & DRM_MODE_FLAG_INTERLACE, mode->clock,
			drm_mode_vrefresh(mode));
	if (res_id < 0)
		return MODE_NOMODE;

	/*Interlaced and Doublescan mode currently not supported by VPP*/
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;
	else if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (syna_check_mode_enabled(res_id, to_syna_conn_hdmi(connector)))
		return MODE_NOMODE;

	return MODE_OK;
}

static void syna_hdmi_connector_destroy(struct drm_connector *connector)
{
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);
	struct syna_drm_private *dev_priv = connector->dev->dev_private;
	ENUM_VOUT_CONNECTOR vout_id =
		connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ?
			  VOUT_CONNECTOR_HDMI : VOUT_CONNECTOR_DSI;

	if (!dev_priv) {
		DRM_ERROR("%s %d  device private is NULL!!\n",
			  __func__, __LINE__);
		return;
	}
	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s]\n",
			 connector->base.id, connector->name);

#if IS_ENABLED(CONFIG_CEC_CORE)
	if (syna_hdmi->cec)
		cec_notifier_conn_unregister(syna_hdmi->cec);
#endif

	if (syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled)
		kthread_stop(syna_hdmi->hpd_monitor_task);

	kfree(syna_hdmi->supported_formats);

	drm_connector_cleanup(connector);

	kfree(syna_hdmi);
	dev_priv->connector[vout_id] = NULL;
}

static void syna_hdmi_connector_force(struct drm_connector *connector)
{
}

static enum drm_connector_status syna_hdmi_hotplug_detect(
                        struct drm_connector *connector,
                        bool force)
{
	unsigned char device_connected = false;
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);

	(void)force;
	/* read the HPD status from vpp hal, which is updated
	   based on HPD IRQ from dhub */

	if (syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled)
		wrap_MV_VPPOBJ_GetHPDStatus(&device_connected, false);
	else
		device_connected = true; /*assume sink is connected always*/

	return (device_connected ? connector_status_connected :
					connector_status_disconnected);
}

static int syna_configure_def_res(void)
{
	VPP_HDMI_SINK_CAPS sinkCaps;
	int retVal;
	VPP_DISP_OUT_PARAMS dispParams;
	int len = strlen(forced_mode);
	int applyFlag = 1;
	int bl_resId;
	avio_fastlogo_info display_info = avio_get_fastlogo_status();
	int i = 0;
	int best_res_id_ndx = -1;
	int forced_res_id = -1;
	const int mode_array_size = (ARRAY_SIZE(supported_forced_mode));

	retVal = MV_VPP_GetDispOutParams(CPCB_1, &dispParams);
	if (retVal == MV_VPP_OK) {
		bl_resId = dispParams.uiResId;
	} else {
		DRM_ERROR("MV_VPP_GetDispOutParams FAIL with %d\n", retVal);
		return retVal;
	}

	/*configure the default resolution set on HDMI connection*/
	retVal = wrap_MV_VPPOBJ_GetHDMISinkFeatureMap(&sinkCaps);
	if ((retVal == MV_VPP_OK)) {
		/* Priority 1: User specified module param (Force Mode) */
		if (len > 0) {
			if (forced_mode[len - 1] == '\n')
				forced_mode[--len] = '\0';

			for (; i < mode_array_size; i++) {
				bool is_supported = VPP_IS_SINK_SUPPORT_MODE(i, mode_array_size, sinkCaps);
				//lock in the first best supported mode, as look up table is highest res first.
				if (best_res_id_ndx == -1 && is_supported)
					best_res_id_ndx = i;

				if (!strcasecmp(supported_forced_mode[i].name, forced_mode)) {
					if (is_supported) {
						forced_res_id = supported_forced_mode[i].id;
						DRM_DEBUG_DRIVER("Force Mode selected : %s\n", forced_mode);
						break;
					} else {
						DRM_DEBUG_DRIVER("Forced mode %s not supported by sink (sinkCaps=0x%x, mask=0x%x)\n",
								forced_mode, sinkCaps, supported_forced_mode[i].sinkcap_bitmask);
					}
				}
			}
		}

		/* Priority 2: Auto-detect based on EDID Sink Caps */
		if (forced_res_id == -1) {
			/* Try to find the best res-id, if not yet found */
			for (; (best_res_id_ndx == -1) && (i < mode_array_size); i++) {
				if (VPP_IS_SINK_SUPPORT_MODE(i, mode_array_size, sinkCaps)) {
					best_res_id_ndx = i;
					break;
				}
			}
			DRM_DEBUG_DRIVER("Unknown Force Mode: %s, Best sink mode selected :%s\n", forced_mode, supported_forced_mode[best_res_id_ndx].name);
		}

		//select forced res if set and suppored, else set best supported resolution
		dispParams.uiResId = forced_res_id != -1 ? forced_res_id : supported_forced_mode[best_res_id_ndx].id;
		DRM_DEBUG_DRIVER("HDMI_HPD detected, force mode:%d setting res to resId:%d colorfmt:%d bidepth:%d sinkcaps:%d\n",
				forced_res_id, dispParams.uiResId,dispParams.uiColorFmt,dispParams.uiBitDepth,
				(retVal)?-1:sinkCaps);

		if (display_info.u.status && bl_resId == dispParams.uiResId)
			applyFlag = 0;

		//Set the display resolution
		retVal = MV_VPP_SetDisplayResolution(CPCB_1, dispParams, applyFlag);
		if (retVal != MV_VPP_OK)
			DRM_DEBUG_DRIVER("%s:%d: MV_VPP_SetDisplayResolution FAILED, error: 0x%x\n",
					__func__, __LINE__, retVal);
			/* don't handle error */
	} else {
		DRM_ERROR("wrap_MV_VPPOBJ_GetHDMISinkFeatureMap FAIL with %d\n",retVal);
	}

	return retVal;
}

static int syna_hdmi_hpd_monitor(void *param)
{
	struct drm_connector *connector = (struct drm_connector *)param;
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);
	struct drm_device *dev = connector->dev;
	unsigned char activeHpdStatus;
	unsigned char hpdStatus = false;
	int retVal;

	connector->status = syna_hdmi_hotplug_detect(connector, false);
	activeHpdStatus = (connector->status == connector_status_connected) ? true : false;
	DRM_DEBUG_DRIVER("startup HDMI connection state : %d\n", activeHpdStatus);
	if (activeHpdStatus && !force_persistent_res)
		syna_configure_def_res();

	while (!kthread_should_stop()) {
		retVal = wrap_MV_VPP_WaitHdmiConnChange(&hpdStatus);
		if (retVal != 0)
			continue;

		if ((hpdStatus != activeHpdStatus) &&
			syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled) {
			DRM_INFO("HDMI connection state changed to : %d\n", hpdStatus);
			activeHpdStatus = hpdStatus;
			connector->status = hpdStatus ? connector_status_connected :
							connector_status_disconnected;

#if IS_ENABLED(CONFIG_CEC_CORE)
			if (!hpdStatus) {
				DRM_DEBUG_DRIVER("set phy addr CEC invalid\n");
				cec_notifier_phys_addr_invalidate(syna_hdmi->cec);
			}
#endif
			if (syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.fixedModeSet
				       && (hpdStatus==connector_status_connected) && !force_persistent_res)
				syna_configure_def_res();
			drm_kms_helper_hotplug_event(dev);
		}
	}
	// Allow resolution change after bootup
	force_persistent_res = 0;

	return 0;
}

static struct drm_connector_helper_funcs syna_hdmi_connector_helper_funcs = {
	.get_modes = syna_hdmi_connector_helper_get_modes,
	.mode_valid = syna_hdmi_connector_helper_mode_valid,
};

static const struct drm_connector_funcs syna_hdmi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = syna_hdmi_connector_destroy,
	.force = syna_hdmi_connector_force,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.dpms = drm_helper_connector_dpms,
	.detect = syna_hdmi_hotplug_detect,
};

struct drm_connector *syna_hdmi_connector_create(struct drm_device *dev)
{
	struct syna_conn_hdmi *syna_hdmi;
	struct drm_connector *connector;
#if IS_ENABLED(CONFIG_CEC_CORE)
	struct cec_connector_info conn_info;
#endif
	int retVal;

	syna_hdmi = kzalloc(sizeof(*syna_hdmi), GFP_KERNEL);
	if (!syna_hdmi)
		return ERR_PTR(-ENOMEM);

	retVal = syna_hdmi_tx_read_config(syna_hdmi);
	if (retVal) {
		kfree(syna_hdmi);
		return ERR_PTR(retVal);
	}

	syna_hdmi->drm_priv = dev->dev_private;
	syna_hdmi->drm_priv->modeset_enabled =
		syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.fixedModeSet ? false : true;

	connector = &syna_hdmi->base;
	drm_connector_init(dev, connector, &syna_hdmi_connector_funcs,
				DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &syna_hdmi_connector_helper_funcs);

	connector->dpms = DRM_MODE_DPMS_OFF;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;

	if (syna_hdmi->syna_hdmi_conf.hdmiTxConfigFields.hpdHandlingEnabled) {
		syna_hdmi->hpd_monitor_task = kthread_run(syna_hdmi_hpd_monitor, connector, "HDMI HPD monitor thread");
		if (!IS_ERR(syna_hdmi->hpd_monitor_task)) {
			connector->polled = DRM_CONNECTOR_POLL_HPD;
			DRM_DEBUG_DRIVER("syna_drm:enable HDMI HPD polling \n");
		}
	} else {
		DRM_DEBUG_DRIVER("hpd handling not enabled. assume connected always\n");
		connector->status = connector_status_connected;
	}


#if IS_ENABLED(CONFIG_CEC_CORE)
	cec_fill_conn_info_from_drm(&conn_info, connector);
	syna_hdmi->cec = cec_notifier_conn_register(dev->dev, NULL,
					 &conn_info);
	if (!syna_hdmi->cec) {
		pr_err("Couldn't allocate CEC notifier\n");
		return ERR_PTR(-ENOMEM);
	}
#else
	pr_info("CEC support not enabled in kernel config\n");
#endif

	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s]\n", connector->base.id,
			 connector->name);

	return connector;
}

void syna_hdmi_add_debugfs_entry(struct syna_drm_private *dev_priv)
{
	struct drm_connector *connector = dev_priv->connector[VOUT_CONNECTOR_HDMI];
	struct syna_conn_hdmi *syna_hdmi = connector ? to_syna_conn_hdmi(connector) : NULL;
	struct dentry *root = connector ? connector->debugfs_entry : NULL;

	if (!syna_hdmi || !root)
		return;

	/* control HPD handling */
	syna_hdmi->debugfs_hpd_node = debugfs_create_file("enable_hpd_handle", S_IRUGO | S_IWUSR, root, connector,
			&hpd_handle_fops);

	/* control fixedModeset */
	syna_hdmi->debugfs_res_node = debugfs_create_file("enable_res_config", S_IRUGO | S_IWUSR, root, connector,
			&res_handle_fops);
}

void syna_hdmi_remove_debugfs_entry(struct syna_drm_private *dev_priv)
{
	struct drm_connector *connector = dev_priv->connector[VOUT_CONNECTOR_HDMI];
	struct syna_conn_hdmi *syna_hdmi = to_syna_conn_hdmi(connector);

	debugfs_remove(syna_hdmi->debugfs_hpd_node);
	debugfs_remove(syna_hdmi->debugfs_res_node);
}
