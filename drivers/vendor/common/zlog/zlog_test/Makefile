
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/zlog/zlog_exception
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/zlog/zlog_common

obj-$(CONFIG_VENDOR_ZTE_LOG_E_TEST) := zlog_test_dlkm.o

zlog_test_dlkm-objs += zlog_test.o

ifeq ($(CONFIG_VENDOR_ZTE_MISC),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_MISC_COMMON
endif

#BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/zlog_test_dlkm.ko

