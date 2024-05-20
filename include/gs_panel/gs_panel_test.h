/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include <linux/debugfs.h>
#include <linux/platform_device.h>

#include "gs_panel/gs_panel.h"

struct gs_panel_test;

struct gs_panel_test_funcs {
	/**
	 * @debugfs_init:
	 *
	 * Allows panel tests to create panels-specific debugfs files.
	 */
	void (*debugfs_init)(struct gs_panel_test *test, struct dentry *test_root);
};

struct gs_panel_test_desc {
	const struct gs_panel_test_funcs *test_funcs;
};

struct gs_panel_test {
	struct gs_panel *ctx;
	struct device *dev;
	const struct gs_panel_test_desc *test_desc;
};

int gs_panel_test_common_init(struct platform_device *pdev, struct gs_panel_test *test);
int gs_panel_test_common_remove(struct platform_device *pdev);

#define gs_panel_test_has_debugfs_init(test)                             \
	((test) && (test->test_desc) && (test->test_desc->test_funcs) && \
	 (test->test_desc->test_funcs->debugfs_init))
