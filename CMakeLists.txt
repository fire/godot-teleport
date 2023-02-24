cmake_minimum_required(VERSION 3.8)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if(NOT DEFINED TELEPORT_SOLUTION_NAME OR "${TELEPORT_SOLUTION_NAME}" MATCHES "")
	if(TELEPORT_BUILD_DOCS)
		project(Teleport_Docs)
		set(VS_STARTUP_PROJECT Sphinx)
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Sphinx)
	else()
		project(Teleport_Server)
	endif()
else()
	project("${TELEPORT_SOLUTION_NAME}")
endif()
set(PLATFORM_USE_FMT ON)
set(TELEPORT_SOLUTION_NAME "" CACHE STRING "")
option(TELEPORT_SERVER_SUPPORT_WEBRTC "Should the server support WebRTC connections from browsers?" OFF)
set(TELEPORT_LIBWEBRTC_BUILD_DIR "" CACHE PATH "WebRTC Build path")
set(TELEPORT_PLATFORM_DIR "${CMAKE_SOURCE_DIR}/firstparty/Platform" )
set(SIMUL_PLATFORM_FOLDER_PREFIX "Platform" )
set(SIMUL_PLATFORM_DIR ${TELEPORT_PLATFORM_DIR})
set(PLATFORM_IMGUI ON)
option(TELEPORT_BUILD_DOCS "Build documentation?" OFF)
option(TELEPORT_INTERNAL_CHECKS "Internal checks for development?" OFF)
option(TELEPORT_SERVER "Build for server? If true, client will not be built." ${TELEPORT_SERVER})

set (CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "")

#Override srt encryption to be off.
set(ENABLE_ENCRYPTION OFF CACHE BOOL "Enable encryption in SRT")

#Hide Basis Universal STATIC option, and set to opposite of USE_DYNAMIC_RUNTIME.
if(LIBAV_USE_DYNAMIC_RUNTIME)
	set(STATIC OFF CACHE INTERNAL "Static linking for Basis Universal" FORCE)
else()
	set(STATIC ON CACHE INTERNAL "Static linking for Basis Universal" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17 )
set(CMAKE_CXX_STANDARD_REQUIRED ON )
set(CMAKE_CXX_EXTENSIONS ON )
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
SET(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")

set(CONFIG_NAME $<$<CONFIG:Debug>:Debug>$<$<CONFIG:Release>:Release>)
#message(STATUS "CONFIG_NAME ${CONFIG_NAME}")

#defaults for srt:
set(PTHREAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/srt/submodules/pthread-win32)
#set(PTHREAD_LIBRARY ${CMAKE_SOURCE_DIR}/thirdparty/srt/submodules/pthread-win32/bin/x64_MSVC2015.Release/pthread_lib.lib)
set(PTHREAD_LIBRARY ${CMAKE_SOURCE_DIR}/thirdparty/srt/submodules/pthread-win32/windows/VS2019/bin/Release-Unicode-64bit-x64/pthread_static_lib.lib)
option(ENABLE_APPS "" OFF)
option(ENABLE_SHARED "" OFF) 
option(ENABLE_STATIC "" ON)

#openssl

set(SSL_EAY_DEBUG ${CMAKE_SOURCE_DIR}/thirdparty/openssl/libssl_MT.lib)
set(SSL_EAY_RELEASE ${CMAKE_SOURCE_DIR}/thirdparty/openssl/libssl_MT.lib)

set(OPENSSL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/openssl/include)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

#defaults for basisu
option(STATIC "" OFF)
set(CMAKE_CXX_FLAGS_RELEASE "/Od /Ob0 /Zi /DNDEBUG /DNOMINMAX /MT")
set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Ob0 /Od /RTC1 /DNOMINMAX /MDd")


option(BUILD_AS_LIBRARY "" ON) 

function(set_target_runtime targname rt)
	# Default to statically-linked runtime.
	if("${rt}" STREQUAL "")
		set(rt "static")
	endif()
	#message(STATUS "rt ${rt}")
	if(${rt} STREQUAL "static")
		SET_TARGET_PROPERTIES(${targname} PROPERTIES COMPILE_OPTIONS "/MT$<$<CONFIG:Debug>:d>")
	else()	
		SET_TARGET_PROPERTIES(${targname} PROPERTIES COMPILE_OPTIONS "/MD$<$<CONFIG:Debug>:d>")
	endif()
endfunction()

function(SetTeleportDefaults targname)
	set_target_properties( ${targname} PROPERTIES PREFIX "" )
	set_property(TARGET ${targname} PROPERTY COMPILE_WARNING_AS_ERROR ON)
	add_definitions(-DUNICODE -D_UNICODE)
	if(MSVC)
	# Treat angle-brackets as representing external headers. Minimal warnings for external headers.
		target_compile_options(${targname} PRIVATE /W3 /wd4011 /wd4514 /JMC /external:anglebrackets /external:W0)
	else()
		target_compile_options(${targname} PRIVATE -Wall -Wextra -pedantic -Werror )
	endif()
	target_compile_definitions(${targname} PRIVATE CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR} TELEPORT_SUPPORT_AUDIO=$<BOOL:${TELEPORT_SUPPORT_AUDIO}> CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR} TELEPORT_INTERNAL_CHECKS=$<BOOL:${TELEPORT_INTERNAL_CHECKS}>)
	#message("target_compile_definitions(${targname} PRIVATE CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR} TELEPORT_SUPPORT_AUDIO=$<BOOL:${TELEPORT_SUPPORT_AUDIO}> CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR} TELEPORT_INTERNAL_CHECKS=$<BOOL:${TELEPORT_INTERNAL_CHECKS}>)")
