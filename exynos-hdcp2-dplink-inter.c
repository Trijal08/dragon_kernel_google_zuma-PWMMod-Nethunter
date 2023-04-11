/*
 * drivers/soc/samsung/exynos-hdcp/dp_link/exynos-hdcp2-dplink.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <drm/drm_dp_helper.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smc.h>
#include <asm/cacheflush.h>
#include <linux/soc/samsung/exynos-smc.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "exynos-hdcp1-auth.h"
#include "exynos-hdcp2.h"
#include "exynos-hdcp2-log.h"
#include "exynos-hdcp2-dplink.h"
#include "exynos-hdcp2-dplink-inter.h"
#include "exynos-hdcp2-dplink-if.h"
#include "exynos-hdcp2-dplink-auth.h"
#include "exynos-hdcp2-teeif.h"

#define DRM_WAIT_RETRY_COUNT	1000
/* current link data */
enum auth_state auth_proc_state;
enum dp_state dp_hdcp_state;

int hdcp_dplink_auth_control(enum auth_signal hdcp_signal)
{
	switch (hdcp_signal) {
		case HDCP_OFF:
			return 0;
		case HDCP1_ON:
			hdcp13_dplink_authenticate();
			return 0;
		case HDCP2_ON:
			dplink_clear_irqflag_all();
			return hdcp_dplink_authenticate();
		default:
			return HDCP_ERROR_INVALID_STATE;
			break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_auth_control);

int hdcp_dplink_handle_hdcp22_irq(void) {
	uint8_t rxstatus = 0;
	hdcp_dplink_get_rxinfo(&rxstatus);

	if (HDCP_2_2_DP_RXSTATUS_LINK_FAILED(rxstatus)) {
		hdcp_info("integrity check fail.\n");
		hdcp_tee_disable_enc();
		dplink_set_integrity_fail();
		return 0;
	} else if (HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(rxstatus)) {
		hdcp_info("reauth requested.\n");
		hdcp_tee_disable_enc();
		dplink_set_reauth_req();
		return -EAGAIN;
	} else if (HDCP_2_2_DP_RXSTATUS_PAIRING(rxstatus)) {
		hdcp_info("pairing avaible\n");
		dplink_set_paring_available();
		return 0;
	} else if (HDCP_2_2_DP_RXSTATUS_H_PRIME(rxstatus)) {
		hdcp_info("h-prime avaible\n");
		dplink_set_hprime_available();
		return 0;
	} else if (HDCP_2_2_DP_RXSTATUS_READY(rxstatus)) {
		hdcp_info("ready avaible\n");
		dplink_set_rp_ready();
		if (auth_proc_state == HDCP2_AUTH_PROCESS_DONE) {
			if (hdcp_dplink_authenticate() == 0)
				auth_proc_state = HDCP2_AUTH_PROCESS_DONE;
		}
		return 0;
	}

	hdcp_err("undefined RxStatus(0x%x). ignore\n", rxstatus);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_handle_hdcp22_irq);

int hdcp_dplink_handle_hdcp13_irq(void)
{
	uint8_t bstatus;

	if (auth_proc_state != HDCP1_AUTH_PROCESS_DONE) {
		hdcp_err("Ignoring IRQ during auth\n");
		return 0;
	}

	hdcp_dplink_recv(HDCP13_MSG_BSTATUS_R, &bstatus,
		sizeof(bstatus));

	if (bstatus & DP_BSTATUS_LINK_FAILURE ||
	    bstatus & DP_BSTATUS_REAUTH_REQ) {
		hdcp_err("Resetting link and encryption\n");
		hdcp_tee_disable_enc();
		return -EAGAIN;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_handle_hdcp13_irq);

int hdcp_dplink_cancel_auth(void)
{
	hdcp_info("Cancel authenticate.\n");
	hdcp_tee_disable_enc();
	auth_proc_state = HDCP_AUTH_PROCESS_STOP;

	return dplink_set_integrity_fail();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_cancel_auth);

void hdcp_dplink_clear_all(void)
{
	hdcp_info("HDCP flag clear\n");
	hdcp_tee_disable_enc();
	dplink_clear_irqflag_all();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_clear_all);

void hdcp_dplink_connect_state(enum dp_state state)
{
	dp_hdcp_state = state;
	hdcp_info("Displayport connect info (%d)\n", dp_hdcp_state);
	hdcp_tee_connect_info((int)dp_hdcp_state);
}
EXPORT_SYMBOL_GPL(hdcp_dplink_connect_state);

int hdcp_dplink_auth_check(enum auth_signal hdcp_signal)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_auth_check);

int hdcp_dplink_get_rxstatus(uint8_t *status)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_get_rxstatus);

int hdcp_dplink_set_paring_available(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_paring_available);

int hdcp_dplink_set_hprime_available(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_hprime_available);

int hdcp_dplink_set_rp_ready(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_rp_ready);

int hdcp_dplink_set_reauth(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_reauth);

int hdcp_dplink_set_integrity_fail(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_integrity_fail);

