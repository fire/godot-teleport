// (C) Copyright 2018 Simul.co

#pragma once

#include <VrApi_Types.h>
#include <VrApi_Input.h>

#include "App.h"
#include "GuiSys.h"
#include "SceneView.h"
#include "SoundEffectContext.h"

#include <libavstream/libavstream.hpp>
#include <libavstream/geometrydecoder.hpp>
#include <libavstream/audiodecoder.h>
#include <libavstream/audio/audiotarget.h>

#include "crossplatform/GeometryDecoder.h"
#include "crossplatform/ResourceCreator.h"
#include "crossplatform/SessionClient.h"
#include "crossplatform/AudioStreamTarget.h"
#include <crossplatform/AudioPlayer.h>
#include <crossplatform/NetworkPipeline.h>

#include "GlobalGraphicsResources.h"
#include "VideoDecoderProxy.h"
#include "LobbyRenderer.h"
#include "ClientRenderer.h"
#include "Controllers.h"

namespace OVR
{
	class ovrLocale;
}

namespace scr
{
	class Texture;
}
class Application : public OVR::VrAppInterface, public SessionCommandInterface, public DecodeEventInterface, public ClientAppInterface
{
public:
	Application();

	virtual    ~Application();

	virtual void Configure(OVR::ovrSettings &settings) override;

	virtual void EnteredVrMode(
			const OVR::ovrIntentType intentType, const char *intentFromPackage,
			const char *intentJSON, const char *intentURI) override;

	virtual void LeavingVrMode() override;

	virtual bool
	OnKeyEvent(const int keyCode, const int repeatCount, const OVR::KeyEventType eventType);

	virtual OVR::ovrFrameResult Frame(const OVR::ovrFrameInput &vrFrame) override;


	class OVR::ovrLocale &GetLocale()
	{
		return *mLocale;
	}

	/* Begin SessionCommandInterface */
	virtual void OnVideoStreamChanged(const char* server_ip, const avs::SetupCommand& setupCommand, avs::Handshake& handshake) override;
	virtual void OnVideoStreamClosed() override;

	virtual void OnReconfigureVideo(const avs::ReconfigureVideoCommand& reconfigureVideoCommand) override;

	virtual bool OnActorEnteredBounds(avs::uid actor_uid) override;
	virtual bool OnActorLeftBounds(avs::uid actor_uid) override;

    virtual std::vector<avs::uid> GetGeometryResources() override;
    virtual void ClearGeometryResources() override;

    virtual void SetVisibleActors(const std::vector<avs::uid>& visibleActors) override;
    virtual void UpdateActorMovement(const std::vector<avs::MovementUpdate>& updateList) override;
	/* End SessionCommandInterface */

	/* Begin DecodeEventInterface */
	virtual void OnFrameAvailable() override;
	/* End DecodeEventInterface */

private:

	GlobalGraphicsResources& GlobalGraphicsResources = GlobalGraphicsResources::GetInstance();

	static void avsMessageHandler(avs::LogSeverity severity, const char *msg, void *);

	avs::Context  mContext;
	avs::Pipeline mPipeline;

	avs::Surface       mSurface;
	bool               mPipelineConfigured;

	static constexpr size_t NumVideoStreams = 1;
	static constexpr bool AudioStream = true;
	static constexpr bool   GeoStream  = true;

	GeometryDecoder        geometryDecoder;
	avs::GeometryDecoder   avsGeometryDecoder;
	avs::GeometryTarget    avsGeometryTarget;

	avs::AudioDecoder avsAudioDecoder;
	avs::AudioTarget avsAudioTarget;
	std::unique_ptr<sca::AudioStreamTarget> audioStreamTarget;
	sca::AudioPlayer* audioPlayer;
	std::unique_ptr<sca::NetworkPipeline> mNetworkPipeline;
	avs::Queue mAudioInputQueue;

	OVR::ovrSoundEffectContext        *mSoundEffectContext;
	OVR::OvrGuiSys::SoundEffectPlayer *mSoundEffectPlayer;

	OVR::OvrGuiSys *mGuiSys;
	OVR::ovrLocale *mLocale;

	OVR::OvrSceneView mScene;

	SessionClient                       sessionClient;

	std::vector<float> mRefreshRates;

	int mNumPendingFrames                  = 0;

	//Clientside Renderering Objects
	scc::GL_DeviceContext mDeviceContext;

	unsigned long long receivedInitialPos=0;
	unsigned long long receivedRelativePos=0;

    const scr::Effect::EffectPassCreateInfo *BuildEffectPass(const char* effectPassName, scr::VertexBufferLayout* vbl, const scr::ShaderSystem::PipelineCreateInfo*, const std::vector<scr::ShaderResource>& shaderResources) override;

	std::string LoadTextFile(const char *filename) override;
	ClientRenderer clientRenderer;
	scr::ResourceManagers resourceManagers;
	ResourceCreator resourceCreator;
	LobbyRenderer lobbyRenderer;
	Controllers controllers;

	std::string server_ip;
	int server_discovery_port=10600;
	ENetAddress remoteEndpoint;
};
