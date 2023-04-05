// SPDX-License-Identifier: GPL-2.0
/*
 * Google LWIS Test Device Driver
 *
 * Copyright (c) 2022 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-test-dev: " fmt

#include "lwis_device_test.h"

#include <linux/module.h>
#include <linux/platform_device.h>

#include "lwis_commands.h"
#include "lwis_init.h"
#include "lwis_platform.h"

#ifdef CONFIG_OF
#include "lwis_dt.h"
#endif

#define LWIS_DRIVER_NAME "lwis-test"

static struct lwis_device_subclass_operations test_vops = {
	.register_io = NULL,
	.register_io_barrier = NULL,
	.device_enable = NULL,
	.device_disable = NULL,
	.event_enable = NULL,
	.event_flags_updated = NULL,
	.close = NULL,
};

static struct lwis_event_subscribe_operations test_subscribe_ops = {
	.subscribe_event = NULL,
	.unsubscribe_event = NULL,
	.notify_event_subscriber = NULL,
	.release = NULL,
};

static int lwis_test_device_probe(struct platform_device *plat_dev)
{
	int ret = 0;
	struct lwis_test_device *test_dev;
	struct device *dev = &plat_dev->dev;

	/* Allocate test device specific data construct */
	test_dev = devm_kzalloc(dev, sizeof(struct lwis_test_device), GFP_KERNEL);
	if (!test_dev) {
		dev_err(dev, "Failed to allocate test device structure\n");
		return -ENOMEM;
	}

	test_dev->base_dev.type = DEVICE_TYPE_TEST;
	test_dev->base_dev.vops = test_vops;
	test_dev->base_dev.subscribe_ops = test_subscribe_ops;

	/* Call the base device probe function */
	ret = lwis_base_probe(&test_dev->base_dev, plat_dev);
	if (ret) {
		dev_err(dev, "Error in lwis base probe, ret: %d\n", ret);
	}

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_TEST_DEVICE_COMPAT },
	{},
};
// MODULE_DEVICE_TABLE(of, lwis_id_match);

static struct platform_driver lwis_driver = {
	.probe = lwis_test_device_probe,
	.driver = {
		.name = LWIS_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lwis_id_match,
	},
};

#else /* CONFIG_OF not defined */
static struct platform_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lwis_driver_id);

static struct platform_driver lwis_driver = { .probe = lwis_test_device_probe,
					      .id_table = lwis_driver_id,
					      .driver = {
						      .name = LWIS_DRIVER_NAME,
						      .owner = THIS_MODULE,
					      } };
#endif /* CONFIG_OF */

/*
 *  lwis_test_device_init: Init function that will be called by the kernel
 *  initialization routines.
 */
int __init lwis_test_device_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&lwis_driver);
	if (ret)
		pr_err("platform_driver_register failed: %d\n", ret);

	return ret;
}

int lwis_test_device_deinit(void)
{
	platform_driver_unregister(&lwis_driver);
	return 0;
}
