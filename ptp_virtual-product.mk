ifneq ($(filter $(PLATFORM_VERSION), 16 Baklava),$(PLATFORM_VERSION))
PRODUCT_PACKAGES += ptp_virtual.ko
endif
