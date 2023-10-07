# Build eavb_fe kernel driver
ifeq ($(TARGET_BOARD_AUTO),true)
	BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/eavb_fe.ko
endif
