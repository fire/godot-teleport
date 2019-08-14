LOCAL_PATH := $(call my-dir)

#--------------------------------------------------------
# SimulCasterRenderer.a
#
# Crossplatform Renderer for SimulCasterClients (PC and Android)
#--------------------------------------------------------
include $(CLEAR_VARS)				# clean everything up to prepare for a module

LOCAL_MODULE    := SimulCasterRenderer	        # generate SimulCasterRenderer.a

include $(LOCAL_PATH)/../../client/cflags.mk

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src/api
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src/crossplatform
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../libavstream/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/VrApi/Include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/VrAppFramework/Include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/LibOVRKernel/Src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/1stParty/OpenGL_Loader/Include
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES  := 	../src/crossplatform/Actor.cpp			\
    					../src/crossplatform/API.cpp			\
						../src/crossplatform/Camera.cpp			\
						../src/crossplatform/DescriptorSet.cpp	\
						../src/crossplatform/GeometryDecoder.cpp\
						../src/crossplatform/Light.cpp			\
						../src/crossplatform/Material.cpp		\
						../src/crossplatform/Mesh.cpp			\
						../src/crossplatform/Renderer.cpp		\
						../src/crossplatform/ResourceCreator.cpp\
	

LOCAL_CFLAGS += -D__ANDROID__
LOCAL_CPPFLAGS += -Wc++17-extensions
LOCAL_CPP_FEATURES += exceptions
include $(BUILD_STATIC_LIBRARY)		# start building based on everything since CLEAR_VARS

