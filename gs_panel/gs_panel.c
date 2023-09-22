/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "gs_panel/gs_panel.h"

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/version.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <video/mipi_display.h>

#include "gs_drm/gs_drm_connector.h"
#include "gs_panel_internal.h"
#include "gs_panel/gs_panel_funcs_defaults.h"

#include <trace/panel_trace.h>

/* CONSTANTS */

/* ext_info registers */
static const char ext_info_regs[] = { 0xDA, 0xDB, 0xDC };
#define EXT_INFO_SIZE ARRAY_SIZE(ext_info_regs)

/* DEVICE TREE */

struct gs_drm_connector *get_gs_drm_connector_parent(const struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *panel_node = dev->of_node;
	struct device_node *parent_node;
	struct platform_device *parent_pdev;

	parent_node = of_get_parent(panel_node);
	if (!parent_node) {
		dev_warn(dev, "Unable to find parent node for device_node %p\n", panel_node);
		return NULL;
	}
	parent_pdev = of_find_device_by_node(parent_node);
	if (!parent_pdev) {
		dev_warn(dev, "Unable to find parent platform device for node %p\n", parent_node);
		of_node_put(parent_node);
		return NULL;
	}
	of_node_put(parent_node);
	return platform_get_drvdata(parent_pdev);
}

struct gs_panel *gs_connector_to_panel(const struct gs_drm_connector *gs_connector)
{
	if (!gs_connector->panel_dsi_device) {
		dev_err(gs_connector->base.kdev, "No panel_dsi_device associated with connector\n");
		return NULL;
	}
	return mipi_dsi_get_drvdata(gs_connector->panel_dsi_device);
}

static int gs_panel_parse_gpios(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct gs_panel_gpio *gpio = &ctx->gpio;

	dev_dbg(dev, "%s +\n", __func__);

	gpio->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (gpio->reset_gpio == NULL) {
		dev_warn(dev, "no reset gpio found\n");
	} else if (IS_ERR(gpio->reset_gpio)) {
		dev_err(dev, "failed to get reset-gpios %ld\n", PTR_ERR(gpio->reset_gpio));
		return PTR_ERR(gpio->reset_gpio);
	}

	gpio->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (gpio->enable_gpio == NULL) {
		dev_dbg(dev, "no enable gpio found\n");
	} else if (IS_ERR(gpio->enable_gpio)) {
		dev_warn(dev, "failed to get enable-gpio %ld\n", PTR_ERR(gpio->enable_gpio));
		gpio->enable_gpio = NULL;
	}

	dev_dbg(dev, "%s -\n", __func__);
	return 0;
}

static int gs_panel_parse_regulator_or_null(struct device *dev,
					    struct regulator **regulator,
					    const char name[])
{
	*regulator = devm_regulator_get_optional(dev, name);
	if (IS_ERR(*regulator)) {
		if (PTR_ERR(*regulator) == -ENODEV) {
			dev_warn(dev, "no %s found for panel\n", name);
			*regulator = NULL;
		} else {
			dev_warn(dev, "failed to get panel %s (%pe).\n", name,
				 *regulator);
			return PTR_ERR(*regulator);
		}
	}
	return 0;
}

static int gs_panel_parse_regulators(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct gs_panel_regulator *gs_reg = &ctx->regulator;
	struct regulator *reg;
	int ret = 0;

	ret = gs_panel_parse_regulator_or_null(dev, &gs_reg->vddi, "vddi");
	if (ret)
		return ret;
	ret = gs_panel_parse_regulator_or_null(dev, &gs_reg->vci, "vci");
	if (ret)
		return ret;
	ret = gs_panel_parse_regulator_or_null(dev, &gs_reg->vddd, "vddd");
	if (ret)
		return ret;

	ret = of_property_read_u32(dev->of_node, "vddd-normal-microvolt", &gs_reg->vddd_normal_uV);
	if (ret)
		gs_reg->vddd_normal_uV = 0;

	ret = of_property_read_u32(dev->of_node, "vddd-lp-microvolt", &gs_reg->vddd_lp_uV);
	if (ret) {
		gs_reg->vddd_lp_uV = 0;
		if (gs_reg->vddd_normal_uV != 0) {
			pr_warn("ignore vddd normal %u\n", gs_reg->vddd_normal_uV);
			gs_reg->vddd_normal_uV = 0;
		}
	}

	reg = devm_regulator_get_optional(dev, "vddr_en");
	if (!PTR_ERR_OR_ZERO(reg)) {
		dev_dbg(dev, "panel vddr_en found\n");
		gs_reg->vddr_en = reg;
	}

	reg = devm_regulator_get_optional(dev, "vddr");
	if (!PTR_ERR_OR_ZERO(reg)) {
		dev_dbg(dev, "panel vddr found\n");
		gs_reg->vddr = reg;
	}

	return 0;
}

