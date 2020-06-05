// (C) Copyright 2018-2019 Simul Software Ltd
#include "Application.h"

#include <sstream>

#include "GLESDebug.h"
#include "GuiSys.h"
#include "OVR_FileSys.h"
#include "OVR_GlUtils.h"
#include "OVR_Locale.h"
#include "OVR_LogUtils.h"
#include "OVR_Math.h"

#include "enet/enet.h"

#include "AndroidDiscoveryService.h"
#include "Config.h"
#include "Log.h"
#include "OVRActorManager.h"
#include "VideoSurface.h"

#if defined( OVR_OS_WIN32 )
#include "../res_pc/resource.h"
#endif

using namespace OVR;


#if defined( OVR_OS_ANDROID )
extern "C"
{
	jlong Java_co_Simul_remoteplayclient_MainActivity_nativeSetAppInterface(JNIEnv* jni, jclass clazz, jobject activity,
			jstring fromPackageName, jstring commandString, jstring uriString )
	{
		VideoDecoderProxy::InitializeJNI(jni);
		return (new Application())->SetActivity(jni, clazz, activity, fromPackageName, commandString, uriString);
	}
} // extern "C"

#endif

ovrQuatf QuaternionMultiply(const ovrQuatf &p,const ovrQuatf &q)
{
	ovrQuatf r;
	r.w= p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z;
	r.x= p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y;
	r.y= p.w * q.y + p.y * q.w + p.z * q.x - p.x * q.z;
	r.z= p.w * q.z + p.z * q.w + p.x * q.y - p.y * q.x;
	return r;
}

Application::Application()
	: mPipelineConfigured(false)
	, mSoundEffectContext(nullptr)
	, mSoundEffectPlayer(nullptr)
	, mGuiSys(OvrGuiSys::Create())
	, mLocale(nullptr)
	, mSession(this, std::make_unique<AndroidDiscoveryService>())
	, mDeviceContext(dynamic_cast<scr::RenderPlatform*>(&GlobalGraphicsResources.renderPlatform))
	,clientRenderer(&resourceCreator,&resourceManagers,this,this)
	, resourceManagers(new OVRActorManager)
	,resourceCreator(basist::transcoder_texture_format::cTFETC2)
{
	RedirectStdCoutCerr();
	mSession.SetResourceCreator(&resourceCreator);

	pthread_setname_np(pthread_self(), "SimulCaster_Application");
	memset(&renderConstants,0,sizeof(RenderConstants));
	renderConstants.colourOffsetScale={0.0f,0.0f,1.0f,0.6667f};
	renderConstants.depthOffsetScale={0.0f,0.6667f,0.5f,0.3333f};
	mContext.setMessageHandler(Application::avsMessageHandler, this);

	if(enet_initialize() != 0) {
		OVR_FAIL("Failed to initialize ENET library");
	}

	resourceCreator.Initialise(dynamic_cast<scr::RenderPlatform*>(&GlobalGraphicsResources.renderPlatform), scr::VertexBufferLayout::PackingStyle::INTERLEAVED);
	resourceCreator.AssociateResourceManagers(&resourceManagers.mIndexBufferManager, &resourceManagers.mShaderManager, &resourceManagers.mMaterialManager, &resourceManagers.mTextureManager, &resourceManagers.mUniformBufferManager, &resourceManagers.mVertexBufferManager, &resourceManagers.mMeshManager, &resourceManagers.mLightManager);
	resourceCreator.AssociateActorManager(resourceManagers.mActorManager.get());

	//Default Effects
	scr::Effect::EffectCreateInfo ci;
	ci.effectName = "StandardEffects";
	GlobalGraphicsResources.pbrEffect.Create(&ci);

	//Default Sampler
	scr::Sampler::SamplerCreateInfo sci  = {};
	sci.wrapU = scr::Sampler::Wrap::REPEAT;
	sci.wrapV = scr::Sampler::Wrap::REPEAT;
	sci.wrapW = scr::Sampler::Wrap::REPEAT;
	sci.minFilter = scr::Sampler::Filter::LINEAR;
	sci.magFilter = scr::Sampler::Filter::LINEAR;

	GlobalGraphicsResources.sampler = GlobalGraphicsResources.renderPlatform.InstantiateSampler();
	GlobalGraphicsResources.sampler->Create(&sci);

	sci.minFilter = scr::Sampler::Filter::MIPMAP_LINEAR;
	GlobalGraphicsResources.cubeMipMapSampler = GlobalGraphicsResources.renderPlatform.InstantiateSampler();
	GlobalGraphicsResources.cubeMipMapSampler->Create(&sci);
}

