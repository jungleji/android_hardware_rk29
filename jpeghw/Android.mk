LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := libjpeghwenc.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := libjpeghwdec.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := librkswscale.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)
