// (C) Copyright 2018-2019 Simul Software Ltd

#include "Application.h"
#include "Config.h"
#include "Input.h"
#include "VideoSurface.h"

#include "GuiSys.h"
#include "OVR_Locale.h"
#include "GLSLShaders.h"
#include "OVR_LogUtils.h"

#include <enet/enet.h>
#include <sstream>

#if defined( OVR_OS_WIN32 )
#include "../res_pc/resource.h"
#endif

using namespace OVR;

#if defined( OVR_OS_ANDROID )
extern "C" {

jlong Java_co_Simul_remoteplayclient_MainActivity_nativeSetAppInterface(JNIEnv* jni, jclass clazz, jobject activity,
		jstring fromPackageName, jstring commandString, jstring uriString )
{
	VideoDecoderProxy::InitializeJNI(jni);
	return (new Application())->SetActivity(jni, clazz, activity, fromPackageName, commandString, uriString);
}

} // extern "C"

#endif

Application::Application()
    : mDecoder(avs::DecoderBackend::Custom)
    , mPipelineConfigured(false)
	, mSoundEffectContext(nullptr)
	, mSoundEffectPlayer(nullptr)
	, mGuiSys(OvrGuiSys::Create())
	, mLocale(nullptr)
	, mVideoSurfaceTexture(nullptr)
	,mOvrMobile(nullptr)
    , mSession(this)
	, mControllerID(-1)
    , mIndexBufferManager(&scr::IndexBuffer::Destroy)
    , mShaderManager(nullptr)
    , mMaterialManager(nullptr)
    , mTextureManager(&scr::Texture::Destroy)
    , mUniformBufferManager(&scr::UniformBuffer::Destroy)
    , mVertexBufferManager(&scr::VertexBuffer::Destroy)


{
    memset(&renderConstants,0,sizeof(RenderConstants));
    renderConstants.colourOffsetScale={0.0f,0.0f,1.0f,0.6667f};
    renderConstants.depthOffsetScale={0.0f,0.6667f,0.5f,0.3333f};
	mContext.setMessageHandler(Application::avsMessageHandler, this);

	if(enet_initialize() != 0) {
		OVR_FAIL("Failed to initialize ENET library");
	}

    resourceCreator.SetRenderPlatform(dynamic_cast<scr::RenderPlatform*>(&renderPlatform));
    resourceCreator.AssociateResourceManagers(&mIndexBufferManager, &mShaderManager, &mMaterialManager, &mTextureManager, &mUniformBufferManager, &mVertexBufferManager);
    resourceCreator.AssociateActorManager(&mActorManger);
}

Application::~Application()
{
	mPipeline.deconfigure();

	mOvrMobile=nullptr;
	mRefreshRates.clear();
	delete mVideoSurfaceTexture;
	mVideoSurfaceDef.geo.Free();
	GlProgram::Free(mVideoSurfaceProgram);

	delete mSoundEffectPlayer;
	delete mSoundEffectContext;

	OvrGuiSys::Destroy(mGuiSys);

	mSession.Disconnect(REMOTEPLAY_TIMEOUT);
	enet_deinitialize();
}

void Application::Configure(ovrSettings& settings )
{
	settings.CpuLevel = 0;
	settings.GpuLevel = 2;

	settings.EyeBufferParms.colorFormat = COLOR_8888;
	settings.EyeBufferParms.depthFormat = DEPTH_16;
	settings.EyeBufferParms.multisamples = 1;
	settings.TrackingSpace=VRAPI_TRACKING_SPACE_LOCAL;
	//settings.TrackingTransform = VRAPI_TRACKING_TRANSFORM_SYSTEM_CENTER_EYE_LEVEL;
	settings.RenderMode = RENDERMODE_STEREO;
}