Application::~Application()
{
	mPipeline.deconfigure();

	mRefreshRates.clear();
	clientRenderer.ExitedVR();
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
	if(intentType != INTENT_LAUNCH)
		return;

	OVR_LOG("%s | %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &GlobalGraphicsResources.maxFragTextureSlots);
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &GlobalGraphicsResources.maxFragUniformBlocks);
	OVR_LOG("Fragment Texture Slots: %d, Fragment Uniform Blocks: %d", GlobalGraphicsResources.maxFragTextureSlots, GlobalGraphicsResources.maxFragUniformBlocks);

    //Setup Debug
    scc::SetupGLESDebug();

	const ovrJava *java=app->GetJava();
	mSoundEffectContext=new ovrSoundEffectContext(*java->Env, java->ActivityObject);
	mSoundEffectContext->Initialize(&app->GetFileSys());
	mSoundEffectPlayer=new OvrGuiSys::ovrDummySoundEffectPlayer();

	mLocale=ovrLocale::Create(*java->Env, java->ActivityObject, "default");
	std::string fontName;
	GetLocale().GetString("@string/font_name", "efigs.fnt", fontName);

	clientRenderer.EnteredVR(app->GetOvrMobile(),java);
	mGuiSys->Init(app, *mSoundEffectPlayer, fontName.c_str(), &app->GetDebugLines());

	clientRenderer.mDecoder.setBackend(new VideoDecoderProxy(java->Env, this));


	//Set Lighting Cubemap Shader Resource
    scr::ShaderResourceLayout lightingCubemapLayout;
    lightingCubemapLayout.AddBinding(13, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, scr::Shader::Stage::SHADER_STAGE_FRAGMENT);
    lightingCubemapLayout.AddBinding(14, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, scr::Shader::Stage::SHADER_STAGE_FRAGMENT);
	lightingCubemapLayout.AddBinding(15, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, scr::Shader::Stage::SHADER_STAGE_FRAGMENT);
	lightingCubemapLayout.AddBinding(16, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, scr::Shader::Stage::SHADER_STAGE_FRAGMENT);

    GlobalGraphicsResources.lightCubemapShaderResources.SetLayouts({lightingCubemapLayout});
	GlobalGraphicsResources.lightCubemapShaderResources.AddImage(0, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 13, "u_DiffuseCubemap", {clientRenderer.mDiffuseTexture->GetSampler(), clientRenderer.mDiffuseTexture});
	GlobalGraphicsResources.lightCubemapShaderResources.AddImage(0, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 14, "u_SpecularCubemap", {clientRenderer.mSpecularTexture->GetSampler(), clientRenderer.mSpecularTexture});
	GlobalGraphicsResources.lightCubemapShaderResources.AddImage(0, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 15, "u_RoughSpecularCubemap", {clientRenderer.mRoughSpecularTexture->GetSampler(), clientRenderer.mRoughSpecularTexture});
	GlobalGraphicsResources.lightCubemapShaderResources.AddImage(0, scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 16, "u_LightsCubemap", {clientRenderer.mCubemapLightingTexture->GetSampler(), clientRenderer.mCubemapLightingTexture});

	int num_refresh_rates=vrapi_GetSystemPropertyInt(java,VRAPI_SYS_PROP_NUM_SUPPORTED_DISPLAY_REFRESH_RATES);
	mRefreshRates.resize(num_refresh_rates);
	vrapi_GetSystemPropertyFloatArray(java,VRAPI_SYS_PROP_SUPPORTED_DISPLAY_REFRESH_RATES,mRefreshRates.data(),num_refresh_rates);

	if(num_refresh_rates>0)
		vrapi_SetDisplayRefreshRate(app->GetOvrMobile(),mRefreshRates[num_refresh_rates-1]);

	// Bind the delegates.

	controllers.SetToggleTexturesDelegate(std::bind(&ClientRenderer::ToggleTextures,&clientRenderer));
	controllers.SetToggleShowInfoDelegate(std::bind(&ClientRenderer::ToggleShowInfo,&clientRenderer));
	controllers.SetSetStickOffsetDelegate(std::bind(&ClientRenderer::SetStickOffset,&clientRenderer,std::placeholders::_1,std::placeholders::_2));
}