static int gs_panel_parse_dt(struct gs_panel *ctx)
{
	int ret = 0;
	u32 orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;

	if (IS_ERR_OR_NULL(ctx->dev->of_node)) {
		dev_err(ctx->dev, "no device tree information of gs panel\n");
		return -EINVAL;
	}

	ret = gs_panel_parse_gpios(ctx);
	if (ret)
		goto err;

	ret = gs_panel_parse_regulators(ctx);
	if (ret)
		goto err;

	ctx->touch_dev = of_parse_phandle(ctx->dev->of_node, "touch", 0);

	of_property_read_u32(ctx->dev->of_node, "orientation", &orientation);
	if (orientation > DRM_MODE_PANEL_ORIENTATION_RIGHT_UP) {
		dev_warn(ctx->dev, "invalid display orientation %d\n", orientation);
		orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;
	}
	ctx->orientation = orientation;

err:
	return ret;
}

#ifdef CONFIG_OF
static void devm_backlight_release(void *data)
{
	struct backlight_device *bd = data;

	if (bd)
		put_device(&bd->dev);
}
#endif

static int gs_panel_of_parse_backlight(struct gs_panel *ctx)
{
#ifdef CONFIG_OF
	struct device *dev;
	struct device_node *np;
	struct backlight_device *bd;
	int ret = 0;

	dev = ctx->base.dev;
	if (!dev)
		return -EINVAL;

	if (!dev->of_node)
		return 0;

	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (!np)
		return 0;

	bd = of_find_backlight_by_node(np);
	of_node_put(np);
	if (IS_ERR_OR_NULL(bd))
		return -EPROBE_DEFER;
	ctx->base.backlight = bd;
	ret = devm_add_action(dev, devm_backlight_release, bd);
	if (ret) {
		put_device(&bd->dev);
		return ret;
	}
	ctx->bl_ctrl_dcs = of_property_read_bool(dev->of_node, "bl-ctrl-dcs");
	dev_info(ctx->dev, "successfully registered devtree backlight phandle\n");
	return 0;
#else
	return 0;
#endif
}

/* Panel Info */

static int gs_panel_read_extinfo(struct gs_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char buf[EXT_INFO_SIZE];
	int i, ret;

	/* extinfo already set, skip reading */
	if (ctx->panel_extinfo[0] != '\0')
		return 0;

	for (i = 0; i < EXT_INFO_SIZE; i++) {
		ret = mipi_dsi_dcs_read(dsi, ext_info_regs[i], buf + i, 1);
		if (ret != 1) {
			dev_warn(ctx->dev, "Unable to read panel extinfo (0x%x: %d)\n",
				 ext_info_regs[i], ret);
			return ret;
		}
	}
	bin2hex(ctx->panel_extinfo, buf, EXT_INFO_SIZE);

	return 0;
}

/* Modes */

const struct gs_panel_mode *gs_panel_get_mode(struct gs_panel *ctx,
					      const struct drm_display_mode *mode)
{
	const struct gs_panel_mode *pmode;
	int i;

	if (ctx->desc->modes) {
		for (i = 0; i < ctx->desc->modes->num_modes; i++) {
			pmode = &ctx->desc->modes->modes[i];

			if (drm_mode_equal(&pmode->mode, mode))
				return pmode;
		}
	}

	if (ctx->desc->lp_modes) {
		pmode = &ctx->desc->lp_modes->modes[0];
		if (pmode) {
			const size_t count = ctx->desc->lp_modes->num_modes ?: 1;

			for (i = 0; i < count; i++, pmode++)
				if (drm_mode_equal(&pmode->mode, mode))
					return pmode;
		}
	}

	return NULL;
}

/* TE2 */

/**
 * parse_u32_buf() - Parses a user-provided list of ints into a buffer
 * @src: Source buffer
 * @src_len: Size of source buffer
 * @out: Output buffer for parsed u32s
 * @out_len: Size out output buffer
 *
 * This is a convenience function for parsing a user-provided list of unsigned
 * integers into a buffer. It is meant primarily for handling command-line
 * input, like for a sysfs node.
 *
 * Return: Number of integers parsed
 */
static int parse_u32_buf(char *src, size_t src_len, u32 *out, size_t out_len)
{
	int rc = 0, cnt = 0;
	char *str;
	const char *delim = " ";

	if (!src || !src_len || !out || !out_len)
		return -EINVAL;

	/* src_len is the length of src including null character '\0' */
	if (strnlen(src, src_len) == src_len)
		return -EINVAL;

	for (str = strsep(&src, delim); str != NULL; str = strsep(&src, delim)) {
		rc = kstrtou32(str, 0, out + cnt);
		if (rc)
			return -EINVAL;

		cnt++;
		if (out_len == cnt)
			break;
	}
	return cnt;
}

