// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *
 * Samsung DisplayPort driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include <drm/drm_dp_helper.h>

#include "exynos-hdcp1-auth.h"
#include "exynos-hdcp2.h"
#include "exynos-hdcp2-dplink-if.h"
#include "exynos-hdcp2-log.h"
#include "exynos-hdcp2-teeif.h"

#define HDCP_R0_SIZE 2
#define HDCP_BKSV_SIZE 5
#define HDCP_AN_SIZE 8
#define HDCP_AKSV_SIZE 5

#define V_READ_RETRY_CNT 3
#define RI_READ_RETRY_CNT 3
#define RI_AVAILABLE_WAITING 2
#define RI_DELAY 100
#define REPEATER_READY_MAX_WAIT_DELAY 5000

#define MAX_CASCADE_EXCEEDED (0x00000800)
#define MAX_DEVS_EXCEEDED (0x00000080)
#define BKSV_LIST_FIFO_SIZE (15)

extern enum dp_state dp_hdcp_state;
extern enum auth_state auth_proc_state;

static int compare_rprime(void)
{
	uint8_t bstatus, ri_retry_cnt = 0;
	uint16_t rprime;
	int ret;

	usleep_range(RI_DELAY * 1000, RI_DELAY * 1000 + 1);

	ret = hdcp_dplink_recv(HDCP13_MSG_BSTATUS_R, &bstatus,
		sizeof(bstatus));
	if (ret || !(bstatus & DP_BSTATUS_R0_PRIME_READY)) {
		hdcp_err("BSTATUS read err ret(%d) bstatus(%d)\n",
			ret, bstatus);
		return -EIO;
	}

	hdcp_info("R0-Prime is ready in HDCP Receiver\n");
	do {
		ret = hdcp_dplink_recv(HDCP13_MSG_RI_PRIME_R, (uint8_t*)&rprime,
			HDCP_R0_SIZE);
		if (!ret) {
			ret = teei_verify_r_prime(rprime);
			if (!ret)
				return 0;

			hdcp_err("RPrime verification fails (%d)\n", ret);
		} else {
			hdcp_err("RPrime read fails (%d)\n", ret);
		}

		ri_retry_cnt++;
		usleep_range(RI_DELAY * 1000, RI_DELAY * 1000 + 1);
	}
	while (ri_retry_cnt < RI_READ_RETRY_CNT);

	return -EFAULT;
}

static int read_ksv_list(u8* hdcp_ksv, u32 len)
{
	uint32_t read_len = len < BKSV_LIST_FIFO_SIZE ?
		len : BKSV_LIST_FIFO_SIZE;

	return hdcp_dplink_recv(HDCP13_MSG_KSV_FIFO_R, hdcp_ksv, read_len) ?
		-EIO : read_len;
}

