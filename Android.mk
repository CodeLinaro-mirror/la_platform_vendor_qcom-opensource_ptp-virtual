#This makefile is only to compile ptp-virtual driver for AUTO platform

ifeq ($(TARGET_BOARD_AUTO),true)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
PTPVIRT_BLD_DIR := ../../vendor/qcom/opensource/ptp-virtual
endif # opensource

LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)

DLKM_DIR := ./device/qcom/common/dlkm
KBUILD_OPTIONS := $(PTPVIRT_BLD_DIR)

LOCAL_MODULE := ptp_virtual.ko

include $(DLKM_DIR)/AndroidKernelModule.mk
endif
endif

#ifneq (,$(filter $(QCOM_BOARD_PLATFORMS),$(TARGET_BOARD_PLATFORM)))
#ifneq (,$(filter arm aarch64 arm64, $(TARGET_ARCH)))
#LOCAL_PATH := $(call my-dir)
#include $(CLEAR_VARS)

#commonSources :=

#include $(CLEAR_VARS)
#LOCAL_MODULE      := ptp_virtual.ko

#endif
#endif
