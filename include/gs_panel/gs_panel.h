/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef DISPLAY_COMMON_GS_PANEL_H_
#define DISPLAY_COMMON_GS_PANEL_H_

#include <linux/printk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/backlight.h>
#include <drm/drm_bridge.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_property.h>
#include <drm/drm_mipi_dsi.h>

#include "gs_drm/gs_drm_connector.h"
#include "gs_panel/dcs_helper.h"

struct attribute_range {
	__u32 min;
	__u32 max;
};

/**
 * struct brightness_attribute - brightness attribute data
 *
 * @nits: value represents brightness nits range
 * @level: value represents panel brightness level range
 * @percentage: value must be between 0 and 100 and be non-decreasing.
 *              This percentage must comply with display configuration
 *              file.
 *
 * A brightness_attribute represents brightness attribute data.
 */
struct brightness_attribute {
	struct attribute_range nits;
	struct attribute_range level;
	struct attribute_range percentage;
};

/**
 * struct brightness_capability - brightness capability query by user-space
 *
 * @normal: normal rerepresents the normal brightness attribute.
 * @hbm: hbm represents the hbm brightness attribute
 *
 * A brightness_capability represents normal/hbm brightness attribute. It is
 * used to query connector property.
 */
struct brightness_capability {
	struct brightness_attribute normal;
	struct brightness_attribute hbm;
};

/**
 * enum gs_panel_state - panel operating state
 * TODO: reword, rethink, refactor (code style for enums relevant)
 * @GPANEL_STATE_UNINITIALIZED: Panel has never been initialized, and panel OTP info such as
 *                             panel serial and revision has not been read yet
 * @GPANEL_STATE_HANDOFF: Panel looked active when driver was loaded. The panel is uninitialized
 *                       in this state and will switch to PANEL_STATE_ON once it gets initialized
 * @GPANEL_STATE_HANDOFF_MODESET: Similar to HANDOFF state, in this case a modeset was called with
                                 unpreferred mode, so display must be blanked before enabling.
 * @GPANEL_STATE_OFF: Panel is fully disabled and powered off
 * @GPANEL_STATE_NORMAL: Panel is ON in Normal operating mode
 * @GPANEL_STATE_LP: Panel is ON in Low Power mode
 * @GPANEL_STATE_MODESET: Going through modeset, where panel gets disable/enable calls with new mode
 * @GPANEL_STATE_BLANK: Panel is ON but no contents are shown on display
 */
enum gs_panel_state {
	GPANEL_STATE_UNINITIALIZED = 0,
	GPANEL_STATE_HANDOFF,
	GPANEL_STATE_HANDOFF_MODESET,
	GPANEL_STATE_OFF,
	GPANEL_STATE_NORMAL,
	GPANEL_STATE_LP,
	GPANEL_STATE_MODESET,
	GPANEL_STATE_BLANK,
};

/**
 * enum gs_panel_idle_mode - type of idle mode supported per mode
 * TODO: reword, rethink, refactor (code style for enums relevant)
 * @kIDLE_MODE_UNSUPPORTED: No idle mode is supported in this mode
 * @kIDLE_MODE_ON_INACTIVITY: In this mode the panel can go into idle automatically
 *                           after last frame update
 * @kIDLE_MODE_ON_SELF_REFRESH: Manually go into lower idle mode when display enters
 *                             self refresh state
 */
enum gs_panel_idle_mode {
	kIDLE_MODE_UNSUPPORTED,
	kIDLE_MODE_ON_INACTIVITY,
	kIDLE_MODE_ON_SELF_REFRESH,
};

/**
 * enum gs_panel_te2_opt - option of TE2 frequency
 * TODO: reword, rethink, refactor (code style for enums relevant)
 * @GTE2_OPT_CHANGEABLE: TE2 frequency follows display refresh rate
 * @GTE2_OPT_FIXED: TE2 frequency is fixed at 120Hz. Only supported on specific panels
 */
enum gs_panel_te2_opt {
	GTE2_OPT_CHANGEABLE,
	GTE2_OPT_FIXED,
};

enum gs_cabc_mode {
	GCABC_OFF = 0,
	GCABC_UI_MODE,
	GCABC_STILL_MODE,
	GCABC_MOVIE_MODE,
};

