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
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/module.h>

#include "exynos-hdcp-interface.h"

#include "auth-control.h"
#include "auth13.h"
#include "auth22.h"
#include "hdcp-log.h"
#include "teeif.h"

#define HDCP_SCHEDULE_DELAY_MSEC (5000)

static struct delayed_work hdcp_work;

static enum auth_state state;

static unsigned long max_ver = 2;
module_param(max_ver, ulong, 0664);
MODULE_PARM_DESC(max_ver,
	"support up to specific hdcp version by setting max_ver=x");

int hdcp_get_auth_state(void) {
	return state;
}

static int run_hdcp2_auth(void) {
	int ret;
	int i;

	state = HDCP2_AUTH_PROGRESS;
	for (i = 0; i < 5; ++i) {
		ret = hdcp22_dplink_authenticate();
		if (ret == 0) {
			state = HDCP2_AUTH_DONE;
			/* HDCP2.2 spec defined 200ms */
			msleep(200);
			hdcp_tee_enable_enc_22();
			return 0;
		} else if (ret != -EAGAIN) {
			return -EIO;
		}
		hdcp_info("HDCP22 Retry...\n");
	}

	return -EIO;
}

static void hdcp_worker(struct work_struct *work) {
	if (max_ver >= 2) {
		hdcp_info("Trying HDCP22...\n");
		if (run_hdcp2_auth() == 0) {
			hdcp_info("HDCP22 Authentication Success\n");
			return;
		}
		hdcp_info("HDCP22 Authentication Failed.\n");
	} else {
		hdcp_info("Not trying HDCP22. max_ver is %lu\n", max_ver);
	}

	if (max_ver >= 1) {
		hdcp_info("Trying HDCP13...\n");
		state = HDCP1_AUTH_PROGRESS;
		if (hdcp13_dplink_authenticate() == 0) {
			hdcp_info("HDCP13 Authentication Success\n");
			state = HDCP1_AUTH_DONE;
			return;
		}

		state = HDCP_AUTH_IDLE;
		hdcp_info("HDCP13 Authentication Failed.\n");
	} else {
		hdcp_info("Not trying HDCP13. max_ver is %lu\n", max_ver);
	}
}

void hdcp_dplink_handle_irq(void) {
	if (state == HDCP2_AUTH_PROGRESS || state == HDCP2_AUTH_DONE) {
		if (hdcp22_dplink_handle_irq() == -EAGAIN)
			schedule_delayed_work(&hdcp_work, 0);
		return;
	}

	if (state == HDCP1_AUTH_DONE) {
		if (hdcp13_dplink_handle_irq() == -EAGAIN)
			schedule_delayed_work(&hdcp_work, 0);
		return;
	}
}
EXPORT_SYMBOL_GPL(hdcp_dplink_handle_irq);


void hdcp_dplink_connect_state(enum dp_state dp_hdcp_state) {
	hdcp_info("Displayport connect info (%d)\n", dp_hdcp_state);
	hdcp_tee_connect_info((int)dp_hdcp_state);
	if (dp_hdcp_state == DP_DISCONNECT) {
		hdcp13_dplink_abort();
		hdcp22_dplink_abort();
		hdcp_tee_disable_enc();
		state = HDCP_AUTH_IDLE;
		if (delayed_work_pending(&hdcp_work))
			cancel_delayed_work(&hdcp_work);
		return;
	}

	schedule_delayed_work(&hdcp_work, msecs_to_jiffies(HDCP_SCHEDULE_DELAY_MSEC));
	return;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_connect_state);

void hdcp_auth_worker_init(void) {
	INIT_DELAYED_WORK(&hdcp_work, hdcp_worker);
}

void hdcp_auth_worker_deinit(void) {
	cancel_delayed_work_sync(&hdcp_work);
}
