#This makefile is only to compile ptp-virtual driver for AUTO platform

ifeq ($(TARGET_BOARD_AUTO),true)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_CFLAGS += -Wdeclaration-after-statement
# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
PTPVIRT_BLD_DIR := ../../vendor/qcom/opensource/ptp-virtual
endif # opensource

LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)

DLKM_DIR := ./device/qcom/common/dlkm
KBUILD_OPTIONS := $(PTPVIRT_BLD_DIR)

LOCAL_MODULE := ptp_virtual.ko
BOARD_VENDOR_KERNEL_MODULES := $(KERNEL_MODULES_OUT)/ptp_virtual.ko

ifeq ($(TARGET_USES_KERNEL_PLATFORM),true)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
else
include $(DLKM_DIR)/AndroidKernelModule.mk
endif
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