enum gs_local_hbm_enable_state {
	GLOCAL_HBM_DISABLED = 0,
	GLOCAL_HBM_ENABLED,
	GLOCAL_HBM_ENABLING,
};

struct gs_panel;

/**
 * struct gs_panel_mode - panel mode info
 */
struct gs_panel_mode {
	/** @mode: drm display mode info */
	struct drm_display_mode mode;
	/** @gs_mode: gs driver specific mode info */
	struct gs_display_mode gs_mode;
	/** @priv_data: per mode panel driver private data TODO: eliminate */
	const void *priv_data;
	/** @te2_timing: TE2 signal timing */
	struct gs_panel_te2_timing te2_timing;
	/**
	 * @idle_mode:
	 *
	 * Indicates whether going into lower refresh rate is allowed while in this mode, and what
	 * type of idle mode is supported, for more info refer to enum gs_panel_idle_mode.
	 */
	enum gs_panel_idle_mode idle_mode;
};

/* PANEL FUNCS */

struct gs_panel_funcs {
	/**
	 * @set_brightness:
	 *
	 * This callback is used to implement driver specific logic for brightness
	 * configuration. Otherwise defaults to sending brightness commands through
	 * dcs command update
	 */
	int (*set_brightness)(struct gs_panel *gs_panel, u16 br);

	/**
	 * @set_nolp_mode:
	 *
	 * This callback is used to handle command sequences to exit from low power
	 * modes.
	 */
	void (*set_nolp_mode)(struct gs_panel *gs_panel, const struct gs_panel_mode *mode);

	/**
	 * @set_hbm_mode:
	 *
	 * This callback is used to implement panel specific logic for high brightness
	 * mode enablement. If this is not defined, it means that panel does not
	 * support HBM
	 */
	void (*set_hbm_mode)(struct gs_panel *gs_panel, enum gs_hbm_mode mode);

	/**
	 * @mode_set:
	 *
	 * This callback is used to perform driver specific logic for mode_set.
	 * This could be called while display is on or off, should check internal
	 * state to perform appropriate mode set configuration depending on this state.
	 */
	void (*mode_set)(struct gs_panel *gs_panel, const struct gs_panel_mode *mode);

	/**
	 * @panel_init:
	 *
	 * This callback is used to do one time initialization for any panel
	 * specific functions.
	 */
	void (*panel_init)(struct gs_panel *gs_panel);
};

/* PANEL DESC */

/**
 * struct gs_panel_brightness_desc
 * TODO: document
 */
struct gs_panel_brightness_desc {
	u32 max_luminance;
	u32 max_avg_luminance;
	u32 min_luminance;
	u32 max_brightness;
	u32 min_brightness;
	u32 default_brightness;
	const struct brightness_capability *brt_capability;
};

/**
 * struct gs_panel_lhbm_desc
 * TODO: document
 */
struct gs_panel_lhbm_desc {
	bool no_lhbm_rr_constraints;
	const u32 post_cmd_delay_frames;
	const u32 effective_delay_frames;
};

/**
 * gs_panel_mode_array - container for display modes
 * @num_modes: number of modes in array
 * @modes: display modes
 */
struct gs_panel_mode_array {
	size_t num_modes;
	const struct gs_panel_mode modes[];
};

#define BL_STATE_STANDBY BL_CORE_FBBLANK
#define BL_STATE_LP BIT(30) /* backlight is in LP mode */

#define MAX_TE2_TYPE 20
#define PANEL_ID_MAX 40
#define PANEL_EXTINFO_MAX 16
#define LOCAL_HBM_MAX_TIMEOUT_MS 3000 /* 3000 ms */
#define LOCAL_HBM_GAMMA_CMD_SIZE_MAX 16

enum panel_reset_timing {
	PANEL_RESET_TIMING_HIGH = 0,
	PANEL_RESET_TIMING_LOW,
	PANEL_RESET_TIMING_INIT,
	PANEL_RESET_TIMING_COUNT
};

