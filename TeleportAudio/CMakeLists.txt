cmake_minimum_required( VERSION 3.8 )
project( TeleportAudio )

set(DEBUG_CONFIGURATIONS Debug)

# Source
set( src_root
	AudioCommon.h
	AudioCommon.cpp
	)
set( src_crossplatform
	AudioStreamTarget.cpp
	AudioStreamTarget.h
	CustomAudioStreamTarget.cpp
	CustomAudioStreamTarget.h
	AudioPlayer.cpp
	AudioPlayer.h
	NetworkPipeline.cpp
	NetworkPipeline.h
	)
set( src_pc
	PC_AudioPlayer.cpp
	PC_AudioPlayer.h
	PC_AudioCapture.cpp
	PC_AudioCapture.h
	)


if(WIN32)
	set( src_public ${src_root} ${src_crossplatform} ${src_pc})
else()
	set( src_public ${src_root} ${src_crossplatform})
endif()

message ( "src_public ${src_public}" )
#source_group("src" src FILES ${src_root} )
#source_group("src\\crossplatform" src FILES ${src_crossplatform} )

#For MD runtime
add_library( TeleportAudio STATIC ${src_public} ) 

SetTeleportDefaults( TeleportAudio )
set_target_properties( TeleportAudio PROPERTIES FOLDER Teleport )
#Include libavstreamand TeleportCore
target_include_directories(TeleportAudio PUBLIC ../libavstream/include ../)

target_include_directories(TeleportAudio PUBLIC ${SIMUL_PLATFORM_DIR}/External/fmt/include )



#target_compile_features(libavstream PRIVATE cxx_std_17)