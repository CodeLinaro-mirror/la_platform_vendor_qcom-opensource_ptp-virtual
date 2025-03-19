# Build ptp-virtual kernel driver
ifeq ($(TARGET_BOARD_AUTO),true)
ifneq ($(filter $(PLATFORM_VERSION), 16 Baklava),$(PLATFORM_VERSION))
	BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/ptp_virtual.ko
endif
endif
