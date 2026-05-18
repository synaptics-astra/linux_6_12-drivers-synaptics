// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 Synaptics Incorporated
 *
 */

#include "tsp.h"
#include <linux/tee_drv.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

static const uuid_t tsp_ta_uuid = UUID_INIT(0x1316a183, 0x894d, 0x43fe,
        0x98, 0x93, 0xbb, 0x94, 0x6a, 0xe1, 0x03, 0xe8);
static struct tee_context *context;
static u32 session[TSP_FIGO_NUM];

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return (ver->impl_id == TEE_IMPL_ID_OPTEE);
}

static int tz_tsp_check_figo_id(uint32_t id)
{
	if (id > (TSP_FIGO_NUM - 1)) {
		pr_err("Invalid figo id:0x%x\n", id);
		return -EINVAL;
	}
	return 0;
}

int tz_tsp_load_firmware(struct device *dev, const struct tsp_firmware_info fw_info,
							int figo_id, int fw_idx, bool force_load)
{
	dma_addr_t fw_dma_addr = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];
	int ret;

	ret = tz_tsp_check_figo_id(figo_id);
	if (ret < 0)
		return ret;

	if (fw_info.data) {
		fw_dma_addr = dma_map_single(dev, fw_info.data, fw_info.size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, fw_dma_addr)) {
			pr_err("can't map DMA buffer for firmware loading\n");
			return -ENOMEM;
		}
		param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
		param[1].u.value.a = fw_dma_addr;
		param[1].u.value.b = fw_info.size;
	}

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = fw_idx;
	param[0].u.value.b = force_load;

	arg.func = TSP_FW_LOAD;
	arg.num_params = fw_info.data ? 2 : 1;
	arg.session = session[figo_id];
	ret = tee_client_invoke_func(context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("%s failed with ret = %x, TEE err: %#x\n",
				__func__, ret, arg.ret);
	}

	if (fw_info.data) {
		dma_unmap_single(dev, fw_dma_addr, fw_info.size, DMA_TO_DEVICE);
	}

	return ret;
}

int tz_tsp_initialize(struct device *dev)
{
	int ret;
	uint32_t i;
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_param param[4];

	context = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(context)) {
		pr_err("fail to initialize optee context\n");
		return -ENODEV;
	}

	memset(&sess_arg, 0, sizeof(sess_arg));
	memcpy(sess_arg.uuid, tsp_ta_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 1;

	for (i = 0; i < TSP_FIGO_NUM; i++) {
		param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
		param[0].u.value.a = i;

		ret = tee_client_open_session(context, &sess_arg, param);
		if (ret < 0 || sess_arg.ret != 0) {
			pr_err("tee_client_open_session failed with code 0x%x origin 0x%x",
								ret, sess_arg.ret);
			ret = -EINVAL;
			goto error;
		}
		session[i] = sess_arg.session;
	}
	return ret;

error:
	for (i = 0; i < TSP_FIGO_NUM; i++) {
		if (session[i])
			tee_client_close_session(context, session[i]);
	}
	tee_client_close_context(context);
    return ret;
}

void tz_tsp_finalize(void)
{
	uint32_t i;

	for (i = 0; i < TSP_FIGO_NUM; i++) {
		tee_client_close_session(context, session[i]);
	}
	tee_client_close_context(context);
}

int tz_tsp_save_hw_context(uint32_t figo_id)
{
	int ret;
	struct tee_ioctl_invoke_arg arg;

	ret = tz_tsp_check_figo_id(figo_id);
	if (ret < 0)
		return ret;

	arg.func = TSP_SAVE_HW_CONTEXT;
	arg.num_params = 0;
	arg.session = session[figo_id];

	ret = tee_client_invoke_func(context, &arg, NULL);
	if (ret < 0 || arg.ret != 0) {
		pr_err("%s failed with ret = %x, TEE err: 0x%x\n",
				__func__, ret, arg.ret);
	}
	return ret;
}

int tz_tsp_restore_hw_context(uint32_t figo_id)
{
	int ret;
	struct tee_ioctl_invoke_arg arg;

	ret = tz_tsp_check_figo_id(figo_id);
	if (ret < 0)
		return ret;

	arg.func = TSP_RESTORE_HW_CONTEXT;
	arg.num_params = 0;
	arg.session = session[figo_id];

	ret = tee_client_invoke_func(context, &arg, NULL);
	if (ret < 0 || arg.ret != 0) {
		pr_err("%s failed with ret = %x, TEE err: 0x%x\n",
				__func__, ret, arg.ret);
	}
	return ret;
}

int tz_tsp_set_figo_state(uint32_t figo_id, uint32_t state)
{
	int ret;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];

	ret = tz_tsp_check_figo_id(figo_id);
	if (ret < 0)
		return ret;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = state;

	arg.func = TSP_SET_FIGO_STATE;
	arg.num_params = 1;
	arg.session = session[figo_id];

	ret = tee_client_invoke_func(context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("%s failed with ret = %x, TEE err: 0x%x\n",
				__func__, ret, arg.ret);
	}
	return ret;
}

int tz_tsp_get_figo_state(uint32_t figo_id, uint32_t *state)
{
	int ret;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];

	ret = tz_tsp_check_figo_id(figo_id);
	if (ret < 0)
		return ret;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	arg.func = TSP_GET_FIGO_STATE;
	arg.num_params = 1;
	arg.session = session[figo_id];

	ret = tee_client_invoke_func(context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("%s failed with ret = %x, TEE err: 0x%x\n",
				__func__, ret, arg.ret);
	}
	*state = param[0].u.value.a;
	return ret;
}
