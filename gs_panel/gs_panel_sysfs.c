/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "gs_panel_internal.h"

#include <linux/sysfs.h>
#include <drm/drm_mipi_dsi.h>

#include "gs_panel/gs_panel.h"

/* Sysfs Node */

static ssize_t serial_number_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	const struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	if (!ctx->initialized)
		return -EPERM;

	if (!strcmp(ctx->panel_id, ""))
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%s\n", ctx->panel_id);
}

static ssize_t panel_extinfo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	const struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	if (!ctx->initialized)
		return -EPERM;

	return snprintf(buf, PAGE_SIZE, "%s\n", ctx->panel_extinfo);
}

static ssize_t panel_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	const char *p;

	/* filter priority info in the dsi device name */
	p = strstr(dsi->name, ":");
	if (!p)
		p = dsi->name;
	else
		p++;

	return snprintf(buf, PAGE_SIZE, "%s\n", p);
}

static ssize_t panel_idle_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t count)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);
	bool idle_enabled;
	int ret;

	ret = kstrtobool(buf, &idle_enabled);
	if (ret) {
		dev_err(dev, "invalid panel idle value\n");
		return ret;
	}

	mutex_lock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	if (idle_enabled != ctx->idle_data.panel_idle_enabled) {
		ctx->idle_data.panel_idle_enabled = idle_enabled;

		if (idle_enabled)
			ctx->timestamps.last_panel_idle_set_ts = ktime_get();

		panel_update_idle_mode_locked(ctx);
	}
	mutex_unlock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/

	return count;
}

static ssize_t panel_idle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	return sysfs_emit(buf, "%d\n", ctx->idle_data.panel_idle_enabled);
}

static ssize_t panel_need_handle_idle_exit_store(struct device *dev, struct device_attribute *attr,
						 const char *buf, size_t count)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);
	bool idle_handle_exit;
	int ret;

	ret = kstrtobool(buf, &idle_handle_exit);
	if (ret) {
		dev_err(dev, "invalid panel idle handle exit value\n");
		return ret;
	}

	mutex_lock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	ctx->idle_data.panel_need_handle_idle_exit = idle_handle_exit;
	mutex_unlock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/

	return count;
}

static ssize_t panel_need_handle_idle_exit_show(struct device *dev, struct device_attribute *attr,
						char *buf)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	return sysfs_emit(buf, "%d\n", ctx->idle_data.panel_need_handle_idle_exit);
}

static ssize_t idle_delay_ms_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);
	u32 idle_delay_ms;
	int ret;

	ret = kstrtou32(buf, 0, &idle_delay_ms);
	if (ret) {
		dev_err(dev, "invalid idle delay ms\n");
		return ret;
	}

	mutex_lock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/
	ctx->idle_data.idle_delay_ms = idle_delay_ms;
	panel_update_idle_mode_locked(ctx);
	mutex_unlock(&ctx->mode_lock); /*TODO(b/267170999): MODE*/

	return count;
}
static ssize_t idle_delay_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	return sysfs_emit(buf, "%d\n", ctx->idle_data.idle_delay_ms);
}

static DEVICE_ATTR_RO(serial_number);
static DEVICE_ATTR_RO(panel_extinfo);
static DEVICE_ATTR_RO(panel_name);
static DEVICE_ATTR_RW(panel_idle);
static DEVICE_ATTR_RW(panel_need_handle_idle_exit);
static DEVICE_ATTR_RW(idle_delay_ms);
/* TODO(tknelms): re-implement below */
#if 0
static DEVICE_ATTR_WO(gamma);
static DEVICE_ATTR_RW(te2_timing);
static DEVICE_ATTR_RW(te2_lp_timing);
static DEVICE_ATTR_RW(min_vrefresh);
static DEVICE_ATTR_RW(force_power_on);
static DEVICE_ATTR_RW(osc2_clk_khz);
static DEVICE_ATTR_RO(available_osc2_clk_khz);
static DEVICE_ATTR_RW(op_hz);
#endif

static const struct attribute *panel_attrs[] = {
	&dev_attr_serial_number.attr,
	&dev_attr_panel_extinfo.attr,
	&dev_attr_panel_name.attr,
	&dev_attr_panel_idle.attr,
	&dev_attr_panel_need_handle_idle_exit.attr,
	&dev_attr_idle_delay_ms.attr,
/* TODO(tknelms): re-implement below */
#if 0
	&dev_attr_gamma.attr,
	&dev_attr_te2_timing.attr,
	&dev_attr_te2_lp_timing.attr,
	&dev_attr_min_vrefresh.attr,
	&dev_attr_force_power_on.attr,
	&dev_attr_osc2_clk_khz.attr,
	&dev_attr_available_osc2_clk_khz.attr,
	&dev_attr_op_hz.attr,
#endif
	NULL
};

int gs_panel_sysfs_create_files(struct device *dev)
{
	return sysfs_create_files(&dev->kobj, panel_attrs);
}
