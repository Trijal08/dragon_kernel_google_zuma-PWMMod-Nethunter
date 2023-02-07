/*
 * drivers/soc/samsung/exynos-hdcp/exynos-hdcp2-encrypt.c
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
#include "exynos-hdcp2-log.h"
#include <linux/kernel.h>
#include <linux/module.h>

static void OS2BN(uint32_t *pdRes, uint8_t *pbSrc, size_t uSrcLen)
{
	int i;

	for (i = 0; i < uSrcLen; i += 4)
		pdRes[i/4] = pbSrc[uSrcLen-i-1] ^ (pbSrc[uSrcLen-i-2]<<8)
				^ (pbSrc[uSrcLen-i-3]<<16)
				^ (pbSrc[uSrcLen-i-4]<<24);
}

static void BN2OS(uint8_t *pbRes, uint32_t *pdSrc, size_t uSrcLen)
{
	int i;

	for (i = 0; i < uSrcLen; i++) {
		pbRes[4*i+0] = (uint8_t) (pdSrc[uSrcLen-1-i] >> 24);
		pbRes[4*i+1] = (uint8_t) (pdSrc[uSrcLen-1-i] >> 16);
		pbRes[4*i+2] = (uint8_t) (pdSrc[uSrcLen-1-i] >>  8);
		pbRes[4*i+3] = (uint8_t) (pdSrc[uSrcLen-1-i]);
	}
}

static uint32_t sec_bn_Add(uint32_t *pdDst, uint32_t *pdSrc1,	size_t uSrcLen1,
			uint32_t *pdSrc2, size_t uSrcLen2)
{
	int i;
	uint32_t carry, tmp;

	for (carry = i = 0; i < uSrcLen2; i++) {
		if ((pdSrc2[i] == 0xff) && (carry == 1))
			pdDst[i] = pdSrc1[i];
		else {
			tmp = pdSrc2[i] + carry;
			pdDst[i] = pdSrc1[i] + tmp;
			carry = ((pdDst[i]) < tmp) ? 1 : 0;
		}
	}

	for (i = uSrcLen2; i < uSrcLen1; i++) {
		pdDst[i] += carry;
		if (pdDst[i] >= carry)
			carry = 0;
		else
			carry = 1;
	}

	return carry;
}

static int make_priv_data(uint8_t *priv_data, uint8_t *str_ctr, uint8_t *input_ctr)
{
	uint8_t marker_bit;

	marker_bit = 0x1;

	priv_data[0] = 0x0;
	priv_data[1] = (str_ctr[0] >> 5) | marker_bit;
	priv_data[2] = (str_ctr[0] << 2) ^ (str_ctr[1] >> 6);
	priv_data[3] = ((str_ctr[1] << 2) ^ (str_ctr[2] >> 6)) | marker_bit;
	priv_data[4] = (str_ctr[2] << 1) ^ (str_ctr[3] >> 7);
	priv_data[5] = (str_ctr[3] << 1) | marker_bit;
	priv_data[6] = 0x0;
	priv_data[7] = (input_ctr[0] >> 3) | marker_bit;
	priv_data[8] = (input_ctr[0] << 4) ^ (input_ctr[1] >> 4);
	priv_data[9] = ((input_ctr[1] << 4) ^ (input_ctr[2] >> 4)) | marker_bit;
	priv_data[10] = (input_ctr[2] << 3) ^ (input_ctr[3] >> 5);
	priv_data[11] = ((input_ctr[3] << 3) ^ (input_ctr[4] >> 5)) | marker_bit;
	priv_data[12] = (input_ctr[4] << 2) ^ (input_ctr[5] >> 6);
	priv_data[13] = ((input_ctr[5] << 2) ^ (input_ctr[6] >> 6)) | marker_bit;
	priv_data[14] = (input_ctr[6] << 1) ^ (input_ctr[7] >> 7);
	priv_data[15] = (input_ctr[7] << 1) | marker_bit;

	return 0;
}

MODULE_LICENSE("GPL");