void Application::EnteredVrMode(const ovrIntentType intentType, const char* intentFromPackage, const char* intentJSON, const char* intentURI )
{

	if(intentType == INTENT_LAUNCH)
	{
		const ovrJava* java = app->GetJava();

		mOvrMobile=app->GetOvrMobile();

		mSoundEffectContext = new ovrSoundEffectContext(*java->Env, java->ActivityObject);
		mSoundEffectContext->Initialize(&app->GetFileSys());
		mSoundEffectPlayer = new OvrGuiSys::ovrDummySoundEffectPlayer();

		mLocale = ovrLocale::Create(*java->Env, java->ActivityObject, "default");

		std::string fontName;
		GetLocale().GetString("@string/font_name", "efigs.fnt", fontName);
		mGuiSys->Init(this->app, *mSoundEffectPlayer, fontName.c_str(), &app->GetDebugLines());

		{
            //-------------------------------------------------------------------------
            static ovrProgramParm uniformParms[] =	// both TextureMvpProgram and CubeMapPanoProgram use the same parm mapping
                                          {
                                                  { "colourOffsetScale",	ovrProgramParmType::FLOAT_VECTOR4 },
                                                  { "depthOffsetScale",		ovrProgramParmType::FLOAT_VECTOR4 },
                                                  { "videoFrameTexture",	ovrProgramParmType::TEXTURE_SAMPLED },
                                          };
			mVideoSurfaceProgram = GlProgram::Build(nullptr, shaders::VideoSurface_VS,
													shaders::VideoSurface_OPTIONS, shaders::VideoSurface_FS,
													uniformParms, sizeof( uniformParms ) / sizeof( ovrProgramParm ),
													310);
			if(!mVideoSurfaceProgram.IsValid()) {
				OVR_FAIL("Failed to build video surface shader program");
			}
		}

		mDecoder.setBackend(new VideoDecoderProxy(java->Env, this, avs::VideoCodec::HEVC));

		mVideoSurfaceTexture = new OVR::SurfaceTexture(java->Env);
        mVideoTexture = GlTexture(mVideoSurfaceTexture->GetTextureId(), GL_TEXTURE_EXTERNAL_OES, 0, 0);

		mVideoSurfaceDef.surfaceName = "VideoSurface";
		mVideoSurfaceDef.geo = BuildGlobe();
		mVideoSurfaceDef.graphicsCommand.Program = mVideoSurfaceProgram;
		mVideoSurfaceDef.graphicsCommand.GpuState.depthEnable = false;
		mVideoSurfaceDef.graphicsCommand.GpuState.cullEnable = false;

		int num_refresh_rates=vrapi_GetSystemPropertyInt(java,VRAPI_SYS_PROP_NUM_SUPPORTED_DISPLAY_REFRESH_RATES);
		mRefreshRates.resize(num_refresh_rates);
		vrapi_GetSystemPropertyFloatArray(java,VRAPI_SYS_PROP_SUPPORTED_DISPLAY_REFRESH_RATES,mRefreshRates.data(),num_refresh_rates);

		if(num_refresh_rates>0)
			vrapi_SetDisplayRefreshRate(mOvrMobile,mRefreshRates[num_refresh_rates-1]);
	}
}

void Application::LeavingVrMode()
{
}

bool Application::OnKeyEvent(const int keyCode, const int repeatCount, const KeyEventType eventType)
{
	if(mGuiSys->OnKeyEvent(keyCode, repeatCount, eventType))
	{
		return true;
	}
	return false;
}

