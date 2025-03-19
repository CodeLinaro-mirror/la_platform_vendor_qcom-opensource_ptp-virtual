# Build eavb_fe kernel driver
ifneq ($(TARGET_BOARD_PLATFORM),gen5)
ifeq ($(TARGET_BOARD_AUTO),true)
ifneq ($(filter $(PLATFORM_VERSION), 16 Baklava),$(PLATFORM_VERSION)) 
	BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/eavb_fe.ko
endif
endif
endif