void Application::LeavingVrMode()
{
}

bool Application::OnKeyEvent(const int keyCode, const int repeatCount, const KeyEventType eventType)
{
    return mGuiSys->OnKeyEvent(keyCode, repeatCount, eventType);
}

extern ovrQuatf QuaternionMultiply(const ovrQuatf &p,const ovrQuatf &q);

ovrFrameResult Application::Frame(const ovrFrameInput& vrFrame)
{
    GL_CheckErrors("Frame: Start");
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
	if((int)controllers.mControllerID == 0)
	{
		controllers.InitializeController(app->GetOvrMobile());
	}
	controllers.Update(app->GetOvrMobile());
	ovrVector3f footPos=mScene.GetFootPos();

	//Get HMD Position/Orientation
	scr::vec3 headPos =*((const scr::vec3*)&vrFrame.Tracking.HeadPose.Pose.Position);
	headPos+=*((scr::vec3*)&footPos);
	//headPos*=10.0f;
	scr::vec3 scr_OVR_headPos = {headPos.x, headPos.y, headPos.z};

	//Get the Capture Position
	scr::Transform::TransformCreateInfo tci = {(scr::RenderPlatform*)(&GlobalGraphicsResources.renderPlatform)};
	scr::Transform scr_UE4_captureTransform(tci);
	avs::Transform avs_UE4_captureTransform = clientRenderer.mDecoder.getCameraTransform();
	scr_UE4_captureTransform = avs_UE4_captureTransform;

	if(clientRenderer.mDecoder.hasValidTransform())
	{
		if (!receivedInitialPos)
		{
			// Oculus Origin means where the headset's zero is in real space.
			clientRenderer.oculusOrigin = scr_UE4_captureTransform.m_Translation;
			receivedInitialPos = true;
		}
	}
	if(!receivedInitialPos)
	{
		scr_UE4_captureTransform = avs::Transform();
		clientRenderer.oculusOrigin = scr_UE4_captureTransform.m_Translation;
	}

	clientRenderer.cameraPosition = clientRenderer.oculusOrigin+scr_OVR_headPos;

	// Handle networked session.
	if(mSession.IsConnected())
	{
		avs::DisplayInfo displayInfo = {1440, 1600};
		clientRenderer.headPose.orientation=*((avs::vec4*)(&vrFrame.Tracking.HeadPose.Pose.Orientation));
		clientRenderer.headPose.position = {clientRenderer.cameraPosition.x, clientRenderer.cameraPosition.y, clientRenderer.cameraPosition.z};

		mSession.Frame(displayInfo, clientRenderer.headPose, clientRenderer.controllerPoses, receivedInitialPos, controllers.mLastPrimaryControllerState, clientRenderer.mDecoder.idrRequired());
		if (!receivedInitialPos&&mSession.receivedInitialPos)
		{
			clientRenderer.oculusOrigin = mSession.GetInitialPos();
			receivedInitialPos = true;
		}
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
		clientRenderer.mVideoSurfaceTexture->Update();
		--mNumPendingFrames;
	}

	// Process stream pipeline
	mPipeline.process();

	//Build frame
	ovrFrameResult res;

	mScene.Frame(vrFrame);
	mScene.GetFrameMatrices(vrFrame.FovX, vrFrame.FovY, res.FrameMatrices);
	mScene.GenerateFrameSurfaceList(res.FrameMatrices, res.Surfaces);

	// Update GUI systems after the app frame, but before rendering anything.
	mGuiSys->Frame(vrFrame, res.FrameMatrices.CenterView);
	scr::vec3 camera_from_videoCentre=clientRenderer.cameraPosition-scr_UE4_captureTransform.m_Translation;
	// The camera should be where our head is. But when rendering, the camera is in OVR space, so:
	GlobalGraphicsResources.scrCamera->UpdatePosition(scr_OVR_headPos);


	Quat<float> headPose = vrFrame.Tracking.HeadPose.Pose.Orientation;
	ovrQuatf X0={1.0f,0.f,0.f,0.0f};
	ovrQuatf headPoseC={-headPose.x,-headPose.y,-headPose.z,headPose.w};
	ovrQuatf xDir= QuaternionMultiply(QuaternionMultiply(headPose,X0),headPoseC);

    std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
	if(mSession.IsConnected())
	{
		clientRenderer.Render(vrFrame,mGuiSys);
	}
	else
	{
		res.ClearColorBuffer=true;
		res.ClearDepthBuffer=true;
		lobbyRenderer.Render(mGuiSys);
	};
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

	GL_CheckErrors("Frame: Pre-Cubemap");
	clientRenderer.CopyToCubemaps(mDeviceContext);
	// Append video surface
	{
		ovrMatrix4f eye0=res.FrameMatrices.EyeView[ 0 ];
		eye0.M[0][3]=0.0f;
		eye0.M[1][3]=0.0f;
		eye0.M[2][3]=0.0f;
		ovrMatrix4f viewProj0=res.FrameMatrices.EyeProjection[ 0 ]*eye0;
		ovrMatrix4f viewProjT0=ovrMatrix4f_Transpose( &viewProj0 );
		clientRenderer.videoUB.invViewProj[0]=ovrMatrix4f_Inverse( &viewProjT0 );
		ovrMatrix4f eye1=res.FrameMatrices.EyeView[ 1 ];
		eye1.M[0][3]=0.0f;
		eye1.M[1][3]=0.0f;
		eye1.M[2][3]=0.0f;
		ovrMatrix4f viewProj1=res.FrameMatrices.EyeProjection[ 1 ]*eye1;
		ovrMatrix4f viewProjT1=ovrMatrix4f_Transpose( &viewProj1 );
		clientRenderer.videoUB.invViewProj[1]=ovrMatrix4f_Inverse( &viewProjT1 );
	}
	clientRenderer.mVideoSurfaceDef.graphicsCommand.UniformData[4].Data =  &(((scc::GL_UniformBuffer *)  clientRenderer.mVideoUB.get())->GetGlBuffer());
	if(clientRenderer.mCubemapTexture->IsValid())
	{
		float w=vrFrame.IPD/2.0f;//.04f; //half separation.
		scr::vec4 eye={w*xDir.x,w*xDir.y,w*xDir.z,0.0f};
		scr::vec3 &v=camera_from_videoCentre;
		scr::vec4 right_eye ={v.x+eye.x,v.y+eye.y,v.z+eye.z,0.0f};
		scr::vec4 left_eye ={-eye.x,-eye.y,-eye.z,0.0f};
		clientRenderer.videoUB.eyeOffsets[0]=left_eye;		// left eye
		clientRenderer.videoUB.eyeOffsets[1]=eye;	// right eye.
		clientRenderer.videoUB.cameraPosition=clientRenderer.cameraPosition;

		clientRenderer.mVideoSurfaceDef.graphicsCommand.UniformData[2].Data = &(((scc::GL_Texture *) clientRenderer.mCubemapTexture.get())->GetGlTexture());
		clientRenderer.mVideoSurfaceDef.graphicsCommand.UniformData[3].Data = &(((scc::GL_Texture *)  clientRenderer.mVideoTexture.get())->GetGlTexture());
		res.Surfaces.push_back(ovrDrawSurface(&clientRenderer.mVideoSurfaceDef));
	}
	clientRenderer.mVideoUB->Submit();

	//Move the hands before they are drawn.
	clientRenderer.UpdateHandObjects();
	//Append SCR Actors to surfaces.
	GL_CheckErrors("Frame: Pre-SCR");
	uint32_t time_elapsed=(uint32_t)(vrFrame.DeltaSeconds*1000.0f);
	resourceManagers.Update(time_elapsed);
	resourceCreator.Update(time_elapsed);
	clientRenderer.RenderLocalActors(res);
	GL_CheckErrors("Frame: Post-SCR");

	// Append GuiSys surfaces. This should always be the last item to append the render list.
	mGuiSys->AppendSurfaceList(res.FrameMatrices.CenterView, &res.Surfaces);

	return res;
}

