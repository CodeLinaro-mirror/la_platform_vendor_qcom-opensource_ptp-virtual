#This makefile is only to compile ptp-virtual driver for AUTO platform

LOCAL_PATH := $(call my-dir)
$(warning @@@@@eavb_fe@@@@@@@)
$(warning $(LOCAL_PATH))
DLKM_DIR := $(TOP)/device/qcom/common/dlkm

#KBUILD_OPTIONS
KBUILD_OPTIONS += KERNEL_ROOT=$(shell pwd)/kernel/msm-$(TARGET_KERNEL_VERSION)/
KBUILD_OPTIONS += MODNAME=eavb_fe
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
$(info value of TARGET_USES_KERNEL_PLATFORM IS '$(TARGET_USES_KERNEL_PLATFORM)')

#Clear Environment Variables
include $(CLEAR_VARS)
#Defining the local options
LOCAL_SRC_FILES             :=  \
                                $(LOCAL_PATH)/Android.mk \
                                $(LOCAL_PATH)/eavb_fe-board.mk \
                                $(LOCAL_PATH)/eavb_fe-product.mk \
                                $(LOCAL_PATH)/Kbuild 

LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_MODULE              := eavb_fe.ko
LOCAL_MODULE_TAGS         := optional

include $(DLKM_DIR)/Build_external_kernelmodule.mk
