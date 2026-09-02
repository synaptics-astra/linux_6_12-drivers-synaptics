// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Synaptics Incorporated
 */

#define pr_fmt(fmt) "[berlin_m2m]" fmt

#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/tee_drv.h>
#include <linux/dma-heap.h>
#include "uapi/m2m.h"
#include <uapi/bm.h>
#include "m2m_wrapper.h"

#include <linux/uio.h>

#define TSP_FIGO_REG_SIZE	    0x20000
#define TSP_FIGO_NR            2

typedef uint32_t TEEC_Session;

static const uuid_t m2m_ta_uuid = UUID_INIT(0x1316a183, 0x894d, 0x43fe,
 						0x98, 0x93, 0xbb, 0x94, 0x6a, 0xe1, 0x03, 0xe9);
static struct tee_context* g_m2m_ctx;
static bool ctx_created;

enum {
	M2M_INVALID,
	M2M_CryptoInit,
	M2M_Setscheme,
	M2M_Setkey,
	M2M_Update,
	M2M_Update_Residue,
	M2M_FeedCmd,
	M2M_Postprocess,
	M2M_CryptoDeinit,
	M2M_SetPatternMode,
	M2M_SetResidueMode,
	M2M_SetIVPolicy,
	M2M_CMD_MAX
};

struct syna_dmaheap_buf {
	struct dma_buf *dmabuf;
	uint32_t memid;
};

struct m2m_shadow_buff {
	struct device *dev;
	struct dma_heap * cma_cust_dma_heap;
	struct syna_dmaheap_buf shadow_dmabuf_heap_buf[TSP_FIGO_NR];
};

static struct m2m_shadow_buff shadow_dma_buff = { 0 };

static int tz_m2m_open_session(TEEC_Session *teec_sess,
				unsigned int crypto_mode, bool dummy_sess)
{
	int ret = 0;
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_param param[4];

	if (!ctx_created) {
		ret =  -ENODEV;
		pr_err("optee context not created\n");
		goto end;
	}

	if (!dummy_sess && crypto_mode != M2M_CRYPTO_MODE_ENC &&
			crypto_mode != M2M_CRYPTO_MODE_DEC) {
		pr_err("invalid crypto mode: 0x%x\n", crypto_mode);
		ret =  -EINVAL;
		goto end;
	}

	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &m2m_ta_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 2;

	memset(param, 0, sizeof(param));
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = crypto_mode;
	param[0].u.value.b = dummy_sess ? 1 : 0;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;
	ret = tee_client_open_session(g_m2m_ctx, &sess_arg, param);
	if (ret < 0 || sess_arg.ret != 0) {
		pr_err("tee_client_open_session failed with code 0x%x origin 0x%x",
			ret, sess_arg.ret);
		ret = -EINVAL;
		goto end;
	}
	*teec_sess = sess_arg.session;
end:
	return ret;
}

static int tz_m2m_crypto_init(TEEC_Session *teec_sess)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};
	u32 i;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;

	arg.func = M2M_CryptoInit;
	arg.num_params = 1;
	arg.session = *teec_sess;

	for (i = 0; i < TSP_FIGO_NR; i++) {
		param[0].u.value.a = i;
		param[0].u.value.b = shadow_dma_buff.shadow_dmabuf_heap_buf[i].memid;
		ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
		if (ret < 0 || arg.ret != 0) {
			pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n", arg.func,
					ret, arg.ret);
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

int m2m_wrapper_alloc(unsigned int len, void **handle)
{
	struct tee_shm *shm;

	shm = tee_shm_alloc_kernel_buf(g_m2m_ctx, len);
	if (IS_ERR(shm)) {
		pr_err("fail to allocate TEEC share memory: %ld\n", PTR_ERR(shm));
		return -ENOMEM;
	}
	*handle = shm;

	return 0;
}

void m2m_wrapper_free(void *handle)
{
	tee_shm_free(handle);
}


static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return (ver->impl_id == TEE_IMPL_ID_OPTEE);
}