int gs_panel_get_current_mode_te2(struct gs_panel *ctx, struct gs_panel_te2_timing *timing)
{
	struct gs_te2_mode_data *data;
	const struct drm_display_mode *mode;
	u32 bl_th = 0;
	bool is_lp_mode;
	int i;

	if (!ctx)
		return -EINVAL;

	if (!ctx->current_mode)
		return -EAGAIN;

	mode = &ctx->current_mode->mode;
	is_lp_mode = ctx->current_mode->gs_mode.is_lp_mode;

	if (is_lp_mode && !ctx->desc->lp_modes->num_modes) {
		dev_warn(ctx->dev, "Missing LP mode command set\n");
		return -EINVAL;
	}

	if (is_lp_mode && !ctx->current_binned_lp)
		return -EAGAIN;

	if (ctx->current_binned_lp)
		bl_th = ctx->current_binned_lp->bl_threshold;

	for_each_te2_timing(ctx, is_lp_mode, data, i) {
		if (data->mode != mode)
			continue;

		if (data->binned_lp && data->binned_lp->bl_threshold != bl_th)
			continue;

		timing->rising_edge = data->timing.rising_edge;
		timing->falling_edge = data->timing.falling_edge;

		dev_dbg(ctx->dev, "found TE2 timing %s at %dHz: rising %u falling %u\n",
			!is_lp_mode ? "normal" : "LP", drm_mode_vrefresh(mode), timing->rising_edge,
			timing->falling_edge);

		return 0;
	}

	dev_warn(ctx->dev, "failed to find %s TE2 timing at %dHz\n", !is_lp_mode ? "normal" : "LP",
		 drm_mode_vrefresh(mode));

	return -EINVAL;
}
EXPORT_SYMBOL(gs_panel_get_current_mode_te2);

void gs_panel_update_te2(struct gs_panel *ctx)
{
	if (!gs_panel_has_func(ctx, update_te2))
		return;

	ctx->desc->gs_panel_func->update_te2(ctx);

	if (ctx->bl)
		te2_state_changed(ctx->bl);
}
EXPORT_SYMBOL(gs_panel_update_te2);

ssize_t gs_set_te2_timing(struct gs_panel *ctx, size_t count, const char *buf, bool is_lp_mode)
{
	char *buf_dup;
	ssize_t type_len, data_len;
	u32 timing[MAX_TE2_TYPE * 2] = { 0 };

	if (!gs_is_panel_active(ctx))
		return -EPERM;

	if (!count || !gs_panel_has_func(ctx, update_te2) || !gs_panel_has_func(ctx, set_te2_edges))
		return -EINVAL;

	buf_dup = kstrndup(buf, count, GFP_KERNEL);
	if (!buf_dup)
		return -ENOMEM;

	type_len = gs_get_te2_type_len(ctx->desc, is_lp_mode);
	if (type_len < 0) {
		kfree(buf_dup);
		return type_len;
	}
	data_len = parse_u32_buf(buf_dup, count + 1, timing, type_len * 2);
	if (data_len != type_len * 2) {
		dev_warn(ctx->dev, "invalid number of TE2 %s timing: expected %ld but actual %ld\n",
			 is_lp_mode ? "LP" : "normal", type_len * 2, data_len);
		kfree(buf_dup);
		return -EINVAL;
	}

	mutex_lock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	ctx->desc->gs_panel_func->set_te2_edges(ctx, timing, is_lp_mode);
	gs_panel_update_te2(ctx);
	mutex_unlock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/

	kfree(buf_dup);

	return count;
}

/* IDLE MODE */

unsigned int gs_panel_get_idle_time_delta(struct gs_panel *ctx)
{
	const ktime_t now = ktime_get();
	const enum gs_panel_idle_mode idle_mode =
		(ctx->current_mode) ? ctx->current_mode->idle_mode : GIDLE_MODE_UNSUPPORTED;
	unsigned int delta_ms = UINT_MAX;

	if (idle_mode == GIDLE_MODE_ON_INACTIVITY) {
		delta_ms = ktime_ms_delta(now, ctx->timestamps.last_mode_set_ts);
	} else if (idle_mode == GIDLE_MODE_ON_SELF_REFRESH) {
		const struct gs_panel_timestamps *stamps = &ctx->timestamps;
		const ktime_t ts = max3(stamps->last_self_refresh_active_ts,
					stamps->last_mode_set_ts, stamps->last_panel_idle_set_ts);

		delta_ms = ktime_ms_delta(now, ts);
	} else {
		dev_dbg(ctx->dev, "%s: unsupported idle mode %d", __func__, idle_mode);
	}

	return delta_ms;
}
EXPORT_SYMBOL(gs_panel_get_idle_time_delta);

static bool panel_idle_queue_delayed_work(struct gs_panel *ctx)
{
	const unsigned int delta_ms = gs_panel_get_idle_time_delta(ctx);

	if (delta_ms < ctx->idle_data.idle_delay_ms) {
		struct gs_panel_idle_data *idle_data = &ctx->idle_data;
		const unsigned int delay_ms = idle_data->idle_delay_ms - delta_ms;

		dev_dbg(ctx->dev, "%s: last mode %ums ago, schedule idle in %ums\n", __func__,
			delta_ms, delay_ms);

		mod_delayed_work(system_highpri_wq, &idle_data->idle_work,
				 msecs_to_jiffies(delay_ms));
		return true;
	}

	return false;
}