ovrFrameResult Application::Frame(const ovrFrameInput& vrFrame)
{
	// process input events first because this mirrors the behavior when OnKeyEvent was
	// a virtual function on VrAppInterface and was called by VrAppFramework.
	for(int i = 0; i < vrFrame.Input.NumKeyEvents; i++)
	{
		const int keyCode = vrFrame.Input.KeyEvents[i].KeyCode;
		const int repeatCount = vrFrame.Input.KeyEvents[i].RepeatCount;
		const KeyEventType eventType = vrFrame.Input.KeyEvents[i].EventType;

		if(OnKeyEvent(keyCode, repeatCount, eventType))
		{
			continue;   // consumed the event
		}
		// If nothing consumed the key and it's a short-press of the back key, then exit the application to OculusHome.
		if(keyCode == OVR_KEY_BACK && eventType == KEY_EVENT_SHORT_PRESS)
		{
		    app->ShowConfirmQuitSystemUI();
			continue;
		}
	}

	// Try to find remote controller
	if((int)mControllerID == -1) {
		InitializeController();
	}

    // Query controller input state.
    ControllerState controllerState = {};
    if((int)mControllerID != -1)
    {
		ovrInputStateTrackedRemote ovrState;
		ovrState.Header.ControllerType = ovrControllerType_TrackedRemote;
		if(vrapi_GetCurrentInputState(mOvrMobile, mControllerID, &ovrState.Header) >= 0)
		{
			controllerState.mButtons = ovrState.Buttons;
			controllerState.mTrackpadStatus = ovrState.TrackpadStatus > 0;
			controllerState.mTrackpadX = ovrState.TrackpadPosition.x / mTrackpadDim.x;
			controllerState.mTrackpadY = ovrState.TrackpadPosition.y / mTrackpadDim.y;
		}
    }

	// Handle networked session.
	if(mSession.IsConnected())
	{
		mSession.Frame(vrFrame, controllerState);
	}
	else
    {
		ENetAddress remoteEndpoint;
		if(mSession.Discover(REMOTEPLAY_DISCOVERY_PORT, remoteEndpoint))
		{
			mSession.Connect(remoteEndpoint, REMOTEPLAY_TIMEOUT);
		}
	}

	// Update video texture if we have any pending decoded frames.
	while(mNumPendingFrames > 0)
	{
		mVideoSurfaceTexture->Update();
		--mNumPendingFrames;
	}

	// Process stream pipeline
	mPipeline.process();

	ovrFrameResult res;

	mScene.Frame(vrFrame);
	mScene.GetFrameMatrices(vrFrame.FovX, vrFrame.FovY, res.FrameMatrices);
	mScene.GenerateFrameSurfaceList(res.FrameMatrices, res.Surfaces);

	// Update GUI systems after the app frame, but before rendering anything.
	mGuiSys->Frame(vrFrame, res.FrameMatrices.CenterView);

    static float frameRate=1.0f;
    if(vrFrame.DeltaSeconds>0.0f)
    {
        frameRate*=0.99f;
        frameRate+=0.01f/vrFrame.DeltaSeconds;
    }
	if(!mSession.IsConnected())
	{
		mGuiSys->ShowInfoText(
				1.0f,
				"Waiting for connection\nFramerate: %4.4f", frameRate);
	}
    else
	{
#ifdef _DEBUG
		auto ctr = mNetworkSource.getCounterValues();
		mGuiSys->ShowInfoText(
				1.0f,
				"Network Packets Dropped: %d    \nDecoder Packets Dropped: %d\nFramerate: %4.4f\nBandwidth: %4.4f",
				ctr.networkPacketsDropped, ctr.decoderPacketsDropped, frameRate, ctr.bandwidthKPS);
#endif
	}
	res.FrameIndex   = vrFrame.FrameNumber;
	res.DisplayTime  = vrFrame.PredictedDisplayTimeInSeconds;
	res.SwapInterval = app->GetSwapInterval();

	res.FrameFlags = 0;
	res.LayerCount = 0;

	ovrLayerProjection2& worldLayer = res.Layers[res.LayerCount++].Projection;

	worldLayer = vrapi_DefaultLayerProjection2();
	worldLayer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_CHROMATIC_ABERRATION_CORRECTION;
	worldLayer.HeadPose = vrFrame.Tracking.HeadPose;
	for(int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++)
	{
		worldLayer.Textures[eye].ColorSwapChain = vrFrame.ColorTextureSwapChain[eye];
		worldLayer.Textures[eye].SwapChainIndex = vrFrame.TextureSwapChainIndex;
		worldLayer.Textures[eye].TexCoordsFromTanAngles = vrFrame.TexCoordsFromTanAngles;
	}

	// Append video surface
    mVideoSurfaceDef.graphicsCommand.UniformData[0].Data = &renderConstants.colourOffsetScale;
    mVideoSurfaceDef.graphicsCommand.UniformData[1].Data = &renderConstants.depthOffsetScale;
    mVideoSurfaceDef.graphicsCommand.UniformData[2].Data = &mVideoTexture;
	res.Surfaces.push_back(ovrDrawSurface(&mVideoSurfaceDef));

	// Append GuiSys surfaces.
	mGuiSys->AppendSurfaceList(res.FrameMatrices.CenterView, &res.Surfaces);

	GL_CheckErrors("Frame");
	return res;
}

bool Application::InitializeController()
{
	ovrInputCapabilityHeader inputCapsHeader;
	for(uint32_t i = 0;
		vrapi_EnumerateInputDevices(mOvrMobile, i, &inputCapsHeader) == 0; ++i) {
		if(inputCapsHeader.Type == ovrControllerType_TrackedRemote) {
			mControllerID = inputCapsHeader.DeviceID;
			break;
		}
	}

	if((int)mControllerID != -1) {
		OVR_LOG("Found GearVR controller (ID: %x)", mControllerID);

		ovrInputTrackedRemoteCapabilities trackedInputCaps;
		trackedInputCaps.Header = inputCapsHeader;
		vrapi_GetInputDeviceCapabilities(mOvrMobile, &trackedInputCaps.Header);
		mTrackpadDim.x = trackedInputCaps.TrackpadMaxX;
		mTrackpadDim.y = trackedInputCaps.TrackpadMaxY;
		return true;
	}
	return false;
}

