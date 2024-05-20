// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include <linux/of_device.h>
#include "gs_panel/gs_panel_test.h"
#include "trace/panel_trace.h"

static int debugfs_add_test_folder(struct gs_panel_test *test)
{
	struct dentry *test_root, *panel_root = test->ctx->debugfs_entries.panel;

	if (!panel_root)
		return -EFAULT;

	test_root = debugfs_create_dir("test", panel_root);
	if (!test_root)
		return -EFAULT;

	if (gs_panel_test_has_debugfs_init(test))
		test->test_desc->test_funcs->debugfs_init(test, test_root);

	return 0;
}

static int debugfs_remove_test_folder(struct gs_panel_test *test)
{
	struct dentry *panel_root, *test_root;

	panel_root = test->ctx->debugfs_entries.panel;
	if (!panel_root)
		return 0;

	test_root = debugfs_lookup("test", panel_root);
	if (!test_root)
		return 0;

	debugfs_remove_recursive(test_root);

	return 0;
}

int gs_panel_test_common_init(struct platform_device *pdev, struct gs_panel_test *test)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;
	struct gs_panel *ctx;

	if (!parent)
		return 0;

	ctx = dev_get_drvdata(parent);
	if (!ctx)
		return 0;

	PANEL_ATRACE_BEGIN("panel_test_init");

	test->ctx = ctx;
	test->dev = dev;
	test->test_desc = of_device_get_match_data(dev);
	dev_set_drvdata(dev, test);

#ifdef CONFIG_DEBUG_FS
	debugfs_add_test_folder(test);
#endif

	PANEL_ATRACE_END("panel_test_init");

	return 0;
}
EXPORT_SYMBOL_GPL(gs_panel_test_common_init);

int gs_panel_test_common_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gs_panel_test *test = dev_get_drvdata(dev);

	if (!test)
		return 0;

	PANEL_ATRACE_BEGIN("panel_test_remove");

	debugfs_remove_test_folder(test);

	PANEL_ATRACE_END("panel_test_remove");

	return 0;
}
EXPORT_SYMBOL_GPL(gs_panel_test_common_remove);

MODULE_AUTHOR("Safayat Ullah <safayat@google.com>");
MODULE_DESCRIPTION("MIPI-DSI panel driver test abstraction for use across panel vendors");
MODULE_LICENSE("Dual MIT/GPL");