void panel_update_idle_mode_locked(struct gs_panel *ctx)
{
	const struct gs_panel_funcs *funcs = ctx->desc->gs_panel_func;
	struct gs_panel_idle_data *idle_data = &ctx->idle_data;

	lockdep_assert_held(&ctx->mode_lock); /*TODO(b/267170999): MODE*/

	if (unlikely(!ctx->current_mode) || !gs_is_panel_active(ctx))
		return;

	if (!gs_panel_has_func(ctx, set_self_refresh))
		return;

	if (idle_data->idle_delay_ms && idle_data->self_refresh_active)
		if (panel_idle_queue_delayed_work(ctx))
			return;

	if (delayed_work_pending(&idle_data->idle_work)) {
		dev_dbg(ctx->dev, "%s: cancelling delayed idle work\n", __func__);
		cancel_delayed_work(&idle_data->idle_work);
	}

	if (funcs->set_self_refresh(ctx, idle_data->self_refresh_active)) {
		gs_panel_update_te2(ctx);
		ctx->timestamps.last_self_refresh_active_ts = ktime_get();
	}
}

static void panel_idle_work(struct work_struct *work)
{
	struct gs_panel *ctx = container_of(work, struct gs_panel, idle_data.idle_work.work);

	mutex_lock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	panel_update_idle_mode_locked(ctx);
	mutex_unlock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
}

/* BACKLIGHT */

static int gs_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

u16 gs_panel_get_brightness(struct gs_panel *panel)
{
	return gs_get_brightness(panel->bl);
}
EXPORT_SYMBOL(gs_panel_get_brightness);

static int gs_update_status(struct backlight_device *bl)
{
	struct gs_panel *ctx = bl_get_data(bl);
	struct device *dev = ctx->dev;
	int brightness = bl->props.brightness;
	int min_brightness = ctx->desc->brightness_desc->min_brightness;
	if (min_brightness == 0)
		min_brightness = 1;

	if (!gs_is_panel_active(ctx)) {
		dev_dbg(dev, "panel is not enabled\n");
		return -EPERM;
	}

	/* check if backlight is forced off */
	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (brightness && brightness < min_brightness)
		brightness = min_brightness;

	dev_info(dev, "req: %d, br: %d\n", bl->props.brightness, brightness);

	mutex_lock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	if (ctx->base.backlight && !ctx->bl_ctrl_dcs) {
		dev_info(dev, "Setting brightness via backlight function\n");
		backlight_device_set_brightness(ctx->base.backlight, brightness);
	} else if (gs_panel_has_func(ctx, set_brightness)) {
		ctx->desc->gs_panel_func->set_brightness(ctx, brightness);
	} else {
		dev_info(dev, "Setting brightness via dcs\n");
		gs_dcs_set_brightness(ctx, brightness);
	}

	mutex_unlock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	return 0;
}

static const struct backlight_ops gs_backlight_ops = {
	.get_brightness = gs_get_brightness,
	.update_status = gs_update_status,
};

int gs_panel_update_brightness_desc(struct gs_panel_brightness_desc *desc,
				    const struct gs_brightness_configuration *configs,
				    u32 num_configs, u32 panel_rev)
{
	int i;
	const struct gs_brightness_configuration *matched_config;

	if (!desc || !configs)
		return -EINVAL;

	matched_config = configs;

	if (panel_rev) {
		for (i = 0; i < num_configs; i++, configs++) {
			if (configs->panel_rev & panel_rev) {
				matched_config = configs;
				break;
			}
		}
	}

	desc->max_brightness = matched_config->brt_capability.hbm.level.max;
	desc->min_brightness = matched_config->brt_capability.normal.level.min;
	desc->default_brightness = matched_config->default_brightness,
	desc->brt_capability = &(matched_config->brt_capability);

	return 0;

}
EXPORT_SYMBOL(gs_panel_update_brightness_desc);

/* Regulators */

static int _gs_panel_reg_ctrl(struct gs_panel *ctx, const struct panel_reg_ctrl *reg_ctrl,
			      bool enable)
{
	struct regulator *panel_reg[PANEL_REG_ID_MAX] = {
		[PANEL_REG_ID_VCI] = ctx->regulator.vci,
		[PANEL_REG_ID_VDDD] = ctx->regulator.vddd,
		[PANEL_REG_ID_VDDI] = ctx->regulator.vddi,
		[PANEL_REG_ID_VDDR_EN] = ctx->regulator.vddr_en,
		[PANEL_REG_ID_VDDR] = ctx->regulator.vddr,
	};
	u32 i;

	for (i = 0; i < PANEL_REG_COUNT; i++) {
		enum panel_reg_id id = reg_ctrl[i].id;
		u32 delay_ms = reg_ctrl[i].post_delay_ms;
		int ret;
		struct regulator *reg;

		if (!IS_VALID_PANEL_REG_ID(id))
			return 0;

		reg = panel_reg[id];
		if (!reg) {
			dev_dbg(ctx->dev, "no valid regulator found id=%d\n", id);
			continue;
		}
		ret = enable ? regulator_enable(reg) : regulator_disable(reg);
		if (ret) {
			dev_err(ctx->dev, "failed to %s regulator id=%d\n",
				enable ? "enable" : "disable", id);
			return ret;
		}

		if (delay_ms)
			usleep_range(delay_ms * 1000, delay_ms * 1000 + 10);
		dev_dbg(ctx->dev, "%s regulator id=%d with post_delay=%d ms\n",
			enable ? "enable" : "disable", id, delay_ms);
	}
	return 0;
}

