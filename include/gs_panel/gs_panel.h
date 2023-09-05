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
#include <linux/version.h>
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
 * @GIDLE_MODE_UNSUPPORTED: No idle mode is supported in this mode
 * @GIDLE_MODE_ON_INACTIVITY: In this mode the panel can go into idle automatically
 *                           after last frame update
 * @GIDLE_MODE_ON_SELF_REFRESH: Manually go into lower idle mode when display enters
 *                             self refresh state
 */
enum gs_panel_idle_mode {
	GIDLE_MODE_UNSUPPORTED,
	GIDLE_MODE_ON_INACTIVITY,
	GIDLE_MODE_ON_SELF_REFRESH,
};

enum gs_acl_mode {
	ACL_OFF = 0,
	ACL_NORMAL,
	ACL_ENHANCED,
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

/**
 * enum mode_progress_type - the type while mode switch is in progress
 * @MODE_DONE: mode switch is done
 * @MODE_RES_IN_PROGRESS: mode switch is in progress, only resolution is changed
 * @MODE_RR_IN_PROGRESS: mode switch is in progress, only refresh rate is changed
 * @MODE_RES_AND_RR_IN_PROGRESS: mode switch is in progress, both resolution and
 *                               refresh rate are changed
 */
enum mode_progress_type {
	MODE_DONE = 0,
	MODE_RES_IN_PROGRESS,
	MODE_RR_IN_PROGRESS,
	MODE_RES_AND_RR_IN_PROGRESS,
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
	 * @set_lp_mode:
	 *
	 * This callback is used to handle command sequences to enter low power modes.
	 *
	 * mode: LP mode to which to switch
	 *
	 * TODO(b/279521692): implementation
	 */
	void (*set_lp_mode)(struct gs_panel *gs_panel, const struct gs_panel_mode *mode);

	/**
	 * @set_nolp_mode:
	 *
	 * This callback is used to handle command sequences to exit from low power
	 * modes.
	 *
	 * mode: mode to which to switch
	 *
	 * TODO(b/279521692): implementation
	 */
	void (*set_nolp_mode)(struct gs_panel *gs_panel, const struct gs_panel_mode *mode);

	/**
	 * @set_hbm_mode:
	 *
	 * This callback is used to implement panel specific logic for high brightness
	 * mode enablement. If this is not defined, it means that panel does not
	 * support HBM
	 *
	 * TODO(b/279521612): implementation
	 */
	void (*set_hbm_mode)(struct gs_panel *gs_panel, enum gs_hbm_mode mode);

	/**
	 * @set_dimming:
	 *
	 * This callback is used to implement panel specific logic for dimming mode
	 * enablement. If this is not defined, it means that panel does not support
	 * dimming.
	 *
	 * dimming_on: true for dimming enabled, false for dimming disabled
	 *
	 * TODO(b/279520614): implementation
	 */
	void (*set_dimming)(struct gs_panel *gs_panel, bool dimming_on);

	/**
	 * @set_local_hbm_mode:
	 *
	 * This callback is used to implement panel specific logic for local high
	 * brightness mode enablement. If this is not defined, it means that panel
	 * does not support local HBM
	 *
	 * TODO(b/279521693): implementation
	 */
	void (*set_local_hbm_mode)(struct gs_panel *gs_panel,
				   bool local_hbm_en);

	/**
	 * @mode_set:
	 *
	 * This callback is used to perform driver specific logic for mode_set.
	 * This could be called while display is on or off, should check internal
	 * state to perform appropriate mode set configuration depending on this state.
	 *
	 * TODO(b/279520499): implementation
	 */
	void (*mode_set)(struct gs_panel *gs_panel, const struct gs_panel_mode *mode);

	/**
	 * @update_te2:
	 *
	 * This callback is used to update the TE2 signal via DCS commands.
	 * This should be called when the display state is changed between
	 * normal and LP modes, or the refresh rate and LP brightness are
	 * changed.
	 *
	 * TODO(b/279521893): implementation
	 */
	void (*update_te2)(struct gs_panel *gs_panel);

	/**
	 * @atomic_check
	 *
	 * This optional callback happens in atomic check phase, it gives a chance to panel driver
	 * to check and/or adjust atomic state ahead of atomic commit.
	 *
	 * Should return 0 on success (no problems with atomic commit) otherwise negative errno
	 *
	 * TODO(b/279520499): implementation
	 */
	int (*atomic_check)(struct gs_panel *gs_panel, struct drm_atomic_state *state);

