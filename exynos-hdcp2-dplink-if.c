/*
 * drivers/soc/samsung/exynos_hdcp/dp_link/exynos-hdcp2-dplink-if.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/module.h>
#include "exynos-hdcp2-log.h"
#include "exynos-hdcp2-dplink-reg.h"
#include "exynos-hdcp2-dplink-if.h"

static void (*pdp_hdcp22_enable)(u32 en);
static int (*pdp_dpcd_read_for_hdcp22)(u32 address, u32 length, u8 *data);
static int (*pdp_dpcd_write_for_hdcp22)(u32 address, u32 length, u8 *data);

/* Address define for HDCP within DPCD address space */
static uint32_t dpcd_addr[NUM_HDCP_MSG_NAME] = {
	DPCD_ADDR_HDCP13_Bksv,
	DPCD_ADDR_HDCP13_Ri_prime,
	DPCD_ADDR_HDCP13_Aksv,
	DPCD_ADDR_HDCP13_An,
	DPCD_ADDR_HDCP13_Vprime,
	DPCD_ADDR_HDCP13_Bcaps,
	DPCD_ADDR_HDCP13_Bstatus,
	DPCD_ADDR_HDCP13_Binfo,
	DPCD_ADDR_HDCP13_Ksv_fifo,
	DPCD_ADDR_HDCP22_Rtx,
	DPCD_ADDR_HDCP22_TxCaps,
	DPCD_ADDR_HDCP22_cert_rx,
	DPCD_ADDR_HDCP22_Rrx,
	DPCD_ADDR_HDCP22_RxCaps,
	DPCD_ADDR_HDCP22_Ekpub_km,
	DPCD_ADDR_HDCP22_Ekh_km_w,
	DPCD_ADDR_HDCP22_m,
	DPCD_ADDR_HDCP22_Hprime,
	DPCD_ADDR_HDCP22_Ekh_km_r,
	DPCD_ADDR_HDCP22_rn,
	DPCD_ADDR_HDCP22_Lprime,
	DPCD_ADDR_HDCP22_Edkey0_ks,
	DPCD_ADDR_HDCP22_Edkey1_ks,
	DPCD_ADDR_HDCP22_riv,
	DPCD_ADDR_HDCP22_RxInfo,
	DPCD_ADDR_HDCP22_seq_num_V,
	DPCD_ADDR_HDCP22_Vprime,
	DPCD_ADDR_HDCP22_Rec_ID_list,
	DPCD_ADDR_HDCP22_V,
	DPCD_ADDR_HDCP22_seq_num_M,
	DPCD_ADDR_HDCP22_k,
	DPCD_ADDR_HDCP22_stream_IDtype,
	DPCD_ADDR_HDCP22_Mprime,
	DPCD_ADDR_HDCP22_RxStatus,
	DPCD_ADDR_HDCP22_Type,
};

void hdcp_dplink_config(int en)
{
	pdp_hdcp22_enable(en);
}

int hdcp_dplink_is_enabled_hdcp22(void)
{
	/* todo: check hdcp22 enable */
	return 1;
}

/* todo: get stream info from DP */
#define HDCP_DP_STREAM_NUM	0x01
static uint8_t stream_id[1] = {0x00};
int hdcp_dplink_get_stream_info(uint16_t *num, uint8_t *strm_id)
{
	*num = HDCP_DP_STREAM_NUM;
	memcpy(strm_id, stream_id, sizeof(uint8_t) * (*num));

	return 0;
}

int hdcp_dplink_recv(uint32_t msg_name, uint8_t *data, uint32_t size)
{
	return pdp_dpcd_read_for_hdcp22(dpcd_addr[msg_name], size, data);
}

int hdcp_dplink_send(uint32_t msg_name, uint8_t *data, uint32_t size)
{
	return pdp_dpcd_write_for_hdcp22(dpcd_addr[msg_name], size, data);
}

void dp_register_func_for_hdcp22(void (*func0)(u32 en), int (*func1)(u32 address, u32 length, u8 *data), int (*func2)(u32 address, u32 length, u8 *data))
{
	pdp_hdcp22_enable = func0;
	pdp_dpcd_read_for_hdcp22 = func1;
	pdp_dpcd_write_for_hdcp22 = func2;
}
EXPORT_SYMBOL_GPL(dp_register_func_for_hdcp22);
