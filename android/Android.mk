LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := main
SDL_PATH := ../SDL
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include
LOCAL_CFLAGS := -O2 -Wno-unused-parameter
LOCAL_SRC_FILES := rom.c machine.c sound.c video.c anim.c screens.c events.c menus.c \
                   menu_care.c menu_info.c life.c front_sdl.c
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -llog
include $(BUILD_SHARED_LIBRARY)