static void gs_panel_pre_power_off(struct gs_panel *ctx)
{
	int ret;

	if (!ctx->desc->reg_ctrl_desc)
		return;

	if (!IS_VALID_PANEL_REG_ID(ctx->desc->reg_ctrl_desc->reg_ctrl_pre_disable[0].id))
		return;

	ret = _gs_panel_reg_ctrl(ctx, ctx->desc->reg_ctrl_desc->reg_ctrl_pre_disable, false);
	if (ret)
		dev_err(ctx->dev, "failed to set pre power off: ret %d\n", ret);
	else
		dev_dbg(ctx->dev, "set pre power off\n");
}

static int _gs_panel_set_power(struct gs_panel *ctx, bool on)
{
	const struct panel_reg_ctrl default_ctrl_disable[PANEL_REG_COUNT] = {
		{ PANEL_REG_ID_VDDR, 0 },
		{ PANEL_REG_ID_VDDR_EN, 0 },
		{ PANEL_REG_ID_VDDD, 0 },
		{ PANEL_REG_ID_VDDI, 0 },
		{ PANEL_REG_ID_VCI, 0 },
	};
	const struct panel_reg_ctrl default_ctrl_enable[PANEL_REG_COUNT] = {
		{ PANEL_REG_ID_VDDI, 5 },
		{ PANEL_REG_ID_VDDD, 0 },
		{ PANEL_REG_ID_VCI, 0 },
		{ PANEL_REG_ID_VDDR_EN, 2 },
		{ PANEL_REG_ID_VDDR, 0 },
	};
	const struct panel_reg_ctrl *reg_ctrl;

	if (on) {
		if (!IS_ERR_OR_NULL(ctx->gpio.enable_gpio)) {
			gpiod_set_value(ctx->gpio.enable_gpio, 1);
			usleep_range(10000, 11000);
		}
		if (ctx->desc->reg_ctrl_desc &&
		    IS_VALID_PANEL_REG_ID(ctx->desc->reg_ctrl_desc->reg_ctrl_enable[0].id)) {
			reg_ctrl = ctx->desc->reg_ctrl_desc->reg_ctrl_enable;
		} else {
			reg_ctrl = default_ctrl_enable;
		}
	} else {
		gs_panel_pre_power_off(ctx);
		if (!IS_ERR_OR_NULL(ctx->gpio.reset_gpio))
			gpiod_set_value(ctx->gpio.reset_gpio, 0);
		if (!IS_ERR_OR_NULL(ctx->gpio.enable_gpio))
			gpiod_set_value(ctx->gpio.enable_gpio, 0);
		if (ctx->desc->reg_ctrl_desc &&
		    IS_VALID_PANEL_REG_ID(ctx->desc->reg_ctrl_desc->reg_ctrl_disable[0].id)) {
			reg_ctrl = ctx->desc->reg_ctrl_desc->reg_ctrl_disable;
		} else {
			reg_ctrl = default_ctrl_disable;
		}
	}

	return _gs_panel_reg_ctrl(ctx, reg_ctrl, on);
}

int gs_panel_set_power_helper(struct gs_panel *ctx, bool on)
{
	int ret;

	ret = _gs_panel_set_power(ctx, on);

	if (ret) {
		dev_err(ctx->dev, "failed to set power: ret %d\n", ret);
		return ret;
	}

	ctx->bl->props.power = on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;

	return 0;
}
EXPORT_SYMBOL(gs_panel_set_power_helper);

void gs_panel_set_vddd_voltage(struct gs_panel *ctx, bool is_lp)
{
	u32 uv = is_lp ? ctx->regulator.vddd_lp_uV : ctx->regulator.vddd_normal_uV;
	if (!uv || !ctx->regulator.vddd)
		return;
	if (regulator_set_voltage(ctx->regulator.vddd, uv, uv))
		dev_err(ctx->dev, "failed to set vddd at %u uV\n", uv);
}

/* INITIALIZATION */