void Application::OnVideoStreamChanged(const avs::SetupCommand& setupCommand, avs::Handshake& handshake)
{
	if(!mPipelineConfigured)
    {
		OVR_WARN("VIDEO STREAM CHANGED: %d %d %d, cubemap %d", setupCommand.port,
				 setupCommand.video_width, setupCommand.video_height,
				 setupCommand.colour_cubemap_size);

		avs::NetworkSourceParams sourceParams = {};
		sourceParams.socketBufferSize      = 3 * 1024 * 1024; // 3 Mb socket buffer size
		//sourceParams.gcTTL = (1000/60) * 4; // TTL = 4 * expected frame time
		sourceParams.maxJitterBufferLength = 0;


		if (!clientRenderer.mNetworkSource.configure(
				NumStreams + (GeoStream ? 1 : 0), setupCommand.port + 1,
				mSession.GetServerIP().c_str(), setupCommand.port, sourceParams))
		{
			OVR_WARN("OnVideoStreamChanged: Failed to configure network source node");
			return;
		}
	    clientRenderer.mNetworkSource.setDebugStream(setupCommand.debug_stream);
	    clientRenderer.mNetworkSource.setDebugNetworkPackets(setupCommand.debug_network_packets);
	    clientRenderer.mNetworkSource.setDoChecksums(setupCommand.do_checksums);
		avs::DecoderParams decoderParams = {};
		decoderParams.codec             = setupCommand.videoCodec;
		decoderParams.decodeFrequency   = avs::DecodeFrequency::NALUnit;
		decoderParams.prependStartCodes = false;
		decoderParams.deferDisplay      = false;

		size_t stream_width  = setupCommand.video_width;
		size_t stream_height = setupCommand.video_height;
		if (!clientRenderer.mDecoder.configure(avs::DeviceHandle(), stream_width, stream_height, decoderParams, 50))
		{
			OVR_WARN("OnVideoStreamChanged: Failed to configure decoder node");
			clientRenderer.mNetworkSource.deconfigure();
			return;
		}
		{
			scr::Texture::TextureCreateInfo textureCreateInfo = {};
			textureCreateInfo.externalResource = true;
			textureCreateInfo.slot   = scr::Texture::Slot::NORMAL;
			textureCreateInfo.format = scr::Texture::Format::RGBA8;
			textureCreateInfo.type   = scr::Texture::Type::TEXTURE_2D_EXTERNAL_OES;
			textureCreateInfo.height = setupCommand.video_height;
			textureCreateInfo.width  = setupCommand.video_width;

			clientRenderer.mVideoTexture->Create(textureCreateInfo);
			((scc::GL_Texture *) (clientRenderer.mVideoTexture.get()))->SetExternalGlTexture(
					clientRenderer.mVideoSurfaceTexture->GetTextureId());

		}
		renderConstants.colourOffsetScale.x = 0;
		renderConstants.colourOffsetScale.y = 0;
		renderConstants.colourOffsetScale.z = 1.0f;
		renderConstants.colourOffsetScale.w =
				float(setupCommand.video_height) / float(stream_height);

		renderConstants.depthOffsetScale.x = 0;
		renderConstants.depthOffsetScale.y =
				float(setupCommand.video_height) / float(stream_height);
		renderConstants.depthOffsetScale.z = float(setupCommand.depth_width) / float(stream_width);
		renderConstants.depthOffsetScale.w =
				float(setupCommand.depth_height) / float(stream_height);

		mSurface.configure(new VideoSurface(clientRenderer.mVideoSurfaceTexture));

		mPipeline.link({&clientRenderer.mNetworkSource, &clientRenderer.mDecoder, &mSurface});

		if (GeoStream)
		{
			avsGeometryDecoder.configure(100, &geometryDecoder);
			avsGeometryTarget.configure(&resourceCreator);
			mPipeline.link({&clientRenderer.mNetworkSource, &avsGeometryDecoder, &avsGeometryTarget});
		}
		//GL_CheckErrors("Pre-Build Cubemap");
		//Build Video Cubemap
		{
			scr::Texture::TextureCreateInfo textureCreateInfo =
													{
															setupCommand.colour_cubemap_size,
															setupCommand.colour_cubemap_size,
															1,
															4,
															1,
															1,
															scr::Texture::Slot::UNKNOWN,
															scr::Texture::Type::TEXTURE_CUBE_MAP,
															scr::Texture::Format::RGBA8,
															scr::Texture::SampleCountBit::SAMPLE_COUNT_1_BIT,
															{},
															{},
															scr::Texture::CompressionFormat::UNCOMPRESSED
													};
			clientRenderer.mCubemapTexture->Create(textureCreateInfo);
			clientRenderer.mCubemapTexture->UseSampler(GlobalGraphicsResources.cubeMipMapSampler);
		}
		//GL_CheckErrors("Built Video Cubemap");
		//Build Lighting Cubemap
		{
			scr::Texture::TextureCreateInfo textureCreateInfo //TODO: Check this against the incoming texture from the video stream
													{
															128,
															128,
															1,
															4,
															1,
															3,
															scr::Texture::Slot::UNKNOWN,
															scr::Texture::Type::TEXTURE_CUBE_MAP,
															scr::Texture::Format::RGBA8,
															scr::Texture::SampleCountBit::SAMPLE_COUNT_1_BIT,
															{},
															{},
															scr::Texture::CompressionFormat::UNCOMPRESSED
													};
			textureCreateInfo.mipCount = 1;
			textureCreateInfo.width  = clientRenderer.diffuseSize;
			textureCreateInfo.height = clientRenderer.diffuseSize;
			clientRenderer.mDiffuseTexture->Create(textureCreateInfo);
			textureCreateInfo.width  = clientRenderer.lightSize;
			textureCreateInfo.height = clientRenderer.lightSize;
			clientRenderer.mCubemapLightingTexture->Create(textureCreateInfo);
			textureCreateInfo.mipCount = 3;
			textureCreateInfo.width  = clientRenderer.specularSize;
			textureCreateInfo.height = clientRenderer.specularSize;
			clientRenderer.mSpecularTexture->Create(textureCreateInfo);
			clientRenderer.mRoughSpecularTexture->Create(textureCreateInfo);
			clientRenderer.mDiffuseTexture->UseSampler(GlobalGraphicsResources.cubeMipMapSampler);
			clientRenderer.mSpecularTexture->UseSampler(GlobalGraphicsResources.cubeMipMapSampler);
			clientRenderer.mRoughSpecularTexture->UseSampler(GlobalGraphicsResources.cubeMipMapSampler);
			clientRenderer.mCubemapLightingTexture->UseSampler(GlobalGraphicsResources.cubeMipMapSampler);
		}
		//GL_CheckErrors("Built Lighting Cubemap");

		mPipelineConfigured = true;
	}

    handshake.startDisplayInfo.width = 1440;
    handshake.startDisplayInfo.height = 1600;
    handshake.framerate = 60;
    handshake.FOV = 110;
    handshake.isVR = true;
	handshake.udpBufferSize = static_cast<uint32_t>(clientRenderer.mNetworkSource.getSystemBufferSize());
	handshake.maxBandwidthKpS = 10*handshake.udpBufferSize * static_cast<uint32_t>(handshake.framerate);
	handshake.axesStandard = avs::AxesStandard::GlStyle;
	handshake.MetresPerUnit = 1.0f;
	handshake.usingHands = true;
}