void Application::OnVideoStreamChanged(const avs::SetupCommand &setupCommand)
{
	if(mPipelineConfigured) {
		// TODO: Fix!
		return;
	}

    OVR_WARN("VIDEO STREAM CHANGED: %d %d %d", setupCommand.port, setupCommand.video_width, setupCommand.video_height);

	avs::NetworkSourceParams sourceParams = {};
	sourceParams.socketBufferSize = 64 * 1024 * 1024; // 64MiB socket buffer size
	//sourceParams.gcTTL = (1000/60) * 4; // TTL = 4 * expected frame time
	sourceParams.maxJitterBufferLength = 0;


	if(!mNetworkSource.configure(NumStreams + (GeoStream?1:0), setupCommand.port+1, mSession.GetServerIP().c_str(), setupCommand.port, sourceParams)) {
		OVR_WARN("OnVideoStreamChanged: Failed to configure network source node");
		return;
	}

	avs::DecoderParams decoderParams = {};
	decoderParams.codec = avs::VideoCodec::HEVC;
	decoderParams.decodeFrequency = avs::DecodeFrequency::NALUnit;
	decoderParams.prependStartCodes = false;
	decoderParams.deferDisplay = false;
	size_t stream_width=std::max(setupCommand.video_width,setupCommand.depth_width);
	size_t stream_height=setupCommand.video_height+setupCommand.depth_height;
	if(!mDecoder.configure(avs::DeviceHandle(), stream_width, stream_height, decoderParams, 50))
	{
		OVR_WARN("OnVideoStreamChanged: Failed to configure decoder node");
		mNetworkSource.deconfigure();
		return;
	}

    renderConstants.colourOffsetScale.x=0;
    renderConstants.colourOffsetScale.y = 0;
    renderConstants.colourOffsetScale.z = 1.0f;
    renderConstants.colourOffsetScale.w = float(setupCommand.video_height) / float(stream_height);

    renderConstants.depthOffsetScale.x = 0;
    renderConstants.depthOffsetScale.y = float(setupCommand.video_height) / float(stream_height);
    renderConstants.depthOffsetScale.z = float(setupCommand.depth_width) / float(stream_width);
    renderConstants.depthOffsetScale.w = float(setupCommand.depth_height) / float(stream_height);

	mSurface.configure(new VideoSurface(mVideoSurfaceTexture));

	mPipeline.link({&mNetworkSource, &mDecoder, &mSurface});

   //TODO: We will add a GEOMETRY PIPE:
   if(GeoStream)
   {
        avsGeometryDecoder.configure(100, &geometryDecoder);
	    avsGeometryTarget.configure(&resourceCreator);
        mPipeline.link({ &mNetworkSource, &avsGeometryDecoder, &avsGeometryTarget });
   }

   mPipelineConfigured = true;
}

void Application::OnVideoStreamClosed()
{
    OVR_WARN("VIDEO STREAM CLOSED");

	mPipeline.deconfigure();
	mPipeline.reset();
	mPipelineConfigured = false;
}

void Application::OnFrameAvailable()
{
	++mNumPendingFrames;
}

void Application::avsMessageHandler(avs::LogSeverity severity, const char* msg, void*)
{
	switch(severity)
	{
		case avs::LogSeverity::Error:
		case avs::LogSeverity::Warning:
		if(msg)
        {
            static std::ostringstream ostr;
            while((*msg)!=0&&(*msg)!='\n')
            {
                ostr<<(*msg);
                msg++;
            }
            if(*msg=='\n')
            {
                OVR_WARN("%s", ostr.str().c_str());
                ostr.str("");
                ostr.clear();
            }
            break;
        }
		case avs::LogSeverity::Critical:
			OVR_FAIL("%s", msg);
		default:
            if(msg)
            {
                static std::ostringstream ostr;
                while((*msg)!=0&&(*msg)!='\n')
                {
                    ostr<<(*msg);
                    msg++;
                }
                if(*msg=='\n')
                {
                    OVR_LOG("%s", ostr.str().c_str());
					ostr.str("");
                    ostr.clear();
                }
                break;
            }
			break;
	}
}
