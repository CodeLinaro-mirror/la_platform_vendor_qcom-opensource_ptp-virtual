ifneq ($(filter $(PLATFORM_VERSION), 16 Baklava),$(PLATFORM_VERSION))
PRODUCT_PACKAGES += eavb_fe.ko
endif
