// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Synaptics Incorporated
 */

#define pr_fmt(fmt) "[berlin_m2m] " fmt

#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include "tee_client_api.h"
#include "uapi/m2m.h"
#include "m2m_wrapper.h"
#include "tsp.h"

#define TA_M2M_UUID {0x1316a183, 0x894d, 0x43fe, \
					{0x98, 0x93, 0xbb, 0x94, 0x6a, 0xe1, 0x03, 0xe9} }
#define TSP_FIGO_REG_SIZE	    0x20000
#define TA_IMG_PATH_M2M      "ta/libm2m.ta"

static TEEC_UUID m2m_ta_uuid = TA_M2M_UUID;
static TEEC_Context teec_ctx;
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

struct m2m_shadow_buff {
	struct device *dev;
	dma_addr_t shadow_dma_addr;
	void *vir;
};
static struct m2m_shadow_buff shadow_dma_buff;


static int tz_m2m_load_ta(struct device *dev, const char *ta_img_path)
{
	const struct firmware *fw = NULL;
	TEEC_SharedMemory fw_shm = {};
	TEEC_Parameter tee_param = {};
	int ret;

	ret = request_firmware(&fw, ta_img_path, dev);
	if (ret) {
		pr_err("faild req fw 0x%x %s\n", ret, ta_img_path);
		return ret;
	}

	fw_shm.size = ALIGN(fw->size, PAGE_SIZE);
	fw_shm.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
	ret = TEEC_AllocateSharedMemory(&teec_ctx, &fw_shm);
	if (ret || !fw_shm.buffer) {
		pr_err("can't allocate memory(%zu) for firmware loading: 0x%x\n",
			fw_shm.size, ret);
		ret = -ENOMEM;
		goto free_fw;
	}
	memcpy(fw_shm.buffer, fw->data, fw->size);

	tee_param.memref.parent = &fw_shm;
	tee_param.memref.size = fw_shm.size;
	ret = TEEC_RegisterTA(&teec_ctx, &tee_param, TEEC_MEMREF_PARTIAL_INPUT);
	if (TEEC_ERROR_ACCESS_CONFLICT == ret) {
		pr_warn("%s TA has been loaded\n", ta_img_path);
		ret = 0;
	} else if (ret) {
		pr_err("can't register %s TA: 0x%08x\n", ta_img_path, ret);
	} else {
		pr_info("TA loaded sucessfully - %s\n", ta_img_path);
	}

	TEEC_ReleaseSharedMemory(&fw_shm);
free_fw:
	release_firmware(fw);
	return ret;
}

static int tz_m2m_errcode_translate(TEEC_Result result)
{
	int ret;

	switch (result) {
	case TEEC_SUCCESS:
		ret = 0;
		break;
	case TEEC_ERROR_ACCESS_DENIED:
	case TEEC_ERROR_ACCESS_CONFLICT:
		ret = -EACCES;
		break;
	case TEEC_ERROR_SECURITY:
		ret = -EPERM;
		break;
	case TEEC_ERROR_BAD_PARAMETERS:
	case TEEC_ERROR_ITEM_NOT_FOUND:
		ret = -EINVAL;
		break;
	case TEEC_ERROR_OUT_OF_MEMORY:
		ret = -ENOMEM;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static TEEC_Result tz_m2m_open_session(TEEC_Session *teec_sess,
				unsigned int crypto_mode, bool dummy_sess)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	if (!ctx_created) {
		ret =  TEEC_ERROR_BAD_STATE;
		goto end;
	}

	if (!dummy_sess && crypto_mode != M2M_CRYPTO_MODE_ENC &&
			crypto_mode != M2M_CRYPTO_MODE_DEC) {
		pr_err("invalid crypto mode: 0x%x\n", crypto_mode);
		ret =  TEEC_ERROR_BAD_PARAMETERS;
		goto end;
	}

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_INPUT,
			TEEC_VALUE_OUTPUT,
			TEEC_NONE,
			TEEC_NONE);

	operation.params[0].value.a = crypto_mode;
	operation.params[0].value.b = dummy_sess ? 1 : 0;
	ret = TEEC_OpenSession(
			&teec_ctx,
			teec_sess,
			&m2m_ta_uuid,
			TEEC_LOGIN_USER,
			NULL,
			&operation,
			NULL);

	if (ret != TEEC_SUCCESS)
		pr_err("fail to open TEEC session: 0x%x\n", ret);

end:
	return ret;
}

