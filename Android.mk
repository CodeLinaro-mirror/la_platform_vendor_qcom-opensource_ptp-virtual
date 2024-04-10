#This makefile is only to compile ptp-virtual driver for AUTO platform

LOCAL_PATH := $(call my-dir)
DLKM_DIR := $(TOP)/device/qcom/common/dlkm

#KBUILD_OPTIONS
KBUILD_OPTIONS += KERNEL_ROOT=$(TOP)/kernel/msm-$(TARGET_KERNEL_VERSION)/
KBUILD_OPTIONS += MODNAME=ptp_virtual
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
$(info value of TARGET_USES_KERNEL_PLATFORM IS '$(TARGET_USES_KERNEL_PLATFORM)')

#Clear Environment Variables
include $(CLEAR_VARS)
#Defining the local options
LOCAL_SRC_FILES             :=  \
                                $(LOCAL_PATH)/Android.mk \
                                $(LOCAL_PATH)/ptp_virtual-board.mk   \
                                $(LOCAL_PATH)/ptp_virtual-product.mk \
                                $(LOCAL_PATH)/Kbuild
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_MODULE              := ptp_virtual.ko
LOCAL_MODULE_TAGS         := optional

include $(DLKM_DIR)/Build_external_kernelmodule.mk
  
ifeq ($(TARGET_BOARD_SUFFIX),_gvm)  
include $(CLEAR_VARS)
LOCAL_EAVB_PATH := $(LOCAL_PATH)
include $(call first-makefiles-under,$(LOCAL_EAVB_PATH))
endif

