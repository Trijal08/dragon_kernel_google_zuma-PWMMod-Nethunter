/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef _GS_DCS_HELPER_H_
#define _GS_DCS_HELPER_H_

#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0))
#include <drm/display/drm_dsc.h>
#else
#include <drm/drm_dsc.h>
#endif
#include <drm/drm_mipi_dsi.h>

/** Private DSI msg flags **/

/* Stack all commands until lastcommand bit and trigger all in one go */
#define GS_DSI_MSG_QUEUE BIT(15)

/* packetgo feature to batch msgs can wait for vblank, use this flag to ignore */
#define GS_DSI_MSG_IGNORE_VBLANK BIT(14)
/* Mark the start of mipi commands transaction. Following commands should not be
 * sent to panel until see a GS_DSI_MSG_FORCE_FLUSH flag
 */
#define GS_DSI_MSG_FORCE_BATCH BIT(13)
/** Mark the end of mipi commands transaction */
#define GS_DSI_MSG_FORCE_FLUSH BIT(12)

/** Panel Command Flags **/

/* indicates that all commands in this cmd set should be batched together */
#define GS_PANEL_CMD_SET_BATCH BIT(0)
/*
 * indicates that all commands in this cmd set should be queued, a follow up
 * command should take care of triggering transfer of batch
 */
#define GS_PANEL_CMD_SET_QUEUE BIT(1)

/* packetgo feature to batch msgs can wait for vblank, use this flag to ignore explicitly */
#define GS_PANEL_CMD_SET_IGNORE_VBLANK BIT(2)

/* Panel Rev bits */
#define PANEL_REV_PROTO1        BIT(0)
#define PANEL_REV_PROTO1_1      BIT(1)
#define PANEL_REV_PROTO1_2      BIT(2)
#define PANEL_REV_PROTO2        BIT(3)
#define PANEL_REV_EVT1          BIT(4)
#define PANEL_REV_EVT1_0_2      BIT(5)
#define PANEL_REV_EVT1_1        BIT(6)
#define PANEL_REV_EVT1_2        BIT(7)
#define PANEL_REV_EVT2          BIT(8)
#define PANEL_REV_DVT1          BIT(9)
#define PANEL_REV_DVT1_1        BIT(10)
#define PANEL_REV_PVT           BIT(11)
#define PANEL_REV_LATEST        BIT(31)
#define PANEL_REV_ALL           (~0)
#define PANEL_REV_GE(rev)       (~((rev) - 1))
#define PANEL_REV_LT(rev)       ((rev) - 1)
#define PANEL_REV_ALL_BUT(rev)  (PANEL_REV_ALL & ~(rev))

/** Command Set data structures **/

/**
 * struct gs_dsi_cmd - information for a dsi command.
 * @cmd_len:  Length of a dsi command.
 * @cmd:      Pointer to a dsi command.
 * @delay_ms: Delay time after executing this dsi command.
 * @panel_rev:Send the command only when the panel revision is matched.
 */
struct gs_dsi_cmd {
	u32 cmd_len;
	const u8 *cmd;
	u32 delay_ms;
	u32 panel_rev;
	u8 type;
};

/**
 * struct gs_dsi_cmd_set - a dsi command sequence.
 * @num_cmd:  Number of dsi commands in this sequence.
 * @cmds:     Pointer to a dsi command sequence.
 */
struct gs_dsi_cmd_set {
	const u32 num_cmd;
	const struct gs_dsi_cmd *cmds;
};

/**
 * GS_DSI_CMD_REV - construct a struct gs_dsi_cmd from inline data
 * @cmd: The command to pack into the struct
 * @delay: The delay to attach to sending the command
 * @rev: The panel revision this applies to, if any
 *
 * Return: struct gs_dsi_cmd holding data necessary to send the command to the
 * panel.
 */
#define GS_DSI_CMD_REV(cmd, delay, rev)           \
	{                                         \
		sizeof(cmd),                      \
		cmd,                              \
		delay,                            \
		(u32)rev,                         \
	}
#define GS_DSI_CMD(cmd, delay) GS_DSI_CMD_REV(cmd, delay, PANEL_REV_ALL)
#define GS_DSI_CMD0_REV(cmd, rev) GS_DSI_CMD_REV(cmd, 0, rev)
#define GS_DSI_CMD0(cmd) GS_DSI_CMD(cmd, 0)

