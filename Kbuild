# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/$(src)/include

obj-$(CONFIG_BIGOCEAN) += bigocean.o
bigocean-$(CONFIG_BIGOCEAN) += bigo.o bigo_pm.o bigo_io.o bigo_of.o bigo_iommu.o bigo_prioq.o
bigocean-$(CONFIG_SLC_PARTITION_MANAGER) += bigo_slc.o
bigocean-y += bigo_debug.o
