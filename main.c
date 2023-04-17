// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *
 * Samsung DisplayPort driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smc.h>
#include <asm/cacheflush.h>
#include <linux/soc/samsung/exynos-smc.h>

#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/dma-mapping.h>

#include "exynos-hdcp-interface.h"

#include "auth-control.h"
#include "hdcp-log.h"
#include "selftest.h"
#include "teeif.h"

#define EXYNOS_HDCP_DEV_NAME	"hdcp2"

static struct miscdevice hdcp;

static ssize_t hdcp_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	hdcp_info("Kicking off selftest\n");
	hdcp_protocol_self_test();
	return len;
}

static ssize_t hdcp_read(struct file *filp, char __user *buf,
			 size_t count, loff_t *f_pos)
{
	return 0;
}

static int exynos_hdcp_probe(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id exynos_hdcp_of_match_table[] = {
	{ .compatible = "samsung,exynos-hdcp", },
	{ },
};

static struct platform_driver exynos_hdcp_driver = {
	.probe = exynos_hdcp_probe,
	.driver = {
		.name = "exynos-hdcp",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_hdcp_of_match_table),
	}
};

static int __init hdcp_init(void)
{
	int ret;

	hdcp_info("hdcp2 driver init\n");

	ret = misc_register(&hdcp);
	if (ret) {
		hdcp_err("hdcp can't register misc on minor=%d\n",
				MISC_DYNAMIC_MINOR);
		return ret;
	}

	hdcp_tee_init();
	hdcp_auth_worker_init();

	return platform_driver_register(&exynos_hdcp_driver);
}

static void __exit hdcp_exit(void)
{
	misc_deregister(&hdcp);
	hdcp_tee_close();
	hdcp_auth_worker_deinit();

	platform_driver_unregister(&exynos_hdcp_driver);
}

static const struct file_operations hdcp_fops = {
	.owner		= THIS_MODULE,
	.write 		= hdcp_write,
	.read 		= hdcp_read,
};

static struct miscdevice hdcp = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= EXYNOS_HDCP_DEV_NAME,
	.fops	= &hdcp_fops,
};

module_init(hdcp_init);
module_exit(hdcp_exit);

MODULE_DESCRIPTION("Exynos Secure hdcp driver");
MODULE_AUTHOR("<hakmin_1.kim@samsung.com>");
MODULE_LICENSE("GPL v2");