int m2m_wrapper_init(struct device *dev)
{
	int ret;
	TEEC_Session dummy_sess;
	struct bm_fb_param fb_param;
	struct bm_pt_param pt_param;
	int i = 0;

	if (!shadow_dma_buff.cma_cust_dma_heap) {
		shadow_dma_buff.cma_cust_dma_heap = dma_heap_find("CMA-CUST-reserved");
		if (!shadow_dma_buff.cma_cust_dma_heap) {
			pr_err("CMA-CUST-reserved not found\n");
			return -ENODEV;
		}
	}

	for (; i < TSP_FIGO_NR; i++) {
		memset(&fb_param, 0, sizeof(fb_param));
		memset(&pt_param, 0, sizeof(pt_param));
		if (!shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf) {
			shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf = dma_heap_buffer_alloc(shadow_dma_buff.cma_cust_dma_heap, TSP_FIGO_REG_SIZE, 0, 0);
			if (IS_ERR(shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf)) {
				pr_err("failed to allocate dma heap buffer: %ld\n", PTR_ERR(shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf));
				return -ENOMEM;
			}
		}

		if (shadow_dma_buff.shadow_dmabuf_heap_buf[i].memid == 0) {
			ret = bm_create_pt(shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf, 0, &fb_param, &pt_param);
			if (ret < 0) {
				pr_err("failed to create pt for dma heap buffer: %d\n", ret);
				dma_buf_put(shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf);
				shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf = NULL;
				return -ENOMEM;
			}
		}

		shadow_dma_buff.shadow_dmabuf_heap_buf[i].memid = pt_param.mem_id;
	}

	shadow_dma_buff.dev = dev;

	g_m2m_ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(g_m2m_ctx)) {
		pr_err("fail to initialize optee context\n");
		ctx_created = false;
		return -ENODEV;
	}
	ctx_created = true;

	ret = tz_m2m_open_session(&dummy_sess, M2M_CRYPTO_MODE_INVALID, true);
	if (ret) {
		pr_err("fail to open a dummy sess\n");
		goto error;
	}

	ret = tz_m2m_crypto_init(&dummy_sess);
	if (ret) {
		pr_err("tz_m2m_crypto_init failed\n");
		tee_client_close_session(g_m2m_ctx, dummy_sess);
		goto error;
	}
	ret = tee_client_close_session(g_m2m_ctx, dummy_sess);
	return ret;

error:
	for (i = 0; i < TSP_FIGO_NR; i++) {
		if (shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf) {
			dma_buf_put(shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf);
			shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf = NULL;
		}
	}
	return ret;
}

void m2m_wrapper_exit(void)
{
	int i = 0;

	for (; i < TSP_FIGO_NR; i++) {
		if (shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf) {
			dma_buf_put(shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf);
			shadow_dma_buff.shadow_dmabuf_heap_buf[i].dmabuf = NULL;
		}
	}

	if (ctx_created) {
		tee_client_close_context(g_m2m_ctx);
		ctx_created = false;
	}
}

int m2m_wrapper_open_session(TEEC_Session *teec_sess, unsigned int crypto_mode)
{
	return tz_m2m_open_session(teec_sess, crypto_mode, false);
}

void m2m_wrapper_close_session(TEEC_Session *teec_sess)
{
	int ret = 0;
	ret = tee_client_close_session(g_m2m_ctx, *teec_sess);
	if (ret < 0) {
		pr_err("fail to close TEEC session: 0x%x\n", ret);
	}
	return;
}

int m2m_wrapper_set_scheme(TEEC_Session *teec_sess, unsigned int scheme)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};

	if ((scheme < M2M_CRYPTO_TYPE_AES_128_ECB) ||
		 (scheme > M2M_CRYPTO_TYPE_AES_128_CTR128)) {
		pr_err("invalid scheme: 0x%x\n", scheme);
		ret =  -EINVAL;
		goto end;
	}

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = scheme;

	arg.func = M2M_Setscheme;
	arg.num_params = 1;
	arg.session = *teec_sess;
	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}
end:
	return ret;
}

int m2m_wrapper_set_patternMode(TEEC_Session *teec_sess, struct m2m_pattern_mode *pattern)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = pattern->pattern_enc;
	param[0].u.value.b = pattern->pattern_clr;

	arg.func = M2M_SetPatternMode;
	arg.num_params = 1;
	arg.session = *teec_sess;
	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
	}

	return ret;
}

int m2m_wrapper_set_residueMode(TEEC_Session *teec_sess, unsigned int residueMode)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};

	if (residueMode > M2M_RESIDUE_MODE_CTS) {
		pr_err("invalid residueMode: 0x%x\n", residueMode);
		ret =  -EINVAL;
		goto end;
	}

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = residueMode;

	arg.func = M2M_SetResidueMode;
	arg.num_params = 1;
	arg.session = *teec_sess;
	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}
end:
	return ret;
}

int m2m_wrapper_set_IVPolicy(TEEC_Session *teec_sess, unsigned int iv_policy)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};

	if (iv_policy >= M2M_IV_POLICY_MAX) {
		pr_err("invalid ivPolicy: 0x%x\n", iv_policy);
		ret =  -EINVAL;
		goto end;
	}

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = iv_policy;

	arg.func = M2M_SetIVPolicy;
	arg.num_params = 1;
	arg.session = *teec_sess;
	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}
end:
	return ret;
}

