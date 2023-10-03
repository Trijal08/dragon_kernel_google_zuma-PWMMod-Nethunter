/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */
#ifndef _GS_PANEL_FUNCS_DEFAULTS_H_
#define _GS_PANEL_FUNCS_DEFAULTS_H_

#include <linux/types.h>

struct gs_panel;

/*
 * DOC: gs_panel_funcs_defaults theory
 *
 * This file contains a number of default implementations of the functions
 * outlined in the `struct gs_panel_funcs` vtable in `gs_panel.h`
 *
 * These are meant to be used directly or extended in panel-specific driver code
 * as appropriate.
 *
 * In general, these functions should perform behavior that is common to a large
 * range of our panel code.
 */

/**
 * gs_panel_get_panel_rev - Callback for getting panel rev from extinfo block
 * Currently, this would not slot directly into the `get_panel_rev` entry in the
 * vtable, because it operates on the 8-bit build code rather than the entire
 * 32-bit extinfo data block
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_rev` member variable of this struct.
 * @rev: Short-form build-code-based rev entry used to determine revision of
 *       panel
 */
void gs_panel_get_panel_rev(struct gs_panel *ctx, u8 rev);

/**
 * gs_panel_read_slsi_ddic_id - Callback for reading the panel id.
 *
 * This will read the panel id information (serial number) from the SLSI_DDIC_ID
 * reg. It is meant to be used on SLSI ddic's.
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_id` member variable of this struct.
 * Return: 0 on success, negative value on error.
 */
int gs_panel_read_slsi_ddic_id(struct gs_panel *ctx);

/**
 * gs_panel_read_id - Callback for reading the panel id.
 *
 * This will read the panel id information from the register referred to by
 * the panel_id_reg member of the gs_panel_desc, or the PANEL_ID_REG_DEFAULT
 * if no data exists for that register.
 *
 * NOTE: this function is deprecated; for new work, prefer use of
 * gs_panel_read_slsi_ddic_id, or a more vendor-applicable method.
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_id` member variable of this struct.
 * Return: 0 on success, negative value on error.
 */
int gs_panel_read_id(struct gs_panel *ctx);

#endif // _GS_PANEL_FUNCS_DEFAULTS_H_