	/**
	 * @commit_done
	 *
	 * Called after atomic commit flush has completed but transfer may not have started yet
	 *
	 * TODO(b/279520499): implementation
	 */
	void (*commit_done)(struct gs_panel *gs_panel);

	/**
	 * @is_mode_seamless:
	 *
	 * This callback is used to check if a switch to a particular mode can be done
	 * seamlessly without full mode set given the current hardware configuration
	 *
	 * TODO(b/279520499): implementation
	 */
	bool (*is_mode_seamless)(const struct gs_panel *gs_panel,
				 const struct gs_panel_mode *pmode);

	/**
	 * @set_self_refresh
	 *
	 * Called when display self refresh state has changed. While in self refresh state, the
	 * panel can optimize for power assuming that there are no pending updates.
	 *
	 * Returns true if underlying mode was updated to reflect new self refresh state,
	 * otherwise returns false if no action was taken.
	 *
	 * TODO(b/279519827): implementation
	 */
	bool (*set_self_refresh)(struct gs_panel *gs_panel, bool enable);

	/**
	 * @set_op_hz
	 *
	 * set display panel working on specified operation rate.
	 *
	 * Returns 0 if successfully setting operation rate.
	 *
	 * TODO(b/279521713): implementation
	 */
	int (*set_op_hz)(struct gs_panel *gs_panel, unsigned int hz);

	/**
	 * @get_panel_rev
	 *
	 * This callback is used to get panel HW revision from panel_extinfo.
	 * It is expected to fill in the `panel_rev` member of the `gs_panel`
	 *
	 * @id: contents of `extinfo`, read as a binary value
	 */
	void (*get_panel_rev)(struct gs_panel *gs_panel, u32 id);

	/**
	 * @read_id:
	 *
	 * This callback is used to read the panel's id. The id is unique for
	 * each panel.
	 */
	int (*read_id)(struct gs_panel *gs_panel);

	/**
	 * @set_acl_mode:
	 *
	 * This callback is used to implement panel specific logic for acl mode
	 * enablement. If this is not defined, it means that panel does not
	 * support acl.
	 *
	 * TODO(tknelms): implement default version
	 */
	void (*set_acl_mode)(struct gs_panel *gs_panel, enum gs_acl_mode mode);

	/**
	 * @panel_config:
	 *
	 * This callback is used to do one time panel configuration before the
	 * common driver initialization.
	 */
	int (*panel_config)(struct gs_panel *gs_panel);

	/**
	 * @panel_init:
	 *
	 * This callback is used to do one time initialization for any panel
	 * specific functions.
	 */
	void (*panel_init)(struct gs_panel *gs_panel);

	/**
	 * @get_te_usec
	 *
	 * This callback is used to get current TE pulse time.
	 *
	 * TODO(b/279521893): implementation
	 */
	unsigned int (*get_te_usec)(struct gs_panel *gs_panel, const struct gs_panel_mode *pmode);

	/**
	 * @run_normal_mode_work
	 *
	 * This callback is used to run the periodic work for each panel in
	 * normal mode.
	 */
	void (*run_normal_mode_work)(struct gs_panel *gs_panel);
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

struct gs_brightness_configuration {
	const u32 panel_rev;
	const u32 default_brightness;
	const struct brightness_capability brt_capability;
};

/**
 * gs_panel_update_brightness_desc - Update brightness_desc based on panel rev
 * @desc: Desc object to update
 * @configs: Array of possible brightness configurations
 * @num_configs: How many configs are in the array
 * @panel_rev: This panel's revision
 *
 * Some of our panels have different target brightness configuration based on
 * their panel revision. This ends up stored in a
 * `struct gs_brightness_configuration` array. This function finds the matching
 * configuration based on the given panel revision and updates the
 * `struct gs_panel_brightness_desc` to reflect the correct brightness settings.
 *
 * Returns: 0 on success, negative value on error
 */
int gs_panel_update_brightness_desc(struct gs_panel_brightness_desc *desc,
				    const struct gs_brightness_configuration *configs,
				    u32 num_configs, u32 panel_rev);

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
	u32 rr_switch_duration;
	bool dbv_extra_frame;
	bool is_partial;
	bool is_idle_supported;
	const u32 *bl_range;
	u32 bl_num_ranges;
	const struct gs_panel_mode_array *modes;
	const struct gs_panel_mode_array *lp_modes;
	const struct gs_dsi_cmdset *off_cmdset;
	const struct gs_dsi_cmdset *lp_cmdset;
	const struct gs_binned_lp *binned_lp;
	const size_t num_binned_lp;
	const struct drm_panel_funcs *panel_func;
	const struct gs_panel_funcs *gs_panel_func;
	const u32 reset_timing_ms[PANEL_RESET_TIMING_COUNT];
	const struct gs_panel_reg_ctrl_desc *reg_ctrl_desc;
};

/* PRIV DATA */

/**
 * struct gs_panel_debugfs_entries - references to debugfs folder entries
 * @panel: parent folder for panel (ex. "DSI-1/panel")
 * @reg: folder for direct dsi operations (ex. "DSI-1/panel/reg")
 * @cmdset: folder for cmdset entries (ex. "DSI-1/panel/cmdsets")
 *
 * This stores references to the main "folder"-level debugfs entries for the
 * panel. This allows some degree of extension by specific drivers, for example
 * to add an additional cmdset to the "cmdset" debugfs folder.
 */
struct gs_panel_debugfs_entries {
	struct dentry *panel;
	struct dentry *reg;
	struct dentry *cmdset;
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