void Application::OnVideoStreamClosed()
{
	OVR_WARN("VIDEO STREAM CLOSED");

	mPipeline.deconfigure();
	mPipeline.reset();
	mPipelineConfigured = false;

    receivedInitialPos = false;
}

void Application::OnReconfigureVideo(const avs::ReconfigureVideoCommand& reconfigureVideoCommand)
{
	if(!mPipelineConfigured)
	{
		return;
	}

    const avs::VideoConfig& videoConfig = reconfigureVideoCommand.video_config;

    WARN("VIDEO STREAM RECONFIGURED: clr %d x %d dpth %d x %d", videoConfig.video_width, videoConfig.video_height
    , videoConfig.depth_width, videoConfig.depth_height);
}

bool Application::OnActorEnteredBounds(avs::uid actor_uid)
{
    return resourceManagers.mActorManager->ShowActor(actor_uid);
}

bool Application::OnActorLeftBounds(avs::uid actor_uid)
{
	return resourceManagers.mActorManager->HideActor(actor_uid);
}

std::vector<uid> Application::GetGeometryResources()
{
    return resourceManagers.GetAllResourceIDs();
}

void Application::ClearGeometryResources()
{
    resourceManagers.Clear();
}

void Application::SetVisibleActors(const std::vector<avs::uid>& visibleActors)
{
    resourceManagers.mActorManager->SetVisibleActors(visibleActors);
}

