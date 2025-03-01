cmake_minimum_required(VERSION 3.8)
project(pc_client)

option(TELEPORT_CLIENT_USE_D3D12 "Use D3D12?" OFF)
if( ${TELEPORT_CLIENT_USE_D3D12})
	set(TELEPORT_CLIENT_USE_D3D11 OFF)
else()
	set(TELEPORT_CLIENT_USE_D3D11 ON)
endif()
option(TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER "Use Platform video decoder? Currently requires TELEPORT_CLIENT_USE_D3D12" OFF)

if(${TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER})
	if(NOT ${TELEPORT_CLIENT_USE_D3D12})
		message("Warning: Setting TELEPORT_CLIENT_USE_D3D12 true because TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER is true.")
		set(TELEPORT_CLIENT_USE_D3D12 ON)
	endif()
endif()

file(GLOB inifiles "*.ini")
set(srcs
	Main.cpp
	MemoryUtil.cpp
	MemoryUtil.h
	UseOpenXR.cpp
	UseOpenXR.h
	Resource.h
	targetver.h
	../ClientApp/ClientApp.cpp
	../ClientApp/ClientApp.h
)

if(WIN32)
	set(def_platform PLATFORM_WINDOWS UNICODE _WIN32_WINNT=0x0601)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DPLATFORM_DIR=${TELEPORT_PLATFORM_DIR}")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DPLATFORM_DIR=${TELEPORT_PLATFORM_DIR}")
else()
	message(FATAL_ERROR "Unsupported platform!")
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /wd4711")
source_group("Shaders" src FILES ${shaders} )

add_static_executable( pc_client WIN32 SOURCES ${srcs} ${shaders} ${inifiles} Icon.rc)


target_include_directories(pc_client PRIVATE 
	${CMAKE_CURRENT_SOURCE_DIR} ${TELEPORT_SIMUL}/.. ${CMAKE_SOURCE_DIR}/../thirdparty/enet/include ../libavstream/include ../TeleportAudio/src ../ClientRender/src ../thirdparty/basis_universal ${TELEPORT_PLATFORM_DIR}/Windows)

if(TELEPORT_CLIENT_USE_D3D12)
	target_link_libraries(pc_client SimulDirectX12_MT)
else()
	target_link_libraries(pc_client SimulDirectX11_MT)
	target_link_libraries(pc_client d3d12)
endif()
target_compile_definitions(pc_client PUBLIC TELEPORT_CLIENT_USE_D3D12=$<BOOL:${TELEPORT_CLIENT_USE_D3D12}> TELEPORT_CLIENT_USE_D3D11=$<BOOL:${TELEPORT_CLIENT_USE_D3D11}> TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER=$<BOOL:${TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER}>) 	

SetTeleportDefaults(pc_client)
set_target_properties( pc_client PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
set_target_properties( pc_client PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties( pc_client PROPERTIES FOLDER Client)
set_target_properties( pc_client PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x86;$(PATH)"	 )		
target_include_directories(pc_client PUBLIC ../thirdparty/enet/Include ${CMAKE_SOURCE_DIR}/thirdparty/openxr-sdk/include ${SIMUL_PLATFORM_DIR}/External/fmt/include ${SIMUL_PLATFORM_DIR}/External/imgui)
target_compile_features(pc_client PRIVATE cxx_std_17)
target_link_directories(pc_client PUBLIC ${CMAKE_SOURCE_DIR}/thirdparty/openxr-sdk/lib)
#target_link_direc tories(pc_client PUBLIC "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.19041.0/um/x64")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	target_compile_definitions(pc_client PRIVATE PLATFORM_64BIT)
endif()

target_link_libraries(pc_client debug openxr_loaderd optimized openxr_loader) 
target_link_libraries(pc_client libavstream enet TeleportAudio ClientRender TeleportClient TeleportCore basisu winmm)
target_link_libraries(pc_client Core_MT SimulCrossPlatform_MT SimulMath_MT directxtex_MT ImGui_MT fmt)
target_link_libraries(pc_client -ignore:4711)

target_link_options(pc_client PUBLIC /ignore:4711)
target_compile_options(pc_client PRIVATE /wd4711)

set(CPACK_PACKAGE_INSTALL_DIRECTORY "cpack_install")

LIST(APPEND CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$INSTDIR\\\\TeleportClient.lnk' '$INSTDIR\\\\bin\\\\Release\\\\pc_client..exe' '-log'"
)
LIST(APPEND CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$INSTDIR\\\\TeleportClient.lnk'"
)
install(TARGETS pc_client
		RUNTIME
		DESTINATION build_pc_client/bin/Release
		COMPONENT client)
file(GLOB INSTALL_TEXTURES "${CMAKE_CURRENT_SOURCE_DIR}/textures/*.*")
file(GLOB INSTALL_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.*")
install(FILES "${CMAKE_BINARY_DIR}\\\\bin\\\\Release\\\\pc_client.exe" DESTINATION build_pc_client/bin/Release COMPONENT client )
install(FILES "${CMAKE_BINARY_DIR}\\\\bin\\\\Release\\\\pc_client.pdb" DESTINATION build_pc_client/bin/Release COMPONENT client )
install(FILES ${INSTALL_TEXTURES} DESTINATION pc_client/textures COMPONENT client )
install(FILES ${INSTALL_FONTS} DESTINATION pc_client/assets/fonts COMPONENT client )
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}\\\\assets\\\\localGeometryCache" DESTINATION pc_client/assets COMPONENT client)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}\\\\client_default.ini" DESTINATION pc_client/assets RENAME client.ini COMPONENT client OPTIONAL)
install(DIRECTORY "${CMAKE_BINARY_DIR}\\\\firstparty\\\\Platform\\\\DirectX11\\\\shaderbin\\\\" DESTINATION pc_client/assets/shaders/directx11 COMPONENT client)
install(DIRECTORY "${CMAKE_BINARY_DIR}\\\\firstparty\\\\Platform\\\\DirectX12\\\\shaderbin\\\\" DESTINATION pc_client/assets/shaders/directx12 COMPONENT client)
install(DIRECTORY "${CMAKE_BINARY_DIR}\\\\firstparty\\\\Platform\\\\Vulkan\\\\shaderbin\\\\" DESTINATION pc_client/assets/shaders/vulkan COMPONENT client)

install(DIRECTORY "${CMAKE_BINARY_DIR}\\\\firstparty\\\\Platform\\\\DirectX11\\\\shaderbin\\\\" DESTINATION pc_client/assets/shaders/directx11 COMPONENT client)

message("FILES ${shaders} DESTINATION pc_client\\\\Shaders COMPONENT client")


install(FILES ${shaders} DESTINATION pc_client\\\\Shaders COMPONENT client)