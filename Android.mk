ifneq (,$(filter $(QCOM_BOARD_PLATFORMS),$(TARGET_BOARD_PLATFORM)))
ifneq (,$(filter arm aarch64 arm64, $(TARGET_ARCH)))
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

commonSources :=

#include $(CLEAR_VARS)
LOCAL_MODULE      := ptp_virtual.ko

endif
endif