/**
 * GS_DSI_CMD_SEQ_DELAY_REV - construct a struct gs_dsi_cmd from inline data
 * @rev: The panel revision this applies to, if any
 * @delay: The delay to attach to sending the command
 * @seq: The sequence of commands to pack into a buffer and send
 *
 * Return: struct gs_dsi_cmd holding data necessary to send the command to the
 * panel.
 */
#define GS_DSI_CMD_SEQ_DELAY_REV(rev, delay, seq...) \
	GS_DSI_CMD_REV(((const u8[]){ seq }), delay, rev)
#define GS_DSI_CMD_SEQ_DELAY(delay, seq...) GS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_ALL, delay, seq)
#define GS_DSI_CMD_SEQ_REV(rev, seq...) GS_DSI_CMD_SEQ_DELAY_REV(rev, 0, seq)
#define GS_DSI_CMD_SEQ(seq...) GS_DSI_CMD_SEQ_DELAY(0, seq)

/**
 * DEFINE_GS_CMD_SET - Construct a struct gs_dsi_cmd_set from array of commands
 * @name: The name of the array of `struct gs_dsi_cmd` members
 *
 * This function does some preprocessor expansion to attach the length of a
 * static array of `struct gs_dsi_cmd`s to that array inside a `gs_dsi_cmd_set`
 * data structure. It does this using a particular naming convention, where the
 * input must be named ending in `_cmds` and the output has `_cmd_set` appended
 * to it.
 *
 * Usage example:
 * static const struct gs_dsi_cmd my_panel_turn_on_cmds[] = {
 *   GS_DSI_CMD_SEQ(0x01, 0x02, 0x03, 0x04),
 *   GS_DSI_CMD0(0xB9),
 * };
 * static DEFINE_GS_CMD_SET(my_panel_turn_on);
 *
 * This expands to:
 * static const struct gs_dsi_cmd_set my_panel_turn_on_cmd_set = {...};
 *
 * Return: expansion of array of commands into a `struct gs_dsi_cmd_set`;
 */
#define DEFINE_GS_CMD_SET(name)                        \
	const struct gs_dsi_cmd_set name##_cmd_set = { \
	  .num_cmd = ARRAY_SIZE(name##_cmds),          \
	  .cmds = name##_cmds                          \
	}

/** TE2 Timing **/

/**
 * struct gs_panel_te2_timing - details regarding te2 timing
 */
struct gs_panel_te2_timing {
	/** @rising_edge: vertical start point. */
	u16 rising_edge;
	/** @falling_edge: vertical end point. */
	u16 falling_edge;
};

/** Binned LP Modes **/

/**
 * struct gs_binned_lp - information for binned lp mode.
 * @name: Name of this binned lp mode
 * @bl_threshold: Max brightnes supported by this mode
 * @cmd_set: A dsi command sequence to enter this mode
 * @te2_timing: TE2 signal timing
 */
struct gs_binned_lp {
	const char *name;
	u32 bl_threshold;
	struct gs_dsi_cmd_set cmd_set;
	struct gs_panel_te2_timing te2_timing;
};

/**
 * BINNED_LP_MODE_TIMING - Constructor for struct gs_binned_lp
 * @mode_name: Name to attach to this binned LP mode
 * @bl_thr: Max brightness supported by this mode
 * @cmd_set: Array of gs_dsi_cmds used to enter this mode
 * @rising: TE2 rising time
 * @falling: TE2 falling time
 *
 * Return: A `struct gs_binned_lp` containing this data
 */
#define BINNED_LP_MODE_TIMING(mode_name, bl_thr, cmdset, rising, falling) \
	{                                                                 \
		.name = mode_name, .bl_threshold = bl_thr,                \
		{ .num_cmd = ARRAY_SIZE(cmdset), .cmds = cmdset },        \
		{.rising_edge = rising, .falling_edge = falling }         \
	}
#define BINNED_LP_MODE(mode_name, bl_thr, cmdset) \
	BINNED_LP_MODE_TIMING(mode_name, bl_thr, cmdset, 0, 0)

/** Write Functions **/

/* Command Sets */

/**
 * gs_dsi_send_cmd_set_flags() - Sends a series of dsi commands to the panel
 * @dsi: pointer to mipi_dsi_device by which to write to panel
 * @cmd_set: Set of commands to send
 * @panel_rev: revision identifier for panel to be matched against commands
 * @flags: Any of the Private DSI msg flags to affect command behavior
 */
