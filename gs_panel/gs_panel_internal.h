/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */
#ifndef _GS_PANEL_INTERNAL_H_
#define _GS_PANEL_INTERNAL_H_

#include <linux/device.h>
#include <drm/drm_bridge.h>
#include <drm/drm_modeset_helper_vtables.h>

struct gs_panel;
struct gs_drm_connector;
struct dentry;
struct mipi_dsi_device;

/* gs_panel_connector_funcs.c */
int gs_panel_initialize_gs_connector(struct gs_panel *ctx, struct drm_device *drm_dev,
				     struct gs_drm_connector *gs_connector);

/* drm_bridge_funcs.c */
const struct drm_bridge_funcs *get_panel_drm_bridge_funcs(void);

/* gs_panel_sysfs.c */
int gs_panel_sysfs_create_files(struct device *dev);

/* gs_panel_debugfs.c */
/**
 * gs_panel_create_debugfs_entries - Creates debugfs entries for panel
 * @ctx: Pointer to gs_panel
 * @parent: debugfs_entry for drm_connector panel is connected to
 *
 * This both creates the panel's debugfs folder and populates it with the
 * various debugfs files for controlling the panel. It is meant to be called as
 * part of attaching the gs_panel to the gs_drm_connector
 *
 * Return: 0 on success, negative value on error
 */
#ifdef CONFIG_DEBUG_FS
int gs_panel_create_debugfs_entries(struct gs_panel *ctx, struct dentry *parent);
#else
static int gs_panel_create_debugfs_entries(struct gs_panel *ctx, struct dentry *parent)
{
	return -EOPNOTSUPP;
}
#endif

/* gs_dsi_dcs_helper.c */
/**
 * gs_dsi_dcs_transfer - Executes a dsi dcs transfer
 * @dsi: handle for dsi device
 * @type: type of transfer
 * @data: data to transfer
 * @len: length of data
 * @flags: flags for transfer
 *
 * This function is more granular than the public-facing
 * `gs_dsi_dcs_write_buffer` function, in that it allows for explicitly setting
 * the `type` argument. It is not exposed outwardly to reduce API redundancy,
 * but it is retained here in order to allow some internal access (for example,
 * for the debugfs dsi interface)
 *
 * Return: result of transfer operation
 */
ssize_t gs_dsi_dcs_transfer(struct mipi_dsi_device *dsi, u8 type, const void *data, size_t len,
			    u16 flags);

/* gs_panel.c */
int gs_panel_first_enable(struct gs_panel *ctx);

/**
 * gs_panel_set_vddd_voltage() - Sets appropriate voltage on vddd
 * @ctx: Pointer to gs_panel
 * @is_lp: whether we're setting voltage for an lp mode
 */
void gs_panel_set_vddd_voltage(struct gs_panel *ctx, bool is_lp);

/**
 * get_gs_drm_connector_parent - gets the connector that is panel's parent
 * @ctx: Pointer to panel
 *
 * Return:  Pointer to parent connector, or NULL if error
 */
struct gs_drm_connector *get_gs_drm_connector_parent(const struct gs_panel *ctx);

/**
 * gs_connector_to_panel - get gs_panel object attached to given gs_connector
 * @gs_connector: Pointer to gs_connector
 *
 * This function returns the gs_panel that was connected to the gs_drm_connector
 * during the connector's probe function. The misdirection with the
 * mipi_dsi_device is in service to decoupling dependencies between the two
 * modules.
 *
 * Return: pointer to gs_panel attached to connector, or NULL on error.
 */
struct gs_panel *gs_connector_to_panel(const struct gs_drm_connector *gs_connector);

#endif // _GS_PANEL_INTERNAL_H_