static TEEC_Result tz_m2m_crypto_init(TEEC_Session *teec_sess)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;
	u32 i;

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_INPUT,
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE);

	for (i = 0; i < 2; i++) {
		operation.params[0].value.a = i;
		operation.params[0].value.b = shadow_dma_buff.shadow_dma_addr +
			(i * TSP_FIGO_REG_SIZE);
		ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_CryptoInit,
			&operation,
			NULL);
		if (unlikely(ret != TEEC_SUCCESS)) {
			if (ret == TEEC_ERROR_NOT_IMPLEMENTED)
				pr_debug("figo[0x%x] m2m has already been initialized\n", i);
			else {
				pr_err("figo[0x%x] crypto init failed, 0x%x\n", i, ret);
				break;
			}
		}
	}

	return ret;
}

int m2m_wrapper_alloc(unsigned int len, void **handle)
{
	TEEC_SharedMemory *shm;
	TEEC_Result ret = TEEC_SUCCESS;

	shm = kzalloc(sizeof(TEEC_SharedMemory), GFP_KERNEL);
	if (unlikely(!shm))
		return -ENOMEM;

	shm->size = len;
	shm->flags = TEEC_MEM_INPUT;
	ret = TEEC_AllocateSharedMemory(&teec_ctx, shm);
	if (unlikely(ret != TEEC_SUCCESS)) {
		pr_err("fail to allocate TEEC share memory: 0x%x\n", ret);
		kfree(shm);
		return ret;
	}
	*handle = shm;
	return ret;
}

void m2m_wrapper_free(void *handle)
{
	TEEC_ReleaseSharedMemory(handle);
	kfree(handle);
}

int m2m_wrapper_init(struct device *dev)
{
	TEEC_Result ret;
	TEEC_Session dummy_sess;

	shadow_dma_buff.vir = dma_alloc_coherent(dev,
			TSP_FIGO_REG_SIZE * 2, &shadow_dma_buff.shadow_dma_addr, GFP_KERNEL);
	if (!shadow_dma_buff.vir)
		return -ENOMEM;

	pr_err("shadow_dma_buff.vir = %pk, shadow_dma_buff.shadow_dma_addr = %lld\n",
		shadow_dma_buff.vir, shadow_dma_buff.shadow_dma_addr);

	shadow_dma_buff.dev = dev;

	ret = TEEC_InitializeContext(NULL, &teec_ctx);
	if (ret != TEEC_SUCCESS) {
		pr_err("fail to initialize TEEC context: 0x%x\n", ret);
		ctx_created = false;
		goto error;
	} else {
		ctx_created = true;
	}

	ret = tz_m2m_load_ta(dev, TA_IMG_PATH_M2M);
	if (ret) {
		pr_err("fail to load m2m ta\n");
		goto error;
	}

	ret = tz_m2m_open_session(&dummy_sess, M2M_CRYPTO_MODE_INVALID, true);
	if (ret) {
		pr_err("fail to open a dummy sess\n");
		goto error;
	}

	ret = tz_m2m_crypto_init(&dummy_sess);
	if (ret) {
		pr_err("tz_m2m_crypto_init failed\n");
		TEEC_CloseSession(&dummy_sess);
		goto error;
	}

	TEEC_CloseSession(&dummy_sess);
	return tz_m2m_errcode_translate(ret);

error:
	if (shadow_dma_buff.shadow_dma_addr)
		dma_free_coherent(shadow_dma_buff.dev, TSP_FIGO_REG_SIZE * 2,
			 shadow_dma_buff.vir, shadow_dma_buff.shadow_dma_addr);

	if (ctx_created) {
		TEEC_FinalizeContext(&teec_ctx);
		ctx_created = false;
	}

	return tz_m2m_errcode_translate(ret);
}

void m2m_wrapper_exit(void)
{
	if (shadow_dma_buff.shadow_dma_addr)
		dma_free_coherent(shadow_dma_buff.dev, TSP_FIGO_REG_SIZE * 2,
			 shadow_dma_buff.vir, shadow_dma_buff.shadow_dma_addr);

	if (ctx_created) {
		TEEC_FinalizeContext(&teec_ctx);
		ctx_created = false;
	}
}

int m2m_wrapper_open_session(TEEC_Session *teec_sess, unsigned int crypto_mode)
{
	return tz_m2m_errcode_translate(tz_m2m_open_session(teec_sess, crypto_mode, false));
}

