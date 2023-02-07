/*
 * drivers/soc/samsung/exynos-hdcp/exynos-hdcp2-protocol-msg.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>

#include "exynos-hdcp2-protocol-msg.h"
#include "exynos-hdcp2-teeif.h"
#include "exynos-hdcp2.h"
#include "exynos-hdcp2-log.h"
#include "exynos-hdcp2-dplink-protocol-msg.h"

int ske_generate_sessionkey(uint32_t lk_type, uint8_t *enc_skey, int share_skey)
{
	int ret;

	ret = teei_generate_skey(lk_type,
			enc_skey, HDCP_SKE_SKEY_LEN,
			share_skey);
	if (ret) {
		hdcp_err("generate_session_key() is failed with %x\n", ret);
		return ERR_GENERATE_SESSION_KEY;
	}

	return 0;
}

int ske_generate_riv(uint8_t *out)
{
	int ret;

	ret = teei_generate_riv(out, HDCP_RTX_BYTE_LEN);
	if (ret) {
		hdcp_err("teei_generate_riv() is failed with %x\n", ret);
		return ERR_GENERATE_NON_SECKEY;
	}

	return 0;
}

int lc_generate_rn(uint8_t *out, size_t out_len)
{
	int ret;

	ret = teei_gen_rn(out, out_len);
	if (ret) {
		hdcp_err("lc_generate_rn() is failed with %x\n", ret);
		return ERR_GENERATE_NON_SECKEY;
	}

	return 0;
}

int lc_compare_hmac(uint8_t *rx_hmac, size_t hmac_len)
{
	int ret;

	ret = teei_compare_lc_hmac(rx_hmac, hmac_len);
	if (ret) {
		hdcp_err("compare_lc_hmac_val() is failed with %x\n", ret);
		return ERR_COMPARE_LC_HMAC;
	}

	return ret;
}

int ake_verify_cert(uint8_t *cert, size_t cert_len,
		uint8_t *rrx, size_t rrx_len,
		uint8_t *rx_caps, size_t rx_caps_len)
{
	int ret;

	ret = teei_verify_cert(cert, cert_len,
				rrx, rrx_len,
				rx_caps, rx_caps_len);
	if (ret) {
		hdcp_err("teei_verify_cert() is failed with %x\n", ret);
		return ERR_VERIFY_CERT;
	}

	return 0;
}

int ake_generate_masterkey(uint32_t lk_type, uint8_t *enc_mkey, size_t elen)
{
	int ret;

	/* Generate Encrypted & Wrapped Master Key */
	ret = teei_generate_master_key(lk_type, enc_mkey, elen);
	if (ret) {
		hdcp_err("generate_master_key() is failed with %x\n", ret);
		return ERR_GENERATE_MASTERKEY;
	}

	return ret;
}

int ake_compare_hmac(uint8_t *rx_hmac, size_t rx_hmac_len)
{
	int ret;

	ret = teei_compare_ake_hmac(rx_hmac, rx_hmac_len);
	if (ret) {
		hdcp_err("teei_compare_hmac() is failed with %x\n", ret);
		return ERR_COMPUTE_AKE_HMAC;
	}

	return 0;
}

int ake_store_master_key(uint8_t *ekh_mkey, size_t ekh_mkey_len)
{
	int ret;

	ret = teei_set_pairing_info(ekh_mkey, ekh_mkey_len);
	if (ret) {
		hdcp_err("teei_store_pairing_info() is failed with %x\n", ret);
		return ERR_STORE_MASTERKEY;
	}

	return 0;
}

int ake_find_masterkey(int *found_km,
		uint8_t *ekh_mkey, size_t ekh_mkey_len,
		uint8_t *m, size_t m_len)
{
	int ret;

	ret = teei_get_pairing_info(ekh_mkey, ekh_mkey_len, m, m_len, found_km);
	if (ret) {
		*found_km = 0;
		hdcp_err("teei_store_pairing_info() is failed with %x\n", ret);
		return ERR_FIND_MASTERKEY;
	}

	return 0;
}
