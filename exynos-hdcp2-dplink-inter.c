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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smc.h>
#include <asm/cacheflush.h>
#include <linux/soc/samsung/exynos-smc.h>
#include <linux/types.h>
#include <linux/delay.h>

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
EXPORT_SYMBOL_GPL(auth_proc_state);
enum dp_state dp_hdcp_state;
EXPORT_SYMBOL_GPL(dp_hdcp_state);

int hdcp_dplink_auth_check(enum auth_signal hdcp_signal)
{
	int ret = 0;

	switch (hdcp_signal) {
		case HDCP_DRM_OFF:
			return ret;
		case HDCP_DRM_ON:
			dplink_clear_irqflag_all();
			ret = hdcp_dplink_authenticate();
			return ret;
		case HDCP_RP_READY:
			if (auth_proc_state == HDCP_AUTH_PROCESS_DONE) {
				ret = hdcp_dplink_authenticate();
				if (ret == 0)
					auth_proc_state = HDCP_AUTH_PROCESS_DONE;
			}
			return 0;
		default:
			ret = HDCP_ERROR_INVALID_STATE;
			break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_auth_check);

int hdcp_dplink_get_rxstatus(uint8_t *status)
{
	int ret = 0;

	ret = hdcp_dplink_get_rxinfo(status);
	return ret;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_get_rxstatus);

int hdcp_dplink_set_paring_available(void)
{
	hdcp_info("pairing avaible\n");
	return dplink_set_paring_available();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_paring_available);

int hdcp_dplink_set_hprime_available(void)
{
	hdcp_info("h-prime avaible\n");
	return dplink_set_hprime_available();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_hprime_available);

int hdcp_dplink_set_rp_ready(void)
{
	hdcp_info("ready avaible\n");
	return dplink_set_rp_ready();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_rp_ready);

int hdcp_dplink_set_reauth(void)
{
	hdcp_info("reauth requested.\n");
	hdcp_tee_send_cmd(HDCP_CMD_AUTH_CANCEL);
	return dplink_set_reauth_req();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_reauth);

int hdcp_dplink_set_integrity_fail(void)
{
	hdcp_info("integrity check fail.\n");
	hdcp_tee_send_cmd(HDCP_CMD_AUTH_CANCEL);
	return dplink_set_integrity_fail();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_set_integrity_fail);

int hdcp_dplink_cancel_auth(void)
{
	hdcp_info("Cancel authenticate.\n");
	hdcp_tee_send_cmd(HDCP_CMD_AUTH_CANCEL);
	auth_proc_state = HDCP_AUTH_PROCESS_STOP;

	return dplink_set_integrity_fail();
}
EXPORT_SYMBOL_GPL(hdcp_dplink_cancel_auth);

void hdcp_dplink_clear_all(void)
{
	hdcp_info("HDCP flag clear\n");
	hdcp_tee_send_cmd(HDCP_CMD_AUTH_CANCEL);
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
