# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y       += -I$(srctree)/include/

exynos-hdcp2-y += exynos-hdcp2-main.o
exynos-hdcp2-y += exynos-hdcp2-teeif.o
exynos-hdcp2-y += exynos-hdcp2-session.o
exynos-hdcp2-y += exynos-hdcp2-protocol-msg.o
exynos-hdcp2-y += exynos-hdcp2-dplink-inter.o
exynos-hdcp2-y += exynos-hdcp2-dplink.o
exynos-hdcp2-y += exynos-hdcp2-dplink-if.o
exynos-hdcp2-y += exynos-hdcp2-dplink-auth.o
exynos-hdcp2-y += exynos-hdcp2-dplink-protocol-msg.o
exynos-hdcp2-y += exynos-hdcp2-selftest.o

obj-$(CONFIG_EXYNOS_HDCP2) += exynos-hdcp2.o
