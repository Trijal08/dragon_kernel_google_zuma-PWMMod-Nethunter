/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef _GS_DRM_CONNECTOR_H_
#define _GS_DRM_CONNECTOR_H_

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>

#include "gs_drm/gs_display_mode.h"

#define MIN_WIN_BLOCK_WIDTH 8
#define MIN_WIN_BLOCK_HEIGHT 1

#ifndef INVALID_PANEL_ID
#define INVALID_PANEL_ID 0xFFFFFFFF
#endif

enum gs_hbm_mode {
	GS_HBM_OFF = 0,
	GS_HBM_ON_IRC_ON,
	GS_HBM_ON_IRC_OFF,
	GS_HBM_STATE_MAX,
};

enum gs_mipi_sync_mode {
	GS_MIPI_CMD_SYNC_NONE = BIT(0),
	GS_MIPI_CMD_SYNC_REFRESH_RATE = BIT(1),
	GS_MIPI_CMD_SYNC_LHBM = BIT(2),
	GS_MIPI_CMD_SYNC_GHBM = BIT(3),
	GS_MIPI_CMD_SYNC_BL = BIT(4),
	GS_MIPI_CMD_SYNC_OP_RATE = BIT(5),
};

struct gs_drm_connector;

struct gs_drm_connector_properties {
	struct drm_property *max_luminance;
	struct drm_property *max_avg_luminance;
	struct drm_property *min_luminance;
	struct drm_property *hdr_formats;
	struct drm_property *lp_mode;
	struct drm_property *global_hbm_mode;
	struct drm_property *local_hbm_on;
	struct drm_property *dimming_on;
	struct drm_property *brightness_capability;
	struct drm_property *brightness_level;
	struct drm_property *is_partial;
	struct drm_property *panel_idle_support;
	struct drm_property *mipi_sync;
	struct drm_property *panel_orientation;
	struct drm_property *refresh_on_lp;
	struct drm_property *rr_switch_duration;
	struct drm_property *operation_rate;
};

struct gs_display_partial {
	bool enabled;
	unsigned int min_width;
	unsigned int min_height;
};

/**
 * struct gs_drm_connector_state - mutable connector state
 */
struct gs_drm_connector_state {
	/** @base: base connector state */
	struct drm_connector_state base;

	/** @mode: additional mode details */
	struct gs_display_mode gs_mode;

	/** @seamless_possible: this is set if the current mode switch can be done seamlessly */
	bool seamless_possible;

	/** @brightness_level: panel brightness level */
	unsigned int brightness_level;

	/** @global_hbm_mode: global_hbm_mode indicator */
	enum gs_hbm_mode global_hbm_mode;

	/** @local_hbm_on: local_hbm_on indicator */
	bool local_hbm_on;

	/** @dimming_on: dimming on indicator */
	bool dimming_on;

	/** @pending_update_flags: flags for pending update */
	unsigned int pending_update_flags;

	/**
	 * @te_from: Specify ddi interface where TE signals are received by decon.
	 *	     This is required for dsi command mode hw trigger.
	 */
	int te_from;

	/**
	 * @te_gpio: Provies the gpio for panel TE signal.
	 *	     This is required for dsi command mode hw trigger.
	 */
	int te_gpio;

	/*
	 * @tout_gpio: Provides the gpio for panel TOUT (TE2) signal.
	 *	       This is used for checking panel refresh rate.
	 */
	int tout_gpio;

	/**
	 * @partial: Specify whether this panel supports partial update feature.
	 */
	struct gs_display_partial partial;

	/**
	 * @mipi_sync: Indicating if the mipi command in current drm commit should be
	 *	       sent in the same vsync period as the frame.
	 */
	unsigned long mipi_sync;

	/**
	 * @panel_idle_support: Indicating display support panel idle mode. Panel can
	 *			go into idle after some idle period.
	 */
	bool panel_idle_support;

	/**
	 * @blanked_mode: Display should go into forced blanked mode, where power is on but
	 *                nothing is being displayed on screen.
	 */
	bool blanked_mode;

	/** @is_recovering: whether we're doing decon recovery */
	bool is_recovering;

	/** @operation_rate: panel operation rate */
	unsigned int operation_rate;

	/* @update_operation_rate_to_bts: update panel operation rate to BTS requirement */
	bool update_operation_rate_to_bts;

	/** @dsi_hs_clk_mbps: current MIPI DSI HS clock (megabits per second) */
	u32 dsi_hs_clk_mbps;
	/**
	 * @pending_dsi_hs_clk_mbps: pending MIPI DSI HS clock (megabits per second)
	 * A non-zero value means the clock hasn't been set.
	 */
	u32 pending_dsi_hs_clk_mbps;
	/**
	 * @dsi_hs_clk_changed: indicates the MIPI DSI HS clock has been changed
	 * so that the specific settings can be updated accordingly.
	 */
	bool dsi_hs_clk_changed;
};

#define to_gs_connector_state(connector_state) \
	container_of((connector_state), struct gs_drm_connector_state, base)

