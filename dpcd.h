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
#ifndef __EXYNOS_HDCP_DPCD_H__
#define __EXYNOS_HDCP_DPCD_H__

int hdcp_dplink_recv(uint32_t msg_name, uint8_t *data, uint32_t size);
int hdcp_dplink_send(uint32_t msg_name, uint8_t *data, uint32_t size);

#endif