int gs_panel_first_enable(struct gs_panel *ctx)
{
	const struct gs_panel_funcs *funcs = ctx->desc->gs_panel_func;
	struct device *dev = ctx->dev;
	int ret = 0;

	if (ctx->initialized)
		return 0;

	ret = gs_panel_read_extinfo(ctx);
	if (!ret)
		ctx->initialized = true;

	if (!ctx->panel_rev) {
		if (gs_panel_has_func(ctx, get_panel_rev)) {
			u32 id;

			if (kstrtou32(ctx->panel_extinfo, 16, &id)) {
				dev_warn(dev, "failed to get panel extinfo, default to latest\n");
				ctx->panel_rev = PANEL_REV_LATEST;
			} else
				funcs->get_panel_rev(ctx, id);
		} else {
			dev_warn(dev, "unable to get panel rev, default to latest\n");
			ctx->panel_rev = PANEL_REV_LATEST;
		}
	}

	if (gs_panel_has_func(ctx, read_id))
		ret = funcs->read_id(ctx);
	else
		ret = gs_panel_read_id(ctx);
	if (ret)
		return ret;

	if (funcs && funcs->panel_init)
		funcs->panel_init(ctx);

	return ret;
}

static void gs_panel_post_power_on(struct gs_panel *ctx)
{
	int ret;

	if (!ctx->desc->reg_ctrl_desc)
		return;

	if (!IS_VALID_PANEL_REG_ID(ctx->desc->reg_ctrl_desc->reg_ctrl_post_enable[0].id))
		return;

	ret = _gs_panel_reg_ctrl(ctx, ctx->desc->reg_ctrl_desc->reg_ctrl_post_enable, true);
	if (ret)
		dev_err(ctx->dev, "failed to set post power on: ret %d\n", ret);
	else
		dev_dbg(ctx->dev, "set post power on\n");
}

static void gs_panel_handoff(struct gs_panel *ctx)
{
	bool enabled = gpiod_get_raw_value(ctx->gpio.reset_gpio) > 0;
	gs_panel_set_vddd_voltage(ctx, false);
	if (enabled) {
		dev_info(ctx->dev, "panel enabled at boot\n");
		ctx->panel_state = GPANEL_STATE_HANDOFF;
		gs_panel_set_power_helper(ctx, true);
		gs_panel_post_power_on(ctx);
	} else {
		ctx->panel_state = GPANEL_STATE_UNINITIALIZED;
		gpiod_direction_output(ctx->gpio.reset_gpio, 0);
	}

	if (ctx->desc && ctx->desc->modes && ctx->desc->modes->num_modes > 0 &&
	    ctx->panel_state == GPANEL_STATE_HANDOFF) {
		int i;
		for (i = 0; i < ctx->desc->modes->num_modes; i++) {
			const struct gs_panel_mode *pmode;

			pmode = &ctx->desc->modes->modes[i];
			if (pmode->mode.type & DRM_MODE_TYPE_PREFERRED) {
				ctx->current_mode = pmode;
				break;
			}
		}
		if (ctx->current_mode == NULL) {
			ctx->current_mode = &ctx->desc->modes->modes[0];
			i = 0;
		}
		dev_dbg(ctx->dev, "set default panel mode[%d]: %s\n", i,
			ctx->current_mode->mode.name[0] ? ctx->current_mode->mode.name : "NA");
	}
}

static int gs_panel_init_backlight(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	char name[32];
	static atomic_t panel_index = ATOMIC_INIT(-1);

	/* Backlight */
	scnprintf(name, sizeof(name), "panel%d-backlight", atomic_inc_return(&panel_index));
	ctx->bl = devm_backlight_device_register(dev, name, dev, ctx, &gs_backlight_ops, NULL);
	if (IS_ERR(ctx->bl)) {
		dev_err(dev, "failed to register backlight device\n");
		return PTR_ERR(ctx->bl);
	}

	ctx->bl->props.max_brightness = ctx->desc->brightness_desc->max_brightness;
	ctx->bl->props.brightness = ctx->desc->brightness_desc->default_brightness;

	return 0;
}