	/* Automatic Current Limiting(ACL) */
	enum gs_acl_mode acl_mode;

	/* current type of mode switch */
	enum mode_progress_type mode_in_progress;

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

/**
 * is_panel_initialized - indicates whether the display has been initialized at least once
 * @ctx: panel struct
 *
 * Indicates whether thepanel has been initialized at least once. Certain data such as panel
 * revision is only accurate after display initialization.
 */
static inline bool gs_is_panel_initialized(const struct gs_panel *ctx)
{
	return ctx->panel_state != GPANEL_STATE_UNINITIALIZED &&
	       ctx->panel_state != GPANEL_STATE_HANDOFF &&
	       ctx->panel_state != GPANEL_STATE_HANDOFF_MODESET;
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

#define gs_panel_has_func(ctx, func) \
		((ctx) && ((ctx)->desc) && ((ctx)->desc->gs_panel_func)\
		 && ((ctx)->desc->gs_panel_func->func))


u16 gs_panel_get_brightness(struct gs_panel *panel);

/** Command Functions with specific purposes **/

static inline void gs_panel_send_cmdset_flags(struct gs_panel *ctx,
					      const struct gs_dsi_cmdset *cmdset, u32 flags)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	gs_dsi_send_cmdset_flags(dsi, cmdset, ctx->panel_rev, flags);
}
static inline void gs_panel_send_cmdset(struct gs_panel *ctx, const struct gs_dsi_cmdset *cmdset)
{
	gs_panel_send_cmdset_flags(ctx, cmdset, 0);
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
 * gs_dsi_panel_common_remove - Removes dsi panel
 * @dsi: dsi device pointer for panel
 *
 * Return: 0 on success, negative value for error
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
void gs_dsi_panel_common_remove(struct mipi_dsi_device *dsi);
#else
int gs_dsi_panel_common_remove(struct mipi_dsi_device *dsi);
#endif

/**
 * gs_panel_debugfs_create_cmdset - Creates a cmdset debugfs entry
 * @parent: dentry for debugfs parent folder. This will often be
 *          `gs_panel->debugfs_entries.cmdset`
 * @cmdset: cmdset to be read out from resulting debugfs entry
 * @name: name for resulting debugfs entry
 *
 * Creates a debugfs entry for the given cmdset, which will allow its contents
 * to be read for debugging purposes.
 */
void gs_panel_debugfs_create_cmdset(struct dentry *parent, const struct gs_dsi_cmdset *cmdset,
				    const char *name);

#define GS_VREFRESH_TO_PERIOD_USEC(rate) DIV_ROUND_UP(USEC_PER_SEC, (rate) ? (rate) : 60)

/**
 * gs_panel_wait_for_vblank - wait for next vblank provided by attached drm_crtc
 * @ctx: handle for gs_panel that is waiting
 *
 * Return: 0 on success, negative value for error
 */
int gs_panel_wait_for_vblank(struct gs_panel *ctx);

/**
 * gs_panel_wait_for_vsync_done - wait for the vsync signal to be done
 * @ctx: handle for gs_panel that is waiting
 * @te_us: length of te period, in us
 * @period_us: length of a clock period (TODO: verify)
 */
void gs_panel_wait_for_vsync_done(struct gs_panel *ctx, u32 te_us, u32 period_us);

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
#define GS_HBM_FLAG_OP_RATE_UPDATE BIT(4)

#define GS_IS_HBM_ON(mode) ((mode) >= GS_HBM_ON_IRC_ON && (mode) < GS_HBM_STATE_MAX)
#define GS_IS_HBM_ON_IRC_OFF(mode) (((mode) == GS_HBM_ON_IRC_OFF))

#endif // DISPLAY_COMMON_PANEL_PANEL_GS_H_