void Application::UpdateActorMovement(const std::vector<avs::MovementUpdate>& updateList)
{
	resourceManagers.mActorManager->UpdateActorMovement(updateList);
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
#include <algorithm>

const scr::Effect::EffectPassCreateInfo& Application::BuildEffectPass(const char* effectPassName, scr::VertexBufferLayout* vbl
		, const scr::ShaderSystem::PipelineCreateInfo *pipelineCreateInfo,  const std::vector<scr::ShaderResource>& shaderResources)
{
	if (GlobalGraphicsResources.pbrEffect.HasEffectPass(effectPassName)) return GlobalGraphicsResources.pbrEffect.GetEffectPassCreateInfo(effectPassName);

	scr::ShaderSystem::PassVariables pv;
	pv.mask          = false;
	pv.reverseDepth  = false;
	pv.msaa          = false;

	scr::ShaderSystem::Pipeline gp (&GlobalGraphicsResources.renderPlatform,pipelineCreateInfo);

	//scr::VertexBufferLayout
	vbl->CalculateStride();

	scr::Effect::ViewportAndScissor vs = {};
	vs.x = 0.0f;
	vs.y = 0.0f;
	vs.width = 0.0f;
	vs.height = 0.0f;
	vs.minDepth = 1.0f;
	vs.maxDepth = 0.0f;
	vs.offsetX = 0;
	vs.offsetY = 0;
	vs.extentX = (uint32_t)vs.x;
	vs.extentY = (uint32_t)vs.y;

	scr::Effect::RasterizationState rs = {};
	rs.depthClampEnable = false;
	rs.rasterizerDiscardEnable = false;
	rs.polygonMode = scr::Effect::PolygonMode::FILL;
	rs.cullMode = scr::Effect::CullMode::FRONT_BIT; //As of 2020-02-24, this only affects whether culling is enabled.
	rs.frontFace = scr::Effect::FrontFace::COUNTER_CLOCKWISE; //Unity does clockwise winding, and Unreal does counter-clockwise, but this is set before we connect to a server.

	scr::Effect::MultisamplingState ms = {};
	ms.samplerShadingEnable = false;
	ms.rasterizationSamples = scr::Texture::SampleCountBit::SAMPLE_COUNT_1_BIT;

	scr::Effect::StencilCompareOpState scos = {};
	scos.stencilFailOp = scr::Effect::StencilCompareOp::KEEP;
	scos.stencilPassDepthFailOp = scr::Effect::StencilCompareOp::KEEP;
	scos.passOp = scr::Effect::StencilCompareOp::KEEP;
	scos.compareOp = scr::Effect::CompareOp::NEVER;
	scr::Effect::DepthStencilingState dss = {};
	dss.depthTestEnable = true;
	dss.depthWriteEnable = true;
	dss.depthCompareOp = scr::Effect::CompareOp::LESS;
	dss.stencilTestEnable = false;
	dss.frontCompareOp = scos;
	dss.backCompareOp = scos;
	dss.depthBoundTestEnable = false;
	dss.minDepthBounds = 0.0f;
	dss.maxDepthBounds = 1.0f;

	scr::Effect::ColourBlendingState cbs = {};
	cbs.blendEnable = true;
	cbs.srcColorBlendFactor = scr::Effect::BlendFactor::SRC_ALPHA;
	cbs.dstColorBlendFactor = scr::Effect::BlendFactor::ONE_MINUS_SRC_ALPHA;
	cbs.colorBlendOp = scr::Effect::BlendOp ::ADD;
	cbs.srcAlphaBlendFactor = scr::Effect::BlendFactor::ONE;
	cbs.dstAlphaBlendFactor = scr::Effect::BlendFactor::ZERO;
	cbs.alphaBlendOp = scr::Effect::BlendOp ::ADD;

	scr::Effect::EffectPassCreateInfo ci;
	ci.effectPassName = effectPassName;
	ci.passVariables = pv;
	ci.pipeline = gp;
	ci.vertexLayout = *vbl;
	ci.topology = scr::Effect::TopologyType::TRIANGLE_LIST;
	ci.viewportAndScissor = vs;
	ci.rasterizationState = rs;
	ci.multisamplingState = ms;
	ci.depthStencilingState = dss;
	ci.colourBlendingState = cbs;

	GlobalGraphicsResources.pbrEffect.CreatePass(&ci);
	GlobalGraphicsResources.pbrEffect.LinkShaders(effectPassName, shaderResources);

	return GlobalGraphicsResources.pbrEffect.GetEffectPassCreateInfo(effectPassName);
}

std::string Application::LoadTextFile(const char *filename)
{
    std::vector<uint8_t> outBuffer;
    std::string str="apk:///assets/";
    str+=filename;
    if(app->GetFileSys().ReadFile(str.c_str(), outBuffer))
    {
        if(outBuffer.back() != '\0')
            outBuffer.push_back('\0'); //Append Null terminator character. ReadFile() does return a null terminated string, apparently!
        return std::string((const char *)outBuffer.data());
    }
    return "";
}