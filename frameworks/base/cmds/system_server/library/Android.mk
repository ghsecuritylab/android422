LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	system_init.cpp

base = $(LOCAL_PATH)/../../..
native = $(LOCAL_PATH)/../../../../native

LOCAL_C_INCLUDES := \
	$(native)/services/sensorservice \
	$(native)/services/surfaceflinger \
	frameworks/av/media/libmediaplayerservice \
	frameworks/av/services/audioflinger \
	frameworks/av/services/camera/libcameraservice \
	frameworks/native/services/audioflinger \
	$(JNI_H_INCLUDE)

LOCAL_SHARED_LIBRARIES := \
	libandroid_runtime \
	libsensorservice \
	libsurfaceflinger \
    	libinput \
	libaudioflinger \
    	libcameraservice \
    	libmediaplayerservice \
	libutils \
	libbinder \
	libcutils

LOCAL_MODULE:= libsystem_server

include $(BUILD_SHARED_LIBRARY)
