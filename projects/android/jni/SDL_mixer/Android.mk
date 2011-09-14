LOCAL_PATH := $(call my-dir)/../../../../3rdparty/SDL_mixer/src/

include $(CLEAR_VARS)

LOCAL_MODULE := SDL_mixer

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../../../projects/android/jni/tremor/ \
	$(LOCAL_PATH)/../../SDL-1.3/src/include/ \
	$(LOCAL_PATH)/../../../projects/android/jni/mikmod/include/ \

LOCAL_CFLAGS := -DWAV_MUSIC -DOGG_MUSIC -DOGG_USE_TREMOR -DMOD_MUSIC

LOCAL_SRC_FILES := $(notdir $(filter-out %/fluidsynth.c %/playmus.c %/playwave.c, $(wildcard $(LOCAL_PATH)/*.c)))

LOCAL_SHARED_LIBRARIES := SDL mikmod
LOCAL_STATIC_LIBRARIES := tremor

include $(BUILD_SHARED_LIBRARY)