int m2m_wrapper_config(TEEC_Session *teec_sess, struct m2m_key *key_config, struct m2m_key_iv_info *ses_key_iv_info)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};
	struct m2m_key_info *pKeyInfo;
	struct drm_crypto_info *pCryptoInfo;
	int KeyDataParam = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	int IVDataParam = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	/* Use TEE Client API to register the underlying memory buffer. */
	if (key_config->key_option == M2M_KEY_CONTENT) {
		KeyDataParam = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
		if (unlikely(key_config->key_length <= ses_key_iv_info->pKeySM->size)) {
			memcpy(ses_key_iv_info->pKeySM->kaddr, key_config->key_data, key_config->key_length);
		} else {
			pr_err("Key size is too long\n");
			ret =  -EINVAL;
			goto end;
		}
	}

	if (key_config->iv_option == M2M_KEY_CONTENT) {
		IVDataParam = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
		if (likely(key_config->iv_length <= ses_key_iv_info->pIVSM->size)) {
			memcpy(ses_key_iv_info->pIVSM->kaddr, key_config->iv_data, key_config->iv_length);
		} else {
			pr_err("IV size is too long\n");
			ret =  -EINVAL;
			goto end;
		}
	}

	pCryptoInfo = (struct drm_crypto_info *)ses_key_iv_info->pCryptoSM->kaddr;
	pCryptoInfo->uiSchemeType = key_config->drm_info.scheme_type;
	pCryptoInfo->uiSessionID = key_config->drm_info.session_id;
	pCryptoInfo->uiKeyIdLen = key_config->drm_info.key_id_length;
	memcpy(pCryptoInfo->uiKeyID, key_config->drm_info.key_id, M2M_MAX_KEY_ID_LENGTH);

	pKeyInfo = (struct m2m_key_info *)ses_key_iv_info->pKeyInfoSM->kaddr;
	pKeyInfo->uiKeyOption = key_config->key_option;
	pKeyInfo->uiKeyLength = key_config->key_length;
	pKeyInfo->uiKeyTblIndex = key_config->key_index;
	pKeyInfo->uiIVOption = key_config->iv_option;
	pKeyInfo->uiIVLength = key_config->iv_length;
	pKeyInfo->uiIVTblIndex = key_config->iv_index;

	param[0].attr = KeyDataParam;
	param[0].u.memref.shm = ses_key_iv_info->pKeySM;
	param[0].u.memref.size = ses_key_iv_info->pKeySM->size;
	param[0].u.memref.shm_offs = 0;

	param[1].attr = IVDataParam;
	param[1].u.memref.shm = ses_key_iv_info->pIVSM;
	param[1].u.memref.size = ses_key_iv_info->pIVSM->size;
	param[1].u.memref.shm_offs = 0;

	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[2].u.memref.shm = ses_key_iv_info->pCryptoSM;
	param[2].u.memref.size = ses_key_iv_info->pCryptoSM->size;
	param[2].u.memref.shm_offs = 0;

	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[3].u.memref.shm = ses_key_iv_info->pKeyInfoSM;
	param[3].u.memref.size = ses_key_iv_info->pKeyInfoSM->size;
	param[3].u.memref.shm_offs = 0;

	arg.func = M2M_Setkey;
	arg.num_params = 4;
	arg.session = *teec_sess;

	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}
end:
	return ret;
}


int m2m_wrapper_update(TEEC_Session *teec_sess,
				unsigned int inputMemid, size_t inputSize, size_t inputOffset,
				unsigned int outputMemid, size_t outputSize, size_t outputOffset)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = inputMemid;
	param[0].u.value.b = inputSize;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[1].u.value.a = outputMemid;
	param[1].u.value.b = outputSize;

	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[2].u.value.a = inputOffset;
	param[2].u.value.b = outputOffset;

	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[3].u.value.a = 1; //TSP_MEM_PARAM_SHM_ID

	arg.func = M2M_Update;
	arg.num_params = 4;
	arg.session = *teec_sess;

	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}

	arg.func = M2M_FeedCmd;
	arg.num_params = 0;
	arg.session = *teec_sess;

	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}
end:
	return ret;
}

int m2m_wrapper_routine(TEEC_Session *teec_sess, unsigned int *cmdFinishNum)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4] = {0};

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;
	arg.func = M2M_Postprocess;
	arg.num_params = 1;
	arg.session = *teec_sess;

	ret = tee_client_invoke_func(g_m2m_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("fail to invoke command[%d] to M2M TA: ret = %x, TEE err: 0x%x\n",
			arg.func, ret, arg.ret);
		ret = -EINVAL;
		goto end;
	}
	*cmdFinishNum = param[0].u.value.a;
end:
	return ret;
}