enum panel_reg_id {
	PANEL_REG_ID_INVALID = 0,
	PANEL_REG_ID_VCI,
	PANEL_REG_ID_VDDI,
	PANEL_REG_ID_VDDD,
	PANEL_REG_ID_VDDR_EN,
	PANEL_REG_ID_VDDR,
	PANEL_REG_ID_MAX,
};

struct panel_reg_ctrl {
	enum panel_reg_id id;
	u32 post_delay_ms;
};
#define IS_VALID_PANEL_REG_ID(id) (((id) > PANEL_REG_ID_INVALID) && ((id) < PANEL_REG_ID_MAX))
#define PANEL_REG_COUNT (PANEL_REG_ID_MAX - 1)

struct gs_panel_reg_ctrl_desc {
	const struct panel_reg_ctrl reg_ctrl_enable[PANEL_REG_COUNT];
	const struct panel_reg_ctrl reg_ctrl_post_enable[PANEL_REG_COUNT];
	const struct panel_reg_ctrl reg_ctrl_pre_disable[PANEL_REG_COUNT];
	const struct panel_reg_ctrl reg_ctrl_disable[PANEL_REG_COUNT];
};

struct gs_panel_desc {
	u8 panel_id_reg;
	u32 data_lane_cnt;
	u32 hdr_formats;
	const struct gs_panel_brightness_desc *brightness_desc;
	const struct gs_panel_lhbm_desc *lhbm_desc;
	const unsigned int delay_dsc_reg_init_us;
	u32 vrr_switch_duration;
	bool dbv_extra_frame;
	bool is_partial;
	bool is_idle_supported;
	const u32 *bl_range;
	u32 bl_num_ranges;
	const struct gs_panel_mode_array *modes;
	const struct gs_panel_mode_array *lp_modes;
	const struct gs_dsi_cmd_set *off_cmd_set;
	const struct gs_dsi_cmd_set *lp_cmd_set;
	const struct gs_binned_lp *binned_lp;
	const size_t num_binned_lp;
	const struct drm_panel_funcs *panel_func;
	const struct gs_panel_funcs *gs_panel_func;
	const u32 reset_timing_ms[PANEL_RESET_TIMING_COUNT];
	const struct gs_panel_reg_ctrl_desc *reg_ctrl_desc;
};

/* PRIV DATA */

/**
 * struct gs_panel_debugfs_entries
 */
struct gs_panel_debugfs_entries {
	struct dentry *debugfs_entry;
	struct dentry *debugfs_cmdset_entry;
};

/**
 * struct gs_panel_gpio - references to gpio descriptors associated with panel
 */
struct gs_panel_gpio {
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};

/**
 * struct gs_panel_regulator - state of the power regulator
 * TODO: document
 */
struct gs_panel_regulator {
	struct regulator *vci;
	struct regulator *vddi;
	struct regulator *vddd;
	struct regulator *vddr_en;
	struct regulator *vddr;
	u32 vddd_normal_uV;
	u32 vddd_lp_uV;
};

struct gs_panel_idle_data {
	bool panel_idle_enabled;
	bool panel_need_handle_idle_exit;
	bool self_refresh_active;
	u32 panel_idle_vrefresh;
	u32 idle_delay_ms;
};

/**
 * struct gs_te2_mode_data - stores te2-related mode data
 */
struct gs_te2_mode_data {
	/* @mode: normal or LP mode data */
	const struct drm_display_mode *mode;
	/* @binned_lp: LP mode data */
	const struct gs_binned_lp *binned_lp;
	/* @timing: normal or LP mode timing */
	struct gs_panel_te2_timing timing;
};

/**
 * struct gs_te2_data - stores te2-related data
 * TODO: refactor?
 */
struct gs_te2_data {
	struct gs_te2_mode_data mode_data[MAX_TE2_TYPE];
	enum gs_panel_te2_opt option;
	u32 last_rr;
	int last_rr_te_gpio_value;
	u64 last_rr_te_counter;
	u32 last_rr_te_usec;
};

/**
 * struct gs_panel_timestamps - keeps track of timestamps
 * for particular operations the panel has performed
 */
struct gs_panel_timestamps {
	ktime_t last_commit_ts;
	ktime_t last_mode_set_ts;
	ktime_t last_self_refresh_active_ts;
	ktime_t last_panel_idle_set_ts;
	ktime_t last_rr_switch_ts;
};