struct gs_drm_connector_funcs {
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct gs_drm_connector_state *state);
	int (*atomic_set_property)(struct gs_drm_connector *gs_connector,
				   struct gs_drm_connector_state *gs_state,
				   struct drm_property *property, uint64_t val);
	int (*atomic_get_property)(struct gs_drm_connector *gs_connector,
				   const struct gs_drm_connector_state *gs_state,
				   struct drm_property *property, uint64_t *val);
	int (*late_register)(struct gs_drm_connector *gs_connector);
};

struct gs_drm_connector_helper_funcs {
	/*
	 * @atomic_pre_commit: Update connector states before planes commit.
	 *                     Usually for mipi commands and frame content synchronization.
	 */
	void (*atomic_pre_commit)(struct gs_drm_connector *gs_connector,
				  struct gs_drm_connector_state *gs_old_state,
				  struct gs_drm_connector_state *gs_new_state);

	/*
	 * @atomic_commit: Update connector states after planes commit.
	 */
	void (*atomic_commit)(struct gs_drm_connector *gs_connector,
			      struct gs_drm_connector_state *gs_old_state,
			      struct gs_drm_connector_state *gs_new_state);
};

/**
 * struct gs_drm_connector - private data for connector device
 */
struct gs_drm_connector {
	/** @base: base connector data */
	struct drm_connector base;
	/** @properties: drm properties associated with this connector */
	struct gs_drm_connector_properties properties;
	/** @funcs: functions used to interface with this connector */
	const struct gs_drm_connector_funcs *funcs;
	/** @helper_private: private helper functions for drm operations */
	const struct gs_drm_connector_helper_funcs *helper_private;
	/**
	 * @kdev: reference to platform_device's dev
	 * Note that the `base` member also has a struct device pointer
	 */
	struct device *kdev;
	/**
	 * @panel_dsi_device: pointer to the dsi device associated with the
	 * connected panel. Crucial for the `gs_connector_to_panel` function.
	 */
	struct mipi_dsi_device *panel_dsi_device;
	/**
	 * @dsi_host_device: pointer to dsi device hosting this connector
	 * Should be on the other end of the connector's DT graph
	 */
	struct mipi_dsi_host *dsi_host_device;
	/**
	 * @panel_index: which display this connector is for
	 * Read from the device tree; indicates primary or secondary panel
	 */
	int panel_index;
	/**
	 * @panel_id: panel_id read from bootloader. Parsed by the connector,
	 * stored here for use by the panel on init
	 */
	u32 panel_id;
	/**
	 * @needs_commit: connector will always get atomic commit callback for any
	 * pipeline updates for as long as this flag is set
	 */
	bool needs_commit;
	/**
	 * @ignore_op_rate: a flag used to ignore the current OP rate when deciding
	 * BTS behavior in the DPU driver
	 */
	bool ignore_op_rate;
};

#define to_gs_connector(connector) container_of((connector), struct gs_drm_connector, base)

bool is_gs_drm_connector(const struct drm_connector *connector);
#define is_gs_drm_connector_state(conn_state) is_gs_drm_connector(conn_state->connector)

int gs_drm_connector_create_properties(struct drm_connector *connector);
struct gs_drm_connector_properties *
gs_drm_connector_get_properties(struct gs_drm_connector *gs_conector);

static inline struct gs_drm_connector_state *
crtc_get_gs_connector_state(const struct drm_atomic_state *state,
			    const struct drm_crtc_state *crtc_state)
{
	const struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	int i;

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		if (!(crtc_state->connector_mask & drm_connector_mask(conn)))
			continue;

		if (is_gs_drm_connector(conn))
			return to_gs_connector_state(conn_state);
	}

	return NULL;
}

static inline int gs_drm_mode_te_freq(const struct drm_display_mode *mode)
{
	int freq = drm_mode_vrefresh(mode);

	if (mode->flags & DRM_MODE_FLAG_TE_FREQ_X2)
		freq *= 2;
	else if (mode->flags & DRM_MODE_FLAG_TE_FREQ_X4)
		freq *= 4;

	return freq;
}

int gs_connector_bind(struct device *dev, struct device *master, void *data);

/**
 * gs_connector_set_panel_name() - Sets the name and panel_id string for panel
 *
 * @new_name: Name of the panel
 * @len: Length of the panel name
 * @idx: Index of which display this is for (primary or secondary)
 *
 * When possible, we would like to use the panel name and panel_id read and set
 * by the bootloader. On older systems, this involves passing the information to
 * the connector from the DPU. This hook is used to do so.
 *
 * The expected form is "panel_name.panel_id", where the period and panel_id are
 * optional, and the panel_id is a 6-8 character hex string.
 */
void gs_connector_set_panel_name(const char *new_name, size_t len, int idx);

int gs_drm_mode_bts_fps(const struct drm_display_mode *mode);
int gs_bts_fps_to_drm_mode_clock(const struct drm_display_mode *mode, int bts_fps);

#endif /* _GS_DRM_CONNECTOR_H_ */
