/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef _GS_DISPLAY_MODE_H_
#define _GS_DISPLAY_MODE_H_

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
#include <drm/display/drm_dsc.h>
#else
#include <drm/drm_dsc.h>
#endif

/**
 * struct gs_display_dsc - Information about a mode's DSC parameters
 * @enabled: Whether DSC is enabled for this mode
 * @dsc_count: Number of encoders to be used by DPU (TODO:b/283964743)
 * @cfg: Configuration structure describing bulk of algorithm
 * @delay_reg_init_us: Hack for DPU delaying mode switch (TODO:b/283966795)
 *
 * Though most of the description of Display Stream Compression algorithms falls
 * within the bounds of the `struct drm_dsc_config`, this structure captures a
 * few other parameters surrounding the DSC configuration for a display mode
 * that we find useful to adjust (or refer to).
 */
struct gs_display_dsc {
	bool enabled;
	unsigned int dsc_count;

	const struct drm_dsc_config *cfg;

	unsigned int delay_reg_init_us;
};

/**
 * struct gs_display_underrun_param - Parameters to calculate underrun_lp_ref
 */
struct gs_display_underrun_param {
	/** @te_idle_us: te idle (us) to calculate underrun_lp_ref */
	unsigned int te_idle_us;
	/** @te_var: te variation (percentage) to calculate underrun_lp_ref */
	unsigned int te_var;
};

/**
 * struct gs_display_mode - gs display specific info
 */
struct gs_display_mode {
	/** @dsc: DSC parameters for the selected mode */
	struct gs_display_dsc dsc;

	/** @mode_flags: DSI mode flags from drm_mipi_dsi.h */
	unsigned long mode_flags;

	/** @vblank_usec: parameter to calculate bts */
	unsigned int vblank_usec;

	/** @te_usec: command mode: TE pulse time */
	unsigned int te_usec;

	/** @bpc: display bits per component */
	unsigned int bpc;

	/** @underrun_param: parameters to calculate underrun_lp_ref when hs_clock changes */
	const struct gs_display_underrun_param *underrun_param;

	/** @is_lp_mode: boolean, if true it means this mode is a Low Power mode */
	bool is_lp_mode;

	/**
	 * @sw_trigger:
	 *
	 * Force frame transfer to be triggered by sw instead of based on TE.
	 * This is only applicable for DSI command mode, SW trigger is the
	 * default for Video mode.
	 */
	bool sw_trigger;
};

#endif // _GS_DISPLAY_MODE_H_