void m2m_wrapper_close_session(TEEC_Session *teec_sess)
{
	TEEC_CloseSession(teec_sess);
}

int m2m_wrapper_set_scheme(TEEC_Session *teec_sess, unsigned int scheme)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	if ((scheme < M2M_CRYPTO_TYPE_AES_128_ECB) ||
		 (scheme > M2M_CRYPTO_TYPE_AES_128_CTR128)) {
		pr_err("invalid scheme: 0x%x\n", scheme);
		ret =  TEEC_ERROR_BAD_PARAMETERS;
		goto end;
	}

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_INPUT,
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE);
	operation.params[0].value.a = scheme;

	ret = TEEC_InvokeCommand(
		teec_sess,
		M2M_Setscheme,
		&operation,
		NULL);
	if (unlikely(ret != TEEC_SUCCESS))
		pr_err("set scheme failed, 0x%x\n", ret);

end:
	return tz_m2m_errcode_translate(ret);
}

int m2m_wrapper_set_patternMode(TEEC_Session *teec_sess, struct m2m_pattern_mode *pattern)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	operation.paramTypes = TEEC_PARAM_TYPES(
		TEEC_VALUE_INPUT,
		TEEC_NONE,
		TEEC_NONE,
		TEEC_NONE);
	operation.params[0].value.a = pattern->pattern_enc;
	operation.params[0].value.b = pattern->pattern_clr;

	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_SetPatternMode,
			&operation,
			NULL);
	if (ret != TEEC_SUCCESS)
		pr_err("set patternMode failed, 0x%x\n", ret);

	return tz_m2m_errcode_translate(ret);
}

int m2m_wrapper_set_residueMode(TEEC_Session *teec_sess, unsigned int residueMode)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	if (residueMode > M2M_RESIDUE_MODE_CTS) {
		pr_err("invalid residueMode: 0x%x\n", residueMode);
		ret =  TEEC_ERROR_BAD_PARAMETERS;
		goto end;
	}

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_INPUT,
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE);
	operation.params[0].value.a = residueMode;

	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_SetResidueMode,
			&operation,
			NULL);
	if (unlikely(ret != TEEC_SUCCESS))
		pr_err("set residueMode failed, 0x%x\n", ret);

end:
	return tz_m2m_errcode_translate(ret);
}

int m2m_wrapper_set_IVPolicy(TEEC_Session *teec_sess, unsigned int iv_policy)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	if (iv_policy >= M2M_IV_POLICY_MAX) {
		pr_err("invalid ivPolicy: 0x%x\n", iv_policy);
		ret =  TEEC_ERROR_BAD_PARAMETERS;
		goto end;
	}

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_INPUT,
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE);
	operation.params[0].value.a = iv_policy;

	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_SetIVPolicy,
			&operation,
			NULL);
	if (unlikely(ret != TEEC_SUCCESS))
		pr_err("set IVPolicy failed, 0x%x\n", ret);

end:
	return tz_m2m_errcode_translate(ret);
}

int m2m_wrapper_config(TEEC_Session *teec_sess, struct m2m_key *key_config, struct m2m_key_iv_info *ses_key_iv_info)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;
	struct m2m_key_info *pKeyInfo;
	struct drm_crypto_info *pCryptoInfo;
	enum TEEC_ParamType KeyDataParam = TEEC_NONE, IVDataParam = TEEC_NONE;

	/* Use TEE Client API to register the underlying memory buffer. */
	if (key_config->key_option == M2M_KEY_CONTENT) {
		KeyDataParam = TEEC_MEMREF_PARTIAL_INPUT;
		if (unlikely(key_config->key_length <= ses_key_iv_info->pKeySM->size)) {
			memcpy(ses_key_iv_info->pKeySM->buffer, key_config->key_data, key_config->key_length);
		} else {
			pr_err("Key size is too long\n");
			ret =  TEEC_ERROR_BAD_PARAMETERS;
			goto end;
		}
	}

	if (key_config->iv_option == M2M_KEY_CONTENT) {
		IVDataParam = TEEC_MEMREF_PARTIAL_INPUT;
		if (likely(key_config->iv_length <= ses_key_iv_info->pIVSM->size)) {
			memcpy(ses_key_iv_info->pIVSM->buffer, key_config->iv_data, key_config->iv_length);
		} else {
			pr_err("IV size is too long\n");
			ret =  TEEC_ERROR_BAD_PARAMETERS;
			goto end;
		}
	}

	pCryptoInfo = (struct drm_crypto_info *)ses_key_iv_info->pCryptoSM->buffer;
	pCryptoInfo->uiSchemeType = key_config->drm_info.scheme_type;
	pCryptoInfo->uiSessionID = key_config->drm_info.session_id;
	pCryptoInfo->uiKeyIdLen = key_config->drm_info.key_id_length;
	memcpy(pCryptoInfo->uiKeyID, key_config->drm_info.key_id, M2M_MAX_KEY_ID_LENGTH);

	pKeyInfo = (struct m2m_key_info *)ses_key_iv_info->pKeyInfoSM->buffer;
	pKeyInfo->uiKeyOption = key_config->key_option;
	pKeyInfo->uiKeyLength = key_config->key_length;
	pKeyInfo->uiKeyTblIndex = key_config->key_index;
	pKeyInfo->uiIVOption = key_config->iv_option;
	pKeyInfo->uiIVLength = key_config->iv_length;
	pKeyInfo->uiIVTblIndex = key_config->iv_index;


	operation.paramTypes = TEEC_PARAM_TYPES(
			KeyDataParam,
			IVDataParam,
			TEEC_MEMREF_PARTIAL_INPUT,
			TEEC_MEMREF_PARTIAL_INPUT);
	//use the value to represent the shm
	//In ta env, will use params[0].memref.buffer to access
	operation.params[0].memref.parent = ses_key_iv_info->pKeySM;
	operation.params[0].memref.size = ses_key_iv_info->pKeySM->size;
	operation.params[0].memref.offset = 0;

	operation.params[1].memref.parent = ses_key_iv_info->pIVSM;
	operation.params[1].memref.size = ses_key_iv_info->pIVSM->size;
	operation.params[1].memref.offset = 0;

	operation.params[2].memref.parent = ses_key_iv_info->pCryptoSM;
	operation.params[2].memref.size = ses_key_iv_info->pCryptoSM->size;
	operation.params[2].memref.offset = 0;

	operation.params[3].memref.parent = ses_key_iv_info->pKeyInfoSM;
	operation.params[3].memref.size = ses_key_iv_info->pKeyInfoSM->size;
	operation.params[3].memref.offset = 0;

	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_Setkey,
			&operation,
			NULL);
	if (ret != TEEC_SUCCESS)
		pr_err("set key failed, 0x%x\n", ret);

end:
	return tz_m2m_errcode_translate(ret);
}


// TODO should using memid for optee.
int m2m_wrapper_update(TEEC_Session *teec_sess,
				unsigned int inputMemid, size_t inputSize, size_t inputOffset,
				unsigned int outputMemid, size_t outputSize, size_t outputOffset)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_INPUT,
			TEEC_VALUE_INPUT,
			TEEC_VALUE_INPUT,
			TEEC_VALUE_INPUT);

	operation.params[0].value.a = inputMemid;
	operation.params[0].value.b = inputSize;
	operation.params[1].value.a = outputMemid;
	operation.params[1].value.b = outputSize;

	operation.params[2].value.a = inputOffset;
	operation.params[2].value.b = outputOffset;
	operation.params[3].value.a = 1; //TSP_MEM_PARAM_SHM_ID

	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_Update,
			&operation,
			NULL);
	if (ret != TEEC_SUCCESS) {
		pr_err("update failed, 0x%x\n", ret);
		goto end;
	}

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE);

	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_FeedCmd,
			&operation,
			NULL);
	if (unlikely(ret != TEEC_SUCCESS))
		pr_err("Feed Cmd failed, 0x%x\n", ret);

end:
	return tz_m2m_errcode_translate(ret);
}

int m2m_wrapper_routine(TEEC_Session *teec_sess, unsigned int *cmdFinishNum)
{
	TEEC_Result ret = TEEC_SUCCESS;
	TEEC_Operation operation;

	operation.paramTypes = TEEC_PARAM_TYPES(
			TEEC_VALUE_OUTPUT,
			TEEC_NONE,
			TEEC_NONE,
			TEEC_NONE);
	ret = TEEC_InvokeCommand(
			teec_sess,
			M2M_Postprocess,
			&operation,
			NULL);
	*cmdFinishNum = operation.params[0].value.a;

	if (unlikely(ret != TEEC_SUCCESS))
		pr_err("routine failed, 0x%x\n", ret);

	return tz_m2m_errcode_translate(ret);
}
