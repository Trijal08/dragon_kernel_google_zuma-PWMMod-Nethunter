/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"

/* Initialization */

static int panel_gs_simple_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;

	dev_dbg(dev, "%s+\n", __func__);

	gs_panel_reset_helper(ctx);
	/* this is where we'd send init cmdset */

	dev_dbg(dev, "%s-\n", __func__);
	return 0;
}

/* Module Description */

static const struct drm_panel_funcs panel_gs_simple_drm_funcs = {
	.enable = panel_gs_simple_enable,
	.disable = gs_panel_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.get_modes = gs_panel_get_modes,
};

static const struct gs_panel_funcs panel_gs_simple_panel_funcs = {
	.set_brightness = gs_dcs_set_brightness,
};

const struct brightness_capability panel_gs_simple_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 1000,
		},
		.level = {
			.min = 1,
			.max = 3574,
		},
		.percentage = {
			.min = 0,
			.max = 71,
		},
	},
};

static struct gs_panel_mode_array panel_gs_simple_normal_modes = {
	.num_modes = 1,
	.modes = {
		{
			.mode = {
				.name = "640x480",
				/* htotal * vtotal * refresh (22.464MHz) */
				.clock = (720 * 520 * 60) / 1000,
				.hdisplay = 640,
				.hsync_start = 640 + 32, /* add hfp */
				.hsync_end = 640 + 32 + 12, /* add hsa */
				.htotal = 640 + 32 + 12 + 36, /* add hbp */
				.vdisplay = 480,
				.vsync_start = 480 + 12, /* add vfp */
				.vsync_end = 480 + 12 + 4, /* add vsa */
				.vtotal = 480 + 12 + 4 + 24, /* add vbp */
				.flags = 0,
				.width_mm = 64,
				.height_mm = 48,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.bpc = 8,
			},
		},
	},
};

static int update_panel_timings_from_device_tree(struct device_node *np)
{
	/*
	 * TODO(b/197774385): instead store in driver priv data, override
	 * get_mode and get_modes methods, rather than modifying const mode
	 * objects.
	 */
	struct drm_display_mode *mode =
		(struct drm_display_mode *)&panel_gs_simple_normal_modes.modes[0].mode;
	int ret = 0;

	ret = of_get_drm_panel_display_mode(np, mode, NULL);
	if (ret) {
		pr_warn("%p of_get_drm_panel_display_mode returned %d\n", np, ret);
	}
	return ret;
};

/* Probe */

static int panel_gs_simple_probe(struct mipi_dsi_device *dsi)
{
	update_panel_timings_from_device_tree(dsi->dev.of_node);

	return gs_dsi_panel_common_probe(dsi);
}

/* Module description (cont.) */

const struct gs_panel_brightness_desc panel_gs_simple_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.max_brightness = 4094,
	.min_brightness = 268,
	.default_brightness = 1024,
	.brt_capability = &panel_gs_simple_brightness_capability,
};

const static struct gs_panel_desc panel_gs_simple_desc = {
	.data_lane_cnt = 4,
	.brightness_desc = &panel_gs_simple_brightness_desc,
	.num_binned_lp = 0,
	.modes = &panel_gs_simple_normal_modes,
	.panel_func = &panel_gs_simple_drm_funcs,
	.gs_panel_func = &panel_gs_simple_panel_funcs,
	.reset_timing_ms = { 0, 0, 0 },
};

static const struct of_device_id dsi_of_match[] = {
	{
		.compatible = "google,panel-gs-simple",
		.data = &panel_gs_simple_desc,
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, dsi_of_match);

static struct mipi_dsi_driver panel_gs_simple_dsi_driver = {
	.driver = {
		.name = "panel-gs-simple",
		.of_match_table = dsi_of_match,
	},
	.probe = panel_gs_simple_probe,
	.remove = gs_dsi_panel_common_remove,
};

module_mipi_dsi_driver(panel_gs_simple_dsi_driver);

MODULE_AUTHOR("Taylor Nelms <tknelms@google.com>");
MODULE_DESCRIPTION("DRM Driver for Simple GS Panel");
MODULE_LICENSE("Dual MIT/GPL");
