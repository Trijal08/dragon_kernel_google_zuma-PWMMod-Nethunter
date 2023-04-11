/*
 * drivers/soc/samsung/exynos-hdcp/exynos-hdcp.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smc.h>
#include <asm/cacheflush.h>
#include <linux/soc/samsung/exynos-smc.h>
#include "exynos-hdcp2-teeif.h"
#include "exynos-hdcp2-log.h"
#include "exynos-hdcp2-dplink-if.h"
#include "exynos-hdcp2-dplink.h"
#include "exynos-hdcp2-dplink-inter.h"
#include "exynos-hdcp2-selftest.h"
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/dma-mapping.h>

#define EXYNOS_HDCP_DEV_NAME	"hdcp2"

static struct miscdevice hdcp;
struct device *device_hdcp;
static DEFINE_MUTEX(hdcp_lock);
extern enum dp_state dp_hdcp_state;

struct hdcp_ctx {
	struct delayed_work work;
	/* debugfs root */
	struct dentry *debug_dir;
	/* seclog can be disabled via debugfs */
	bool enabled;
	unsigned int irq;
};

static struct hdcp_ctx h_ctx;
static uint32_t inst_num;

static ssize_t hdcp_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	hdcp_info("Kicking off selftest\n");
	dp_hdcp_protocol_self_test();
	return len;
}

static ssize_t hdcp_read(struct file *filp, char __user *buf,
			 size_t count, loff_t *f_pos)
{
	return 0;
}

static int hdcp_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct device *dev = miscdev->this_device;
	struct hdcp_info *info;

	info = kzalloc(sizeof(struct hdcp_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	file->private_data = info;

	mutex_lock(&hdcp_lock);
	inst_num++;
	/* todo: hdcp device initialize ? */
	mutex_unlock(&hdcp_lock);

	return 0;
}

static int hdcp_release(struct inode *inode, struct file *file)
{
	struct hdcp_info *info = file->private_data;

	/* disable drm if we were the one to turn it on */
	mutex_lock(&hdcp_lock);
	inst_num--;
	/* todo: hdcp device finalize ? */
	mutex_unlock(&hdcp_lock);

	kfree(info);
	return 0;
}

static void exynos_hdcp_worker(struct work_struct *work)
{
	if (dp_hdcp_state == DP_DISCONNECT) {
		hdcp_err("dp_disconnected\n");
		return;
	}

	hdcp_info("Exynos HDCP interrupt occur by LDFW.\n");
	hdcp_dplink_auth_control(HDCP2_ON);
}

static irqreturn_t exynos_hdcp_irq_handler(int irq, void *dev_id)
{
	if (h_ctx.enabled) {
		schedule_delayed_work(&h_ctx.work, msecs_to_jiffies(0));
	}

	return IRQ_HANDLED;
}

static int exynos_hdcp_probe(struct platform_device *pdev)
{
	int err;

	h_ctx.irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!h_ctx.irq) {
		dev_err(&pdev->dev, "Fail to get irq from dt\n");
		return -EINVAL;
	}

	/* Get hardware interrupt number */
	err = devm_request_irq(&pdev->dev, h_ctx.irq,
			exynos_hdcp_irq_handler,
			IRQF_TRIGGER_RISING, pdev->name, NULL);

	device_hdcp = &pdev->dev;

//	arch_setup_dma_ops(&pdev->dev, 0x0ULL, 1ULL << 36, NULL, false);
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));

	if (err) {
		dev_err(&pdev->dev,
				"Fail to request IRQ handler. err(%d) irq(%d)\n",
				err, h_ctx.irq);
		return err;
	}
	/* Set workqueue for Secure log as bottom half */
	INIT_DELAYED_WORK(&h_ctx.work, exynos_hdcp_worker);
	h_ctx.enabled = true;

	hdcp_info("Exynos HDCP driver probe done! (%d)\n", err);
	return err;
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

	hdcp_session_list_init();
	hdcp_tee_init();

	return platform_driver_register(&exynos_hdcp_driver);
}

static void __exit hdcp_exit(void)
{
	/* todo: do clear sequence */
	cancel_delayed_work_sync(&h_ctx.work);

	misc_deregister(&hdcp);
	hdcp_session_list_destroy();
	hdcp_tee_close();
	platform_driver_unregister(&exynos_hdcp_driver);
}

static const struct file_operations hdcp_fops = {
	.owner		= THIS_MODULE,
	.write 		= hdcp_write,
	.read 		= hdcp_read,
	.open		= hdcp_open,
	.release	= hdcp_release,
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
