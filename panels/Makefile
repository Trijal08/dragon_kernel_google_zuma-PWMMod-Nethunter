
M ?= $(shell pwd)

KBASE_PATH_RELATIVE = $(M)

EXTRA_CFLAGS += -DDYNAMIC_DEBUG_MODULE=1
EXTRA_CFLAGS += -Werror

include $(KERNEL_SRC)/../private/google-modules/soc/gs/Makefile.include

EXTRA_SYMBOLS += $(OUT_DIR)/../private/google-modules/display/common/gs_panel/Module.symvers

modules modules_install headers_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)

modules_install: modules