void gs_dsi_send_cmd_set_flags(struct mipi_dsi_device *dsi, const struct gs_dsi_cmd_set *cmd_set,
			       u32 panel_rev, u32 flags);

/**
 * gs_dsi_send_cmd_set() - Sends a series of dsi commands to the panel
 * @dsi: pointer to mipi_dsi_device by which to write to panel
 * @cmd_set: Set of commands to send
 * @panel_rev: revision identifier for panel to be matched against commands
 */
void gs_dsi_send_cmd_set(struct mipi_dsi_device *dsi, const struct gs_dsi_cmd_set *cmd_set,
			 u32 panel_rev);

/* Raw dcs writes */

ssize_t gs_dsi_dcs_write_buffer(struct mipi_dsi_device *dsi, const void *data,
				size_t len, u16 flags);

static inline ssize_t gs_dsi_dcs_write_buffer_force_batch_begin(struct mipi_dsi_device *dsi)
{
	return gs_dsi_dcs_write_buffer(dsi, NULL, 0, GS_DSI_MSG_FORCE_BATCH);
}

static inline ssize_t gs_dsi_dcs_write_buffer_force_batch_end(struct mipi_dsi_device *dsi)
{
	return gs_dsi_dcs_write_buffer(dsi, NULL, 0,
				       GS_DSI_MSG_FORCE_FLUSH | GS_DSI_MSG_IGNORE_VBLANK);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)) || IS_ENABLED(CONFIG_DRM_DISPLAY_DP_HELPER)
/**
 * gs_dcs_write_dsc_config() - function to write dsc configuration to panel
 * @dev: struct device corresponding to dsi panel
 * @dsc_cfg: dsc configuration to write
 *
 * This function wraps the packing and sending of the pps payload from the
 * more user-readable drm_dsc_config structure. Makes use of the
 * mipi_dsi_picture_parameter_set function for the actual transfer.
 *
 * Return: result of the underlying transfer function
 */
int gs_dcs_write_dsc_config(struct device *dev, const struct drm_dsc_config *dsc_cfg);
#else
static inline int gs_dcs_write_dsc_config(struct device *dev, const struct drm_dsc_config *dsc_cfg)
{
	return -ENOTSUPP;
}
#endif

#define GS_DCS_WRITE_SEQ_FLAGS(dev, flags, seq...)                           \
	do {                                                                 \
		struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);       \
		u8 d[] = { seq };                                            \
		gs_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d), flags); \
	} while (0)

#define GS_DCS_WRITE_SEQ(dev, seq...) GS_DCS_WRITE_SEQ_FLAGS(dev, 0, seq)

#define GS_DCS_WRITE_SEQ_DELAY(dev, delay_ms, seq...)                \
	do {                                                         \
		GS_DCS_WRITE_SEQ(dev, seq);                          \
		usleep_range(delay_ms * 1000, delay_ms * 1000 + 10); \
	} while (0)

#define GS_DCS_WRITE_TABLE_FLAGS(dev, table, flags)                                  \
	do {                                                                         \
		struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);               \
		gs_dsi_dcs_write_buffer(dsi, table, ARRAY_SIZE(table), flags); \
	} while (0)

#define GS_DCS_WRITE_TABLE(dev, table) GS_DCS_WRITE_TABLE_FLAGS(dev, table, 0)

#define GS_DCS_WRITE_TABLE_DELAY(dev, delay_ms, table)               \
	do {                                                         \
		GS_DCS_WRITE_TABLE(dev, table);                      \
		usleep_range(delay_ms * 1000, delay_ms * 1000 + 10); \
	} while (0)

#define GS_DCS_BUF_ADD(dev, seq...) GS_DCS_WRITE_SEQ_FLAGS(dev, GS_DSI_MSG_QUEUE, seq)

#define GS_DCS_BUF_ADD_SET(dev, set) GS_DCS_WRITE_TABLE_FLAGS(dev, set, GS_DSI_MSG_QUEUE)

#define GS_DCS_BUF_ADD_AND_FLUSH(dev, seq...) \
	GS_DCS_WRITE_SEQ_FLAGS(dev, GS_DSI_MSG_IGNORE_VBLANK, seq)

#define GS_DCS_BUF_ADD_SET_AND_FLUSH(dev, set) \
	GS_DCS_WRITE_TABLE_FLAGS(dev, set, GS_DSI_MSG_IGNORE_VBLANK)

#endif // _GS_DCS_HELPER_H_