static void gs_panel_init_te2(struct gs_panel *ctx)
{
	struct gs_te2_mode_data *data;
	const struct gs_binned_lp *binned_lp;
	int i, j;
	int lp_mode_count = ctx->desc->lp_modes->num_modes ?: 1;
	int mode_count, actual_num_binned_lp;

	if (ctx->desc->has_off_binned_lp_entry)
		actual_num_binned_lp = ctx->desc->num_binned_lp - 1;
	else
		actual_num_binned_lp = ctx->desc->num_binned_lp;
	mode_count = ctx->desc->modes->num_modes + lp_mode_count * actual_num_binned_lp;

	if (!gs_panel_has_func(ctx, get_te2_edges) || !gs_panel_has_func(ctx, set_te2_edges) ||
	    !gs_panel_has_func(ctx, update_te2))
		return;

	/* TE2 for non-LP modes */
	for (i = 0; i < ctx->desc->modes->num_modes; i++) {
		const struct gs_panel_mode *pmode = &ctx->desc->modes->modes[i];

		data = &ctx->te2.mode_data[i];
		data->mode = &pmode->mode;
		data->timing.rising_edge = pmode->te2_timing.rising_edge;
		data->timing.falling_edge = pmode->te2_timing.falling_edge;
	}

	/* TE2 for LP modes */
	for (i = 0; i < lp_mode_count; i++) {
		int lp_idx = ctx->desc->modes->num_modes;
		int lp_mode_offset = lp_idx + i * actual_num_binned_lp;

		for_each_gs_binned_lp(j, binned_lp, ctx) {
			int idx;

			/* ignore off binned lp entry, if any */
			if (ctx->desc->has_off_binned_lp_entry && j == 0)
				continue;

			if (ctx->desc->has_off_binned_lp_entry)
				idx = lp_mode_offset + j - 1;
			else
				idx = lp_mode_offset + j;
			if (idx >= mode_count) {
				dev_warn(ctx->dev, "idx %d exceeds mode size %d\n", idx,
					 mode_count);
				return;
			}

			data = &ctx->te2.mode_data[idx];
			data->mode = &ctx->desc->lp_modes->modes[i].mode;
			data->binned_lp = binned_lp;
			data->timing.rising_edge = binned_lp->te2_timing.rising_edge;
			data->timing.falling_edge = binned_lp->te2_timing.falling_edge;
		}
	}

	ctx->te2.option = GTE2_OPT_CHANGEABLE;
}

int gs_dsi_panel_common_init(struct mipi_dsi_device *dsi, struct gs_panel *ctx)
{
	struct device *dev = &dsi->dev;
	int ret = 0;

	dev_dbg(dev, "%s +\n", __func__);

	/* Attach descriptive panel data to driver data structure */
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	/* Set DSI data */
	dsi->lanes = ctx->desc->data_lane_cnt;
	dsi->format = MIPI_DSI_FMT_RGB888;

	/* Connector */
	ctx->gs_connector = get_gs_drm_connector_parent(ctx);

	/* Register connector as bridge */
#ifdef CONFIG_OF
	ctx->bridge.of_node = ctx->gs_connector->base.kdev->of_node;
#endif
	drm_bridge_add(&ctx->bridge);

	/* Parse device tree */
	ret = gs_panel_parse_dt(ctx);
	if (ret) {
		dev_err(dev, "Error parsing device tree (%d), exiting init\n", ret);
		return ret;
	}

	/* One-time configuration */
	if (gs_panel_has_func(ctx, panel_config)) {
		ret = ctx->desc->gs_panel_func->panel_config(ctx);
		if (ret) {
			dev_err(dev, "failed to configure panel settings\n");
			return ret;
		}
	}

	/* Backlight */
	ret = gs_panel_init_backlight(ctx);
	if (ret)
		return ret;

	/* TE2 */
	gs_panel_init_te2(ctx);

	/* HBM */
	/*TODO(tknelms): hbm*/

	/* Vrefresh */
	if (ctx->desc->modes) {
		size_t i;
		for (i = 0; i < ctx->desc->modes->num_modes; i++) {
			const struct gs_panel_mode *pmode = &ctx->desc->modes->modes[i];
			const int vrefresh = drm_mode_vrefresh(&pmode->mode);
			if (ctx->peak_vrefresh < vrefresh)
				ctx->peak_vrefresh = vrefresh;
		}
	}

	/* Idle work */
	ctx->idle_data.panel_idle_enabled = gs_panel_has_func(ctx, set_self_refresh);
	INIT_DELAYED_WORK(&ctx->idle_data.idle_work, panel_idle_work);

	/* Initialize mutexes */
	/*TODO(b/267170999): all*/
	mutex_init(&ctx->mode_lock);
	mutex_init(&ctx->bl_state_lock);
	mutex_init(&ctx->lp_state_lock);

	/* Initialize panel */
	drm_panel_init(&ctx->base, dev, ctx->desc->panel_func, DRM_MODE_CONNECTOR_DSI);

	/* Add the panel officially */
	drm_panel_add(&ctx->base);

	/* Parse device tree - Backlight */
	ret = gs_panel_of_parse_backlight(ctx);
	if (ret) {
		dev_err(dev, "failed to register devtree backlight (%d)\n", ret);
		goto err_panel;
	}

	/* Attach bridge funcs */
	ctx->bridge.funcs = get_panel_drm_bridge_funcs();

	/* Create sysfs files */
	ret = gs_panel_sysfs_create_files(dev);
	if (ret)
		dev_warn(dev, "unable to add panel sysfs failes (%d)\n", ret);
	/* TODO(tknelms): bl_device_groups
	ret = sysfs_create_groups(&ctx->bl->dev.kobj, bl_device_groups);
	if (ret)
		dev_err(dev, "unable to create bl_device_groups groups\n");
	*/

	/* TODO(tknelms): cabc_mode
	if (ctx->desc->gs_panel_func && ctx->desc->gs_panel_func->base &&
	    ctx->desc->gs_panel_func->base->set_cabc_mode) {
		ret = sysfs_create_file(&ctx->bl->dev.kobj, *dev_attr_cabc_mode.attr);
		if (ret)
			dev_err(dev, "unable to create cabc_mode\n");
	}
	*/

	/* panel handoff */
	gs_panel_handoff(ctx);

	/* dsi attach */
	ret = mipi_dsi_attach(dsi);
	if (ret)
		goto err_panel;

	dev_info(dev, "gs common panel driver has been probed; dsi %s\n", dsi->name);
	dev_dbg(dev, "%s -\n", __func__);
	return 0;

err_panel:
	drm_panel_remove(&ctx->base);
	dev_err(dev, "failed to probe gs common panel driver (%d)\n", ret);

	return ret;
}
EXPORT_SYMBOL(gs_dsi_panel_common_init);