endfunction()

function(add_teleport_static_library targname)
  add_library(${ARGV})
  set_target_runtime(${targname} static)
  SetTeleportDefaults( ${targname} )
endfunction()

function(add_teleport_static_executable targname)
	add_executable(${ARGV})
	#message( STATUS set_target_runtime ${targname})
	set_target_runtime(${targname} static)
	SetTeleportDefaults( ${targname} )
endfunction()

#set_target_properties(zlib PROPERTIES FOLDER thirdparty/zlib)

add_subdirectory(TeleportCore)

# Server doesn't include Platform, so load fmt separately.
add_subdirectory(thirdparty/fmt)
set(LIBAV_USE_DYNAMIC_RUNTIME ON CACHE BOOL "")
set(LIBAV_BUILD_SHARED_LIBS ON CACHE BOOL "")
add_subdirectory(thirdparty/cpp-httplib)

add_subdirectory(TeleportServer)
 
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TeleportServer)

add_subdirectory(libavstream)

target_compile_definitions(libavstream PRIVATE LIBAV_USE_D3D12=0)


add_subdirectory(thirdparty/enet)
set_target_properties( enet PROPERTIES FOLDER thirdparty/enet)

add_subdirectory(thirdparty/basis_universal)
set_target_properties( basisu PROPERTIES FOLDER thirdparty/basis_universal)

add_subdirectory(thirdparty/draco)
# Here we will put all the draco targets into the thirdparty/draco solution folder:
macro(get_all_targets_recursive targets dir)
    get_property(subdirectories DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirectories})
        get_all_targets_recursive(${targets} ${subdir})
    endforeach()

    get_property(current_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND ${targets} ${current_targets})
endmacro()
set(targets)
get_all_targets_recursive(targets ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/draco)
foreach(targ ${targets})
	set_target_properties( ${targ} PROPERTIES FOLDER thirdparty/draco)
	#message(${targ})
endforeach()

add_subdirectory(thirdparty/srt)
if(TELEPORT_CLIENT OR TELEPORT_BUILD_DOCS)
	set_target_properties( srt_static PROPERTIES FOLDER thirdparty/srt)
endif()
set_target_properties( srt_virtual PROPERTIES FOLDER thirdparty/srt)


if(TELEPORT_BUILD_DOCS)
	add_subdirectory ("docs")
endif()