static int proceed_repeater(void)
{
	uint8_t ksv_list[HDCP_KSV_MAX_LEN];
	uint8_t *ksv_list_ptr = ksv_list;
	uint16_t binfo;
	uint8_t v_read_retry_cnt = 0;
	uint8_t vprime[HDCP_SHA1_SIZE];
	ktime_t start_time_ns;
	int64_t waiting_time_ms;
	uint8_t bstatus;
	uint32_t ksv_len, bytes_read;
	int ret;

	hdcp_info("Start HDCP Repeater Authentication!!!\n");

	// Step0-1. Poll BStatus Ready
	start_time_ns = ktime_get();
	do {
		usleep_range(RI_AVAILABLE_WAITING * 1000, RI_AVAILABLE_WAITING * 1000 + 1);
		waiting_time_ms = (s64)((ktime_get() - start_time_ns) / 1000000);
		if ((waiting_time_ms >= REPEATER_READY_MAX_WAIT_DELAY) ||
		    (dp_hdcp_state == DP_DISCONNECT)) {
			hdcp_err("Not repeater ready in RX part %lld\n",
				waiting_time_ms);
			return -EINVAL;
		}

		ret = hdcp_dplink_recv(HDCP13_MSG_BSTATUS_R, &bstatus,
				sizeof(bstatus));
		if (ret) {
			hdcp_err("Read BSTATUS failed (%d)\n", ret);
			return -EIO;
		}
	} while (!(bstatus & DP_BSTATUS_READY));
	hdcp_info("Ready HDCP RX Repeater!!!\n");

	if (dp_hdcp_state == DP_DISCONNECT)
		return -EINVAL;

	ret = hdcp_dplink_recv(HDCP13_MSG_BINFO_R, (uint8_t*)&binfo, HDCP_BINFO_SIZE);
	if (ret) {
		hdcp_err("Read BINFO failed (%d)\n", ret);
		return -EIO;
	}

	if (binfo & MAX_DEVS_EXCEEDED) {
		hdcp_err("Max Devs Exceeded\n");
		return -EIO;
	}

	if (binfo & MAX_CASCADE_EXCEEDED) {
		hdcp_err("Max Cascade Exceeded\n");
		return -EIO;
	}

	ksv_len = (binfo & HDCP_BINFO_DEVS_COUNT_MAX) * HDCP_KSV_SIZE;
	while (ksv_len != 0) {
		bytes_read = read_ksv_list(ksv_list_ptr, ksv_len);
		if (bytes_read < 0) {
			hdcp_err("Read KSV failed (%d)\n", bytes_read);
			return -EIO;
		}
		ksv_len -= bytes_read;
		ksv_list_ptr += bytes_read;
	}

	do {
		ret = hdcp_dplink_recv(HDCP13_MSG_VPRIME_R, vprime,
			HDCP_SHA1_SIZE);
		if (!ret) {
			ret = teei_verify_v_prime(binfo, ksv_list,
				ksv_list_ptr - ksv_list, vprime);
			if (!ret) {
				hdcp_info("Done 2nd Authentication!!!\n");
				return 0;
			}

			hdcp_err("Vprime verify failed (%d)\n", ret);
		} else
			hdcp_err("Vprime read failed (%d)\n", ret);

		v_read_retry_cnt++;
	} while(v_read_retry_cnt < V_READ_RETRY_CNT);

	hdcp_err("2nd Auth fail!!!\n");
	return -EIO;
}

void hdcp13_dplink_authenticate(void)
{
	uint64_t aksv, bksv, an;
	uint8_t bcaps;
	int ret;

	hdcp_info("Start SW Authentication\n");

	if (dp_hdcp_state == DP_DISCONNECT) {
		hdcp_err("DP is disconnected\n");
		return;
	}

	auth_proc_state = HDCP_AUTH_PROCESS_IDLE;

	aksv = bksv = an = 0;
	ret = hdcp_dplink_recv(HDCP13_MSG_BKSV_R, (uint8_t*)&bksv, HDCP_BKSV_SIZE);
	if (ret) {
		hdcp_err("Read Bksv failed (%d)\n", ret);
		return;
	}

	ret = teei_ksv_exchange(bksv, &aksv, &an);
	if (ret) {
		hdcp_err("Ksv exchange failed (%d)\n", ret);
		return;
	}

	ret = hdcp_dplink_send(HDCP13_MSG_AN_W, (uint8_t*)&an, HDCP_AN_SIZE);
	if (ret) {
		hdcp_err("Write AN failed (%d)\n", ret);
		return;
	}
	ret = hdcp_dplink_send(HDCP13_MSG_AKSV_W, (uint8_t*)&aksv, HDCP_AKSV_SIZE);
	if (ret) {
		hdcp_err("Write AKSV failed (%d)\n", ret);
		return;
	}

	if (compare_rprime() != 0) {
		hdcp_err("R0 is not same\n");
		return;
	}

	hdcp_tee_enable_enc_13();
	hdcp_info("Done 1st Authentication\n");

	ret = hdcp_dplink_recv(HDCP13_MSG_BCAPS_R, (uint8_t*)&bcaps, sizeof(bcaps));
	if (ret) {
		hdcp_err("BCaps Read failure (%d)\n", ret);
		return;
	}

	if ((bcaps & DP_BCAPS_REPEATER_PRESENT) && proceed_repeater()) {
		hdcp_err("HDCP Authentication fail!!!\n");
		hdcp_tee_disable_enc();
		return;
	}

	auth_proc_state = HDCP1_AUTH_PROCESS_DONE;
	hdcp_info("Done SW Authentication\n");
	return;
}