int gs_dsi_panel_common_probe(struct mipi_dsi_device *dsi)
{
	struct gs_panel *ctx;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	return gs_dsi_panel_common_init(dsi, ctx);
}
EXPORT_SYMBOL(gs_dsi_panel_common_probe);

static void _gs_dsi_panel_common_remove(struct mipi_dsi_device *dsi)
{
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->base);
	drm_bridge_remove(&ctx->bridge);

	devm_backlight_device_unregister(ctx->dev, ctx->bl);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
void gs_dsi_panel_common_remove(struct mipi_dsi_device *dsi)
{
	_gs_dsi_panel_common_remove(dsi);
}
#else
int gs_dsi_panel_common_remove(struct mipi_dsi_device *dsi)
{
	_gs_dsi_panel_common_remove(dsi);
	return 0;
}
#endif
EXPORT_SYMBOL(gs_dsi_panel_common_remove);

/* DRM panel funcs */

void gs_panel_reset_helper(struct gs_panel *ctx)
{
	u32 delay;
	const u32 *timing_ms = ctx->desc->reset_timing_ms;

	dev_dbg(ctx->dev, "%s +\n", __func__);

	if (IS_ERR_OR_NULL(ctx->gpio.reset_gpio)) {
		dev_dbg(ctx->dev, "%s -(no reset gpio)\n", __func__);
		return;
	}

	gpiod_set_value(ctx->gpio.reset_gpio, 1);
	delay = timing_ms[PANEL_RESET_TIMING_HIGH] ?: 5;
	delay *= 1000;
	usleep_range(delay, delay + 10);

	gpiod_set_value(ctx->gpio.reset_gpio, 0);
	delay = timing_ms[PANEL_RESET_TIMING_LOW] ?: 5;
	delay *= 1000;
	usleep_range(delay, delay + 10);

	gpiod_set_value(ctx->gpio.reset_gpio, 1);
	delay = timing_ms[PANEL_RESET_TIMING_INIT] ?: 10;
	delay *= 1000;
	usleep_range(delay, delay + 10);

	dev_dbg(ctx->dev, "%s -\n", __func__);

	gs_panel_first_enable(ctx);

	gs_panel_post_power_on(ctx);
}
EXPORT_SYMBOL(gs_panel_reset_helper);

/* Timing */

/* Get the VSYNC start time within a TE period */
static u64 gs_panel_vsync_start_time_us(u32 te_us, u32 te_period_us)
{
	/* Approximate the VSYNC start time with TE falling edge. */
	if (te_us > 0 && te_us < te_period_us)
		return te_us * 105 / 100; /* add 5% for variation */

	/* Approximate the TE falling edge with 55% TE width */
	return te_period_us * 55 / 100;
}

int gs_panel_wait_for_vblank(struct gs_panel *ctx)
{
	struct drm_crtc *crtc = NULL;

	if (ctx->gs_connector->base.state)
		crtc = ctx->gs_connector->base.state->crtc;

	if (crtc && !drm_crtc_vblank_get(crtc)) {
		drm_crtc_wait_one_vblank(crtc);
		drm_crtc_vblank_put(crtc);
		return 0;
	}

	WARN_ON(1);
	return -ENODEV;
}
EXPORT_SYMBOL(gs_panel_wait_for_vblank);

void gs_panel_wait_for_vsync_done(struct gs_panel *ctx, u32 te_us, u32 period_us)
{
	u32 delay_us;

	if (unlikely(gs_panel_wait_for_vblank(ctx))) {
		delay_us = period_us + 1000;
		usleep_range(delay_us, delay_us + 10);
		return;
	}

	delay_us = gs_panel_vsync_start_time_us(te_us, period_us);
	usleep_range(delay_us, delay_us + 10);
}
EXPORT_SYMBOL(gs_panel_wait_for_vsync_done);

/* Tracing */

void gs_panel_msleep(u32 delay_ms)
{
	trace_msleep(delay_ms);
}
EXPORT_SYMBOL(gs_panel_msleep);

MODULE_AUTHOR("Taylor Nelms <tknelms@google.com>");
MODULE_DESCRIPTION("MIPI-DSI panel driver abstraction for use across panel vendors");
MODULE_LICENSE("Dual MIT/GPL");
