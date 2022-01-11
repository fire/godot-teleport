LOCAL_PATH := $(call my-dir)

#--------------------------------------------------------
# ClientRender.a
#
# Crossplatform Renderer for SimulCasterClients (PC and Android)
#--------------------------------------------------------
include $(CLEAR_VARS)				# clean everything up to prepare for a module

LOCAL_MODULE    := ClientRender	        # generate ClientRender.a
LOCAL_STATIC_LIBRARIES	:= Basis_universal enet draco

include $(LOCAL_PATH)/../../client/cflags.mk

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src/api
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src/crossplatform
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../src/crossplatform/NodeComponents
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../firstparty/Platform/External/stb
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../libavstream/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../thirdparty/enet/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../thirdparty/basis_universal
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../thirdparty/basis_universal/transcoder
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../thirdparty/draco/src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../thirdparty/ndk-projects
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/VrApi/Include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/VrAppFramework/Include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/LibOVRKernel/Src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../client/1stParty/OpenGL_Loader/Include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../firstparty
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../..

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES  := 	../src/api/Effect.cpp										\
						../src/api/Texture.cpp										\
						../src/crossplatform/Animation.cpp							\
						../src/crossplatform/API.cpp								\
						../src/crossplatform/Bone.cpp								\
						../src/crossplatform/Camera.cpp								\
						../src/crossplatform/GeometryDecoder.cpp					\
						../src/crossplatform/Light.cpp								\
						../src/crossplatform/Material.cpp							\
						../src/crossplatform/Mesh.cpp								\
						../src/crossplatform/Node.cpp								\
						../src/crossplatform/NodeComponents/AnimationComponent.cpp	\
						../src/crossplatform/NodeComponents/AnimationState.cpp		\
						../src/crossplatform/NodeComponents/VisibilityComponent.cpp	\
						../src/crossplatform/NodeManager.cpp						\
						../src/crossplatform/ResourceCreator.cpp					\
						../src/crossplatform/ShaderSystem.cpp						\
						../src/crossplatform/ShaderResource.cpp						\
						../src/crossplatform/Skin.cpp								\
						../src/crossplatform/Transform.cpp							\
						../src/crossplatform/MemoryUtil.cpp							\

LOCAL_CFLAGS += -D__ANDROID__
LOCAL_CPPFLAGS += -Wc++17-extensions -Wunused-variable
LOCAL_CPP_FEATURES += exceptions
include $(BUILD_STATIC_LIBRARY)		# start building based on everything since CLEAR_VARS

$(call import-module, ../thirdparty/basis_universal/jni)
$(call import-module, ../thirdparty/ndk-projects/draco)
$(call import-module, 3rdParty/enet/jni)