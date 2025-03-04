/************************************************************************************

Filename    :   SoundEffectContext.cpp
Content     :   
Created     :   
Authors     :   

Copyright   :   Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.

*************************************************************************************/

#include "JniUtils.h"
#include "SoundEffectContext.h"
//#include "VrCommon.h"
//#include "GUI/FileSys.h"

using namespace OVRFW;

const char * CMD_LOAD_SOUND_ASSET = "loadSoundAsset";

ovrSoundEffectContext::ovrSoundEffectContext( JNIEnv & jni_, jobject activity_ )
    : SoundPool( jni_, activity_ )
{
#if defined( OVR_OS_ANDROID )
	JavaVM * vm = NULL;
	jni_.GetJavaVM( &vm );
	Ttj.Init( *vm, *this );
#endif
}

ovrSoundEffectContext::~ovrSoundEffectContext()
{
}

void ovrSoundEffectContext::Initialize( OVRFW::ovrFileSys * fileSys )
{
	SoundPool.Initialize( fileSys );
	// TODO: kick this off in the background in the constructor?
	SoundAssetMapping.LoadSoundAssets( fileSys );
}

void ovrSoundEffectContext::Play( const char * name )
{
#if defined( OVR_OS_ANDROID )
	Ttj.GetMessageQueue().PostPrintf( "sound %s", name );
#elif defined( OVR_OS_WIN32 )
	JNIEnv & dummyJni = *(JNIEnv *)0;
	PlayInternal( dummyJni, name );
#else
	OVR_UNUSED( name );
#endif
}

void ovrSoundEffectContext::Stop( const char * name )
{
#if defined( OVR_OS_ANDROID )
	Ttj.GetMessageQueue().PostPrintf( "stopSound %s", name );
#elif defined( OVR_OS_WIN32 )
	JNIEnv & dummyJni = *(JNIEnv *)0;
	StopInternal( dummyJni, name );
#else
	OVR_UNUSED( name );
#endif
}

void ovrSoundEffectContext::LoadSoundAsset( const char * name )
{
#if defined( OVR_OS_ANDROID )
 	Ttj.GetMessageQueue().PostPrintf( "%s %s", CMD_LOAD_SOUND_ASSET, name );
#else
	OVR_UNUSED( name );
#endif
}

void ovrSoundEffectContext::PlayInternal( JNIEnv & env, const char * name )
{
	// Get sound from the asset mapping
	std::string soundFile;
	if ( SoundAssetMapping.GetSound( name, soundFile ) )
	{
		// OVR_LOG( "ovrSoundEffectContext::PlayInternal(%s) : %s", name, soundFile.c_str() );
		SoundPool.Play( env, soundFile.c_str() );
	}
	else
	{
		OVR_WARN( "ovrSoundEffectContext::Play called with non-asset-mapping-defined sound: %s", name );
		// TODO: PC assumes the sound is defined in a sound-asset mapping table. The incoming
		// name is a 'key-name' and not the actual file-name. Provide a PlaySoundFromUri for 
		// non-asset-mapped sounds.
#if defined( OVR_OS_ANDROID )
		SoundPool.Play( env, name );
#endif
	}
}

void ovrSoundEffectContext::StopInternal( JNIEnv & env, const char * name )
{
	// Get sound from the asset mapping
	std::string soundFile;
	if ( SoundAssetMapping.GetSound( name, soundFile ) )
	{
		// OVR_LOG( "ovrSoundEffectContext::PlayInternal(%s) : %s", name, soundFile.c_str() );
		SoundPool.Stop( env, soundFile.c_str() );
	}
	else
	{
		OVR_WARN( "ovrSoundEffectContext::Play called with non-asset-mapping-defined sound: %s", name );
		// TODO: PC assumes the sound is defined in a sound-asset mapping table. The incoming
		// name is a 'key-name' and not the actual file-name. Provide a PlaySoundFromUri for 
		// non-asset-mapped sounds.
#if defined( OVR_OS_ANDROID )
		SoundPool.Stop( env, name );
#endif
	}
}

//==============================
// Allows for pre-loading of the sound asset file into memory on Android.
//==============================
void ovrSoundEffectContext::LoadSoundAssetInternal( JNIEnv & env, const char * name )
{
#if defined( OVR_OS_ANDROID )
	// Get sound from the asset mapping
	std::string soundFile;
	if ( SoundAssetMapping.GetSound( name, soundFile ) )
	{
		SoundPool.LoadSoundAsset( env, soundFile.c_str() );
	}
	else
	{
		OVR_WARN( "ovrSoundEffectContext::LoadSoundAssetInternal called with non-asset-mapping-defined sound: %s", name );
		SoundPool.LoadSoundAsset( env, name );
	}
#else
	OVR_UNUSED( &env );
	OVR_UNUSED( name );
#endif
}

// Returns true if head equals check plus zero or more characters.
static bool MatchesHead( const char * head, const char * check )
{
	const int l = static_cast<int>( OVR::OVR_strlen( head ) );
	return 0 == OVR::OVR_strncmp( head, check, l );
}
void ovrSoundEffectContext::TtjCommand( JNIEnv & jni, const char * commandString )
{
#if defined( OVR_OS_ANDROID )
	if ( MatchesHead( "sound ", commandString ) )
	{
		PlayInternal( jni, commandString + 6 );
		return;
	}
	if ( MatchesHead( "stopSound ", commandString ) )
	{
		StopInternal( jni, commandString + 10 );
		return;
	}
	if ( MatchesHead( "loadSoundAsset ", commandString ) )
	{
		LoadSoundAssetInternal( jni, commandString + strlen( CMD_LOAD_SOUND_ASSET ) + 1 );
		return;
	}	
#else
	OVR_UNUSED( &jni );
	OVR_UNUSED( commandString );
#endif
}

