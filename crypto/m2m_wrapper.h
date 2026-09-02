// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Synaptics Incorporated
 */

#ifndef __M2M_WRAPPER_H__
#define __M2M_WRAPPER_H__
#include <linux/device.h>
#if !IS_ENABLED(CONFIG_OPTEE)
#include "tee_client_api.h"
#endif
#include "uapi/m2m.h"

#if IS_ENABLED(CONFIG_OPTEE)
typedef uint32_t TEEC_Session;
#endif

enum m2m_feed_mode {
	M2M_FEED_MODE_SYNC,
	M2M_FEED_MODE_ASYNC,
};

struct m2m_key_iv_info {
	#ifndef CONFIG_OPTEE
		TEEC_SharedMemory *pCryptoSM;
		TEEC_SharedMemory *pKeyInfoSM;
		TEEC_SharedMemory *pKeySM;
		TEEC_SharedMemory *pIVSM;
	#else
		struct tee_shm *pCryptoSM;
		struct tee_shm *pKeyInfoSM;
		struct tee_shm *pKeySM;
		struct tee_shm *pIVSM;
	#endif
};

struct drm_crypto_info {
	uint32_t  uiSchemeType;                   /**< The Scheme type of DRM */
	uint32_t  uiSessionID;                    /**< The identify of Session */
	uint8_t   uiKeyID[M2M_MAX_KEY_ID_LENGTH]; /**< The identify Key */
	uint32_t  uiKeyIdLen;                     /**< The length of key id */
};

struct m2m_key_info {
	uint32_t  uiKeyOption;
	uint32_t  uiKeyLength;
	uint32_t  uiKeyTblIndex;
	uint32_t  uiIVOption;
	uint32_t  uiIVLength;
	uint32_t  uiIVTblIndex;
};


int m2m_wrapper_init(struct device *dev);
void m2m_wrapper_exit(void);
int m2m_wrapper_open_session(TEEC_Session *teec_sess, unsigned int crypto_mode);
void m2m_wrapper_close_session(TEEC_Session *teec_sess);
int m2m_wrapper_set_scheme(TEEC_Session *teec_sess, unsigned int scheme);
int m2m_wrapper_set_patternMode(TEEC_Session *teec_sess,
				struct m2m_pattern_mode *pattern);
int m2m_wrapper_set_residueMode(TEEC_Session *teec_sess,
				unsigned int residueMode);
int m2m_wrapper_set_ivPolicy(TEEC_Session *teec_sess, unsigned int iv_policy);
int m2m_wrapper_config(TEEC_Session *teec_sess, struct m2m_key *key_config,
						struct m2m_key_iv_info *ses_key_iv_info);
int m2m_wrapper_update(TEEC_Session *teec_sess,
				unsigned int inputPhyAddr, size_t inputSize, size_t inputOffset,
				unsigned int outputPhyAddr, size_t outputSize, size_t outputOffset);
int m2m_wrapper_routine(TEEC_Session *teec_sess, unsigned int *cmdFinishNum);
int m2m_wrapper_set_IVPolicy(TEEC_Session *teec_sess, unsigned int iv_policy);
int m2m_wrapper_KeyIV_allocaiton(TEEC_Session *teec_sess, struct m2m_key *key_config);
int m2m_wrapper_alloc(unsigned int len, void **handle);
void m2m_wrapper_free(void *handle);

#endif