/**
 * struct gs_panel - data associated with panel driver operation
 * TODO: better documentation
 */
struct gs_panel {
	struct device *dev;
	struct drm_panel base;
	struct gs_panel_debugfs_entries debugfs_entries;
	struct gs_panel_gpio gpio;
	struct gs_panel_regulator regulator;
	struct gs_drm_connector *gs_connector;
	struct drm_bridge bridge;
	const struct gs_panel_desc *desc;
	const struct gs_panel_mode *current_mode;
	bool initialized;
	enum gs_panel_state panel_state;
	/* If true, panel won't be powered off */
	bool force_power_on;
	struct gs_panel_idle_data idle_data;
	u32 op_hz;
	u32 osc2_clk_khz;
	int min_vrefresh;
	int peak_vrefresh;
	bool dimming_on;
	bool bl_ctrl_dcs;
	enum gs_cabc_mode cabc_mode;
	struct backlight_device *bl;
	struct mutex mode_lock;
	struct mutex bl_state_lock;
	struct mutex lp_state_lock;
	struct drm_property_blob *lp_mode_blob;
	char panel_id[PANEL_ID_MAX];
	char panel_extinfo[PANEL_EXTINFO_MAX];
	u32 panel_rev;
	enum drm_panel_orientation orientation;
	struct gs_te2_data te2;
	struct device_node *touch_dev;
	struct gs_panel_timestamps timestamps;
	struct delayed_work idle_work;

	/* GHBM (maybe reevaluate */
	enum gs_hbm_mode hbm_mode;
	/* HBM struct */
	struct {
		struct gs_local_hbm {
			bool gamma_para_ready;
			u8 gamma_cmd[LOCAL_HBM_GAMMA_CMD_SIZE_MAX];
			enum gs_local_hbm_enable_state requested_state;
			union {
				enum gs_local_hbm_enable_state effective_state;
				enum gs_local_hbm_enable_state enabled;
			};
			/* max local hbm on period in ms */
			u32 max_timeout_ms;
			/* work used to turn off local hbm if reach max_timeout */
			struct delayed_work timeout_work;
			struct kthread_worker worker;
			struct task_struct *thread;
			struct kthread_work post_work;
			ktime_t en_cmd_ts;
			ktime_t next_vblank_ts;
			u32 frame_index;
			ktime_t last_vblank_ts;
			bool post_work_disabled;
		} local_hbm;

		struct workqueue_struct *wq;
	} hbm;
};

/* FUNCTIONS */

/* accessors */

static inline bool gs_is_panel_active(const struct gs_panel *ctx)
{
	switch (ctx->panel_state) {
	case GPANEL_STATE_LP:
	case GPANEL_STATE_NORMAL:
		return true;
	case GPANEL_STATE_UNINITIALIZED:
	case GPANEL_STATE_HANDOFF:
	case GPANEL_STATE_HANDOFF_MODESET:
	case GPANEL_STATE_OFF:
	case GPANEL_STATE_MODESET:
	case GPANEL_STATE_BLANK:
	default:
		return false;
	}
}

static inline bool gs_is_panel_enabled(const struct gs_panel *ctx)
{
	switch (ctx->panel_state) {
	case GPANEL_STATE_OFF:
	case GPANEL_STATE_UNINITIALIZED:
		return false;
	default:
		return true;
	}
}

static inline bool gs_is_local_hbm_post_enabling_supported(struct gs_panel *ctx)
{
	return false;
	/*TODO(tknelms): implement?
	return (!ctx->hbm.local_hbm.post_work_disabled && ctx->desc
		&& ctx->desc->lhbm_desc
		&& (ctx->desc->lhbm_desc->effective_delay_frames
		 || (ctx->desc->lhbm_desc->post_cmd_delay_frames
		  && ctx->desc->gs_panel_func->base->set_local_hbm_mode_post)));
	*/
}

static inline bool gs_is_local_hbm_disabled(struct gs_panel *ctx)
{
	return (ctx->hbm.local_hbm.effective_state == GLOCAL_HBM_DISABLED);
}

