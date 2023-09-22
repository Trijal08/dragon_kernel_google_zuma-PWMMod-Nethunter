// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */
#include <drm/drm_modes.h>

#include <linux/delay.h>

#include "gs_panel/gs_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"

#define PANEL_ID_REG_DEFAULT 0xA1
#define PANEL_ID_LEN 7
#define PANEL_ID_OFFSET 6
#define PANEL_ID_READ_SIZE (PANEL_ID_LEN + PANEL_ID_OFFSET)
#define PANEL_SLSI_DDIC_ID_REG 0xD6
#define PANEL_SLSI_DDIC_ID_LEN 5

void gs_panel_get_panel_rev(struct gs_panel *ctx, u8 rev)
{
	switch (rev) {
	case 0:
		ctx->panel_rev = PANEL_REV_PROTO1;
		break;
	case 1:
		ctx->panel_rev = PANEL_REV_PROTO1_1;
		break;
	case 2:
		ctx->panel_rev = PANEL_REV_PROTO1_2;
		break;
	case 8:
		ctx->panel_rev = PANEL_REV_EVT1;
		break;
	case 9:
		ctx->panel_rev = PANEL_REV_EVT1_1;
		break;
	case 0xA:
		ctx->panel_rev = PANEL_REV_EVT1_2;
		break;
	case 0xC:
		ctx->panel_rev = PANEL_REV_DVT1;
		break;
	case 0xD:
		ctx->panel_rev = PANEL_REV_DVT1_1;
		break;
	case 0x10:
		ctx->panel_rev = PANEL_REV_PVT;
		break;
	case 0x14:
		ctx->panel_rev = PANEL_REV_MP;
		break;
	default:
		dev_warn(ctx->dev, "unknown rev from panel (0x%x), default to latest\n", rev);
		ctx->panel_rev = PANEL_REV_LATEST;
		return;
	}

	dev_info(ctx->dev, "panel_rev: 0x%x\n", ctx->panel_rev);
}
EXPORT_SYMBOL(gs_panel_get_panel_rev);

int gs_panel_read_slsi_ddic_id(struct gs_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = ctx->dev;
	char buf[PANEL_SLSI_DDIC_ID_LEN] = { 0 };
	int ret;

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xF0, 0x5A, 0x5A);
	ret = mipi_dsi_dcs_read(dsi, PANEL_SLSI_DDIC_ID_REG, buf, PANEL_SLSI_DDIC_ID_LEN);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xF0, 0xA5, 0xA5);
	if (ret != PANEL_SLSI_DDIC_ID_LEN) {
		dev_warn(dev, "Unable to read DDIC id (%d)\n", ret);
		return ret;
	}

	bin2hex(ctx->panel_id, buf, PANEL_SLSI_DDIC_ID_LEN);
	return 0;
}
EXPORT_SYMBOL(gs_panel_read_slsi_ddic_id);

int gs_panel_read_id(struct gs_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char buf[PANEL_ID_READ_SIZE];
	int ret;

	ret = mipi_dsi_dcs_read(dsi, ctx->desc->panel_id_reg ?: PANEL_ID_REG_DEFAULT, buf,
				PANEL_ID_READ_SIZE);
	if (ret != PANEL_ID_READ_SIZE) {
		dev_warn(ctx->dev, "Unable to read panel id (%d)\n", ret);
		return ret;
	}

	bin2hex(ctx->panel_id, buf + PANEL_ID_OFFSET, PANEL_ID_LEN);

	return 0;
}
EXPORT_SYMBOL(gs_panel_read_id);

bool gs_panel_is_mode_seamless_helper(const struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const struct drm_display_mode *current_mode = &ctx->current_mode->mode;
	const struct drm_display_mode *new_mode = &pmode->mode;

	return drm_mode_equal_no_clocks(current_mode, new_mode);
}
EXPORT_SYMBOL(gs_panel_is_mode_seamless_helper);

ssize_t gs_panel_get_te2_edges_helper(struct gs_panel *ctx, char *buf, bool lp_mode)
{
	struct gs_te2_mode_data *data;
	size_t len = 0;
	int i;

	if (!ctx)
		return -EINVAL;

	for_each_te2_timing(ctx, lp_mode, data, i) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%dx%d@%d", data->mode->hdisplay,
				 data->mode->vdisplay, drm_mode_vrefresh(data->mode));

		if (data->binned_lp)
			len += scnprintf(buf + len, PAGE_SIZE - len, "-lp_%s",
					 data->binned_lp->name);

		len += scnprintf(buf + len, PAGE_SIZE - len, " rising %u falling %u\n",
				 data->timing.rising_edge, data->timing.falling_edge);
	}

	return len;
}
EXPORT_SYMBOL(gs_panel_get_te2_edges_helper);

int gs_panel_set_te2_edges_helper(struct gs_panel *ctx, u32 *timings, bool lp_mode)
{
	struct gs_te2_mode_data *data;
	const u32 *t;
	int i;

	if (!ctx || !timings)
		return -EINVAL;

	t = timings;

	for_each_te2_timing(ctx, lp_mode, data, i) {
		data->timing.rising_edge = t[0];
		data->timing.falling_edge = t[1];
		t += 2;
	}

	return 0;
}
EXPORT_SYMBOL(gs_panel_set_te2_edges_helper);