/**
 * gs_panel_get_mode - Finds gs_panel_mode matching drm_display_mode for panel
 * @ctx: Pointer to panel private data structure
 * @mode: drm_display_mode to search for in possible panel modes
 *
 * This function searches the possible display modes of the panel for one that
 * matches the given `mode` argument (as per `drm_mode_equal`)
 *
 * Return: Matching gs_panel_mode for this panel, or NULL if not found
 */
const struct gs_panel_mode *gs_panel_get_mode(struct gs_panel *ctx,
					      const struct drm_display_mode *mode);

static inline bool gs_ctx_has_set_backlight_func(const struct gs_panel *ctx)
{
	if (!ctx || !ctx->desc || !ctx->desc->gs_panel_func)
		return false;
	if (!ctx->desc->gs_panel_func->set_brightness)
		return false;
	else
		return true;
}

u16 gs_panel_get_brightness(struct gs_panel *panel);

/** Command Functions with specific purposes **/

static inline void gs_panel_send_cmd_set_flags(struct gs_panel *ctx,
					       const struct gs_dsi_cmd_set *cmd_set, u32 flags)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	gs_dsi_send_cmd_set_flags(dsi, cmd_set, ctx->panel_rev, flags);
}
static inline void gs_panel_send_cmd_set(struct gs_panel *ctx, const struct gs_dsi_cmd_set *cmd_set)
{
	gs_panel_send_cmd_set_flags(ctx, cmd_set, 0);
}
static inline int gs_dcs_set_brightness(struct gs_panel *ctx, u16 br)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	return mipi_dsi_dcs_set_display_brightness(dsi, br);
}

/* Driver-facing functions (high-level) */

void gs_panel_reset_helper(struct gs_panel *ctx);
int gs_panel_set_power_helper(struct gs_panel *ctx, bool on);
/**
 * gs_dsi_panel_common_init - Probe-level initialization for gs_panel
 * @dsi: dsi device pointer for panel
 * @ctx: Preallocated memory for gs_panel object
 *
 * This function performs a wide range of initialization functions at probe time
 * for gs_panel objects, including creating mutexes, parsing the device tree,
 * registering the device data, creating sysfs files, etc.
 *
 * Return: Probe results; 0 for success, negative value for error
 */
int gs_dsi_panel_common_init(struct mipi_dsi_device *dsi, struct gs_panel *ctx);
/**
 * gs_dsi_panel_common_probe() - Wrapper for gs_dsi_panel_common_init with malloc
 * @dsi: dsi device pointer for panel
 *
 * For drivers that don't need additional working state data for their panels,
 * this function calls the `kzalloc` function to allocate a `gs_panel` before
 * sending that to the `gs_dsi_panel_common_init` function.
 *
 * It is designed to plug directly into the `probe` function of the
 * `struct mipi_dsi_driver` data structure.
 *
 * Return: Probe results; 0 for success, negative value for error
 */
int gs_dsi_panel_common_probe(struct mipi_dsi_device *dsi);

/**
 * gs_panel_msleep - sleeps for a given number of ms
 * @delay_ms: Length of time to sleep
 *
 * This is an implementation of the normal `sleep` functions with a tie-in to
 * the panel driver's tracing utilities
 */
void gs_panel_msleep(u32 delay_ms);

static inline void backlight_state_changed(struct backlight_device *bl)
{
	sysfs_notify(&bl->dev.kobj, NULL, "state");
}

static inline void te2_state_changed(struct backlight_device *bl)
{
	sysfs_notify(&bl->dev.kobj, NULL, "te2_state");
}

/* HBM */

#define GS_HBM_FLAG_GHBM_UPDATE BIT(0)
#define GS_HBM_FLAG_BL_UPDATE BIT(1)
#define GS_HBM_FLAG_LHBM_UPDATE BIT(2)
#define GS_HBM_FLAG_DIMMING_UPDATE BIT(3)

#define GS_IS_HBM_ON(mode) ((mode) >= GS_HBM_ON_IRC_ON && (mode) < GS_HBM_STATE_MAX)

#endif // DISPLAY_COMMON_PANEL_PANEL_GS_H_
