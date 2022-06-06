#include "ClientRender/Renderer.h"
#include "ClientRender/VideoDecoder.h"
#include <libavstream/libavstream.hpp>
#if TELEPORT_CLIENT_USE_D3D12
#include "Platform/DirectX12/RenderPlatform.h"
#include <libavstream/surfaces/surface_dx12.hpp>
#endif
#if TELEPORT_CLIENT_USE_D3D11
#include <libavstream/surfaces/surface_dx11.hpp>
#endif
#ifdef _MSC_VER
#include "libavstream/platforms/platform_windows.hpp"
#endif
#include "TeleportClient/ServerTimestamp.h"
#include "TeleportClient/Log.h"
#include <regex>
#include "Tests.h"
#include "TeleportClient/Config.h"
#include "Platform/CrossPlatform/Macros.h"
#include "Platform/CrossPlatform/GpuProfiler.h"
#include "Platform/CrossPlatform/BaseFramebuffer.h"
#include "Platform/CrossPlatform/Quaterniond.h"
#include "Platform/Core/StringFunctions.h"
#include "TeleportClient/OpenXR.h"
#include <fmt/format.h>
#if TELEPORT_CLIENT_USE_VULKAN
#include <libavstream/surfaces/surface_vulkan.hpp>
#include "Platform/Vulkan/Texture.h"
#endif

avs::Timestamp clientrender::platformStartTimestamp ;
static bool timestamp_initialized=false;
using namespace clientrender;
using namespace teleport;
using namespace platform;

void msgHandler(avs::LogSeverity severity, const char* msg, void* userData)
{
	if (severity > avs::LogSeverity::Warning)
		std::cerr << msg;
	else
		std::cout << msg ;
}

static const char* ToString(clientrender::Light::Type type)
{
	const char* lightTypeName = "";
	switch (type)
	{
	case clientrender::Light::Type::POINT:
		lightTypeName = "Point";
		break;
	case clientrender::Light::Type::DIRECTIONAL:
		lightTypeName = "  Dir";
		break;
	case clientrender::Light::Type::SPOT:
		lightTypeName = " Spot";
		break;
	case clientrender::Light::Type::AREA:
		lightTypeName = " Area";
		break;
	default:
	case clientrender::Light::Type::DISC:
		lightTypeName = " Disc";
		break;
		break;
	};
	return lightTypeName;
}

const char *stringof(avs::GeometryPayloadType t)
{
	static const char *txt[]=
	{
		"Invalid", 
		"Mesh",
		"Material",
		"MaterialInstance",
		"Texture",
		"Animation",
		"Node",
		"Skin",
		"Bone"
	};
	return txt[(size_t)t];
}



struct AVSTextureImpl :public clientrender::AVSTexture
{
	AVSTextureImpl(platform::crossplatform::Texture *t)
		:texture(t)
	{
	}
	platform::crossplatform::Texture *texture = nullptr;
	avs::SurfaceBackendInterface* createSurface() const override
	{
#if TELEPORT_CLIENT_USE_D3D12
		return new avs::SurfaceDX12(texture->AsD3D12Resource());
#endif
#if TELEPORT_CLIENT_USE_D3D11
		return new avs::SurfaceDX11(texture->AsD3D11Texture2D());
#endif
#if TELEPORT_CLIENT_USE_VULKAN
		auto &img=((platform::vulkan::Texture*)texture)->AsVulkanImage();
		return new avs::SurfaceVulkan(&img);
#endif
	}
};

Renderer::Renderer(client::ClientDeviceState *c,clientrender::NodeManager *localNodeManager,clientrender::NodeManager *remoteNodeManager,teleport::client::SessionClient *sc,teleport::Gui& g,bool dev)
	:sessionClient(sc)
	,localGeometryCache(localNodeManager)
	,dev_mode(dev)
	,clientDeviceState(c)
	,gui(g)
	,geometryCache(remoteNodeManager)
{
	sessionClient->SetSessionCommandInterface(this);
	if (!timestamp_initialized)
#ifdef _MSC_VER
		platformStartTimestamp = avs::PlatformWindows::getTimestamp();
#else
		platformStartTimestamp = avs::PlatformPOSIX::getTimestamp();
#endif
	timestamp_initialized=true;
	sessionClient->SetResourceCreator(&resourceCreator);
	sessionClient->SetGeometryCache(&geometryCache);
	resourceCreator.SetGeometryCache(&geometryCache);
	localResourceCreator.SetGeometryCache(&localGeometryCache);

	clientrender::Tests::RunAllTests();
}

Renderer::~Renderer()
{
	clientPipeline.pipeline.deconfigure();
	InvalidateDeviceObjects(); 
}

void Renderer::Init(platform::crossplatform::RenderPlatform *r,teleport::client::OpenXR *u,teleport::PlatformWindow* active_window)
{
	// Initialize the audio (asynchronously)
#ifdef _MSC_VER
	audioPlayer.initializeAudioDevice();
#endif
	renderPlatform = r;
	openXR=u;

	PcClientRenderPlatform.SetSimulRenderPlatform(r);
	r->SetShaderBuildMode(crossplatform::ShaderBuildMode::BUILD_IF_CHANGED);
	resourceCreator.Initialize(&PcClientRenderPlatform, clientrender::VertexBufferLayout::PackingStyle::INTERLEAVED);
	localResourceCreator.Initialize(&PcClientRenderPlatform, clientrender::VertexBufferLayout::PackingStyle::INTERLEAVED);

	hDRRenderer = new crossplatform::HdrRenderer();

	hdrFramebuffer	=renderPlatform->CreateFramebuffer();
	hdrFramebuffer->SetFormat(crossplatform::RGBA_16_FLOAT);
	hdrFramebuffer->SetDepthFormat(crossplatform::D_32_FLOAT);
	hdrFramebuffer->SetAntialiasing(1);
	camera.SetPositionAsXYZ(0.f,0.f,2.f);
	vec3 look(0.f,1.f,0.f),up(0.f,0.f,1.f);
	camera.LookInDirection(look,up);

	camera.SetHorizontalFieldOfViewDegrees(HFOV);

	// Automatic vertical fov - depends on window shape:
	camera.SetVerticalFieldOfViewDegrees(0.f);
	
	//const float aspect = hdrFramebuffer->GetWidth() / hdrFramebuffer->GetHeight();
	//cubemapConstants.localHorizFOV = HFOV * clientrender::DEG_TO_RAD;
	//cubemapConstants.localVertFOV = clientrender::GetVerticalFOVFromHorizontal(cubemapConstants.localHorizFOV, aspect);

	crossplatform::CameraViewStruct vs;
	vs.exposure=1.f;
	vs.farZ=3000.f;
	vs.nearZ=0.01f;
	vs.gamma=1.0f;
	vs.InfiniteFarPlane=true;
	vs.projection=crossplatform::DEPTH_REVERSE;
	
	camera.SetCameraViewStruct(vs);

	memset(keydown,0,sizeof(keydown));

	hDRRenderer->RestoreDeviceObjects(renderPlatform);
	hdrFramebuffer->RestoreDeviceObjects(renderPlatform);

	gui.RestoreDeviceObjects(renderPlatform,active_window);
	auto connectButtonHandler = std::bind(&Renderer::ConnectButtonHandler, this,std::placeholders::_1);
	gui.SetConnectHandler(connectButtonHandler);
	videoTexture = renderPlatform->CreateTexture();
	specularCubemapTexture = renderPlatform->CreateTexture();
	diffuseCubemapTexture = renderPlatform->CreateTexture();
	lightingCubemapTexture = renderPlatform->CreateTexture();
	errno=0;
	RecompileShaders();

	pbrConstants.RestoreDeviceObjects(renderPlatform);
	pbrConstants.LinkToEffect(pbrEffect,"pbrConstants");
	cubemapConstants.RestoreDeviceObjects(renderPlatform);
	cubemapConstants.LinkToEffect(cubemapClearEffect, "CubemapConstants");
	cameraConstants.RestoreDeviceObjects(renderPlatform); 
	tagDataIDBuffer.RestoreDeviceObjects(renderPlatform, 1, true);
	tagDataCubeBuffer.RestoreDeviceObjects(renderPlatform, maxTagDataSize, false, true);
	lightsBuffer.RestoreDeviceObjects(renderPlatform,10,false,true);
	boneMatrices.RestoreDeviceObjects(renderPlatform);
	boneMatrices.LinkToEffect(pbrEffect, "boneMatrices");

	avs::Context::instance()->setMessageHandler(msgHandler,nullptr);

	// initialize the default local geometry:
	geometryDecoder.decodeFromFile("meshes/Wand.mesh_compressed",&localResourceCreator);
	
	avs::uid wand_uid = 11;
	auto uids=localGeometryCache.mMeshManager.GetAllIDs();
	if (uids.size())
	{
		wand_uid = uids[0];
	}
	else
	{
		TELEPORT_BREAK_ONCE("Wand mesh not found");
	}
	{
	
		avs::Material avsMaterial;
		avsMaterial.name="local material";
		avsMaterial.pbrMetallicRoughness.metallicFactor=0.0f;
		localResourceCreator.CreateMaterial(14,avsMaterial);// not used just now.
		avsMaterial.name="local blue glow";
		avsMaterial.emissiveFactor={0.0f,0.5f,1.f};
		localResourceCreator.CreateMaterial(15,avsMaterial);
		avsMaterial.name="local red glow";
		avsMaterial.emissiveFactor={1.0f,0.1f,0.1f};
		localResourceCreator.CreateMaterial(16,avsMaterial);
	}
	avs::Node avsNode;
	avsNode.data_type=avs::NodeDataType::Mesh;
	//avsNode.transform.scale = { 0.2f,0.2f,0.2f };
	avsNode.data_uid=wand_uid;
	avsNode.materials.push_back(15);
	avsNode.materials.push_back(0);
	
	avsNode.name = "local Left Aim";
	avsNode.materials[0]=15;
	localResourceCreator.CreateNode(23,avsNode);
	//std::shared_ptr<clientrender::Node> leftHandNode=localGeometryCache.mNodeManager->CreateNode(23, avsNode);
	//leftHandNode->SetMesh(localGeometryCache.mMeshManager.Get(wand_uid));

	avsNode.name="local Right Aim";
	avsNode.materials[0]=16;
	localResourceCreator.CreateNode(24,avsNode);

	avsNode.name = "local Left Grip";
	avsNode.materials[0]=15;
	localResourceCreator.CreateNode(25,avsNode);

	avsNode.name="local Right Grip";
	avsNode.materials[0]=16;
	localResourceCreator.CreateNode(26,avsNode);

	if(openXR)
	{
		openXR->SetFallbackBinding(client::LEFT_AIM_POSE,"left/input/aim/pose");
		openXR->SetFallbackBinding(client::RIGHT_AIM_POSE,"right/input/aim/pose");
		openXR->MapNodeToPose(local_server_uid,23,"left/input/aim/pose");
		openXR->MapNodeToPose(local_server_uid,24,"right/input/aim/pose");
		
		openXR->SetFallbackBinding(client::LEFT_GRIP_POSE,"left/input/grip/pose");
		openXR->SetFallbackBinding(client::RIGHT_GRIP_POSE,"right/input/grip/pose");
		openXR->MapNodeToPose(local_server_uid,25,"left/input/grip/pose");
		openXR->MapNodeToPose(local_server_uid,26,"right/input/grip/pose");

		// Hard-code the menu button
		openXR->SetHardInputMapping(local_server_uid,local_menu_input_id,avs::InputType::IntegerEvent,teleport::client::SHOW_MENU);
	}
}

// This allows live-recompile of shaders. 
void Renderer::RecompileShaders()
{
	renderPlatform->RecompileShaders();
	hDRRenderer->RecompileShaders();
	gui.RecompileShaders();
	delete pbrEffect;
	delete cubemapClearEffect;
	pbrEffect = renderPlatform->CreateEffect("pbr");
	cubemapClearEffect = renderPlatform->CreateEffect("cubemap_clear");
	_RWTagDataIDBuffer = cubemapClearEffect->GetShaderResource("RWTagDataIDBuffer");
	_lights = pbrEffect->GetShaderResource("lights");
}
void Renderer::InvalidateDeviceObjects()
{
	AVSTextureImpl *ti = (AVSTextureImpl*)avsTexture.get();
	if (ti)
	{
		SAFE_DELETE(ti->texture);
	}
	gui.InvalidateDeviceObjects();
	if(pbrEffect)
	{
		pbrEffect->InvalidateDeviceObjects();
		delete pbrEffect;
		pbrEffect=nullptr;
	}
	if(hDRRenderer)
		hDRRenderer->InvalidateDeviceObjects();
	if(renderPlatform)
		renderPlatform->InvalidateDeviceObjects();
	if(hdrFramebuffer)
		hdrFramebuffer->InvalidateDeviceObjects();
	SAFE_DELETE(diffuseCubemapTexture);
	SAFE_DELETE(specularCubemapTexture);
	SAFE_DELETE(lightingCubemapTexture);
	SAFE_DELETE(videoTexture);
	SAFE_DELETE(hDRRenderer);
	SAFE_DELETE(hdrFramebuffer);
	SAFE_DELETE(pbrEffect);
	SAFE_DELETE(cubemapClearEffect);
}

void Renderer::CreateTexture(clientrender::AVSTextureHandle &th,int width, int height)
{
	if (!(th))
		th.reset(new AVSTextureImpl(nullptr));
	clientrender::AVSTexture *t = th.get();
	AVSTextureImpl *ti=(AVSTextureImpl*)t;
	if(!ti->texture)
		ti->texture = renderPlatform->CreateTexture();

	// NVidia decoder needs a shared handle to the resource.
#if TELEPORT_CLIENT_USE_D3D12 && !TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER
	bool useSharedHeap = true;
#else
	bool useSharedHeap = false;
#endif

	ti->texture->ensureTexture2DSizeAndFormat(renderPlatform, width, height,1, platform::crossplatform::RGBA_8_UNORM, true, true, 
		false, 1, 0, false, vec4(0.5f, 0.5f, 0.2f, 1.0f), 1.0f, 0, useSharedHeap);
}

void Renderer::FillInControllerPose(int index, float offset)
{
	if(!hdrFramebuffer->GetHeight())
		return;
	float x= mouseCameraInput.MouseX / (float)hdrFramebuffer->GetWidth();
	float y= mouseCameraInput.MouseY / (float)hdrFramebuffer->GetHeight();
	controllerSim.controller_dir	=camera.ScreenPositionToDirection(x, y, hdrFramebuffer->GetWidth() / static_cast<float>(hdrFramebuffer->GetHeight()));
	controllerSim.view_dir			=camera.ScreenPositionToDirection(0.5f,0.5f,1.0f);
	// we seek the angle positive on the Z-axis representing the view direction azimuth:
	static float cc=0.0f;
	cc+=0.01f;
	controllerSim.angle=atan2f(-controllerSim.view_dir.x, controllerSim.view_dir.y);
	float sine= sin(controllerSim.angle), cosine=cos(controllerSim.angle);
	float sine_elev= controllerSim.view_dir.z;
	static float hand_dist=0.5f;
	// Position the hand based on mouse pos.
	static float xmotion_scale = 1.0f;
	static float ymotion_scale = 1.0f;
	static float ymotion_offset = .2f;
	static float z_offset = -0.1f;
	vec2 pos; 
	pos.x = offset+(x - 0.5f) * xmotion_scale;
	pos.y = ymotion_offset + (0.5f-y)*ymotion_scale;

	controllerSim.pos_offset[index]=vec3(hand_dist*(-pos.y*sine+ pos.x*cosine),hand_dist*(pos.y*cosine+pos.x*sine),z_offset+hand_dist*sine_elev*pos.y);

	// Get horizontal azimuth of view.
	vec3 camera_local_pos	=camera.GetPosition();
	vec3 footspace_pos		=camera_local_pos;
	footspace_pos			+=controllerSim.pos_offset[index];

	// For the orientation, we want to point the controller towards controller_dir. The pointing direction is y.
	// The up direction is x, and the left direction is z.
	vec3 local_controller_dir = { 0,1.f,0 };
	crossplatform::Quaternionf q = (const float*)(&clientDeviceState->headPose.localPose.orientation);
	Multiply(local_controller_dir,q, local_controller_dir);
	float azimuth	= atan2f(-local_controller_dir.x, local_controller_dir.y);
	float elevation	= asin(local_controller_dir.z);
	q.Reset();
	q.Rotate(azimuth,vec3(0,0,1.0f));
	q.Rotate(elevation, vec3(1.0f, 0, 0));

	// convert from footspace to worldspace
	clientDeviceState->SetControllerPose( index,*((avs::vec3*)&footspace_pos),*((const clientrender::quat*)&q));
	avs::Pose pose;
	pose.position=*((avs::vec3*)&footspace_pos);
	pose.orientation=*((const clientrender::quat*)&q);

	openXR->SetFallbackPose(index?client::RIGHT_GRIP_POSE:client::LEFT_GRIP_POSE,pose);
	pose.position.z-=0.1f;
	openXR->SetFallbackPose(index?client::RIGHT_AIM_POSE:client::LEFT_AIM_POSE,pose);
	//openXR->SetVirtualPose("/interaction_profiles/simul/mouse_ext/left/input/grip/pose",LEFT_GRIP_POSE);
	//openXR->SetFallbackPath(teleport::client::ActionId::LEFT_GRIP_POSE,pose);
	
	controllerSim.position[index] =footspace_pos;
	controllerSim.orientation[index] =((const float*)&q);
}
void Renderer::ConfigureVideo(const avs::VideoConfig& videoConfig)
{
	clientPipeline.videoConfig = videoConfig;
}

void Renderer::RenderView(platform::crossplatform::GraphicsDeviceContext &deviceContext)
{
	SIMUL_COMBINED_PROFILE_START(deviceContext,"RenderView");
	crossplatform::Viewport viewport = renderPlatform->GetViewport(deviceContext, 0);
	pbrEffect->UnbindTextures(deviceContext);
	// The following block renders to the hdrFramebuffer's rendertarget:
	//vec3 finalViewPos=localOriginPos+relativeHeadPos;
	{
		clientrender::AVSTextureHandle th = avsTexture;
		clientrender::AVSTexture& tx = *th;
		AVSTextureImpl* ti = static_cast<AVSTextureImpl*>(&tx);

		if (ti)
		{
			// This will apply to both rendering methods
			{
				cubemapClearEffect->SetTexture(deviceContext, "plainTexture", ti->texture);
				tagDataIDBuffer.ApplyAsUnorderedAccessView(deviceContext, cubemapClearEffect, _RWTagDataIDBuffer);
				cubemapConstants.sourceOffset = int2(ti->texture->width - (32 * 4), ti->texture->length - 4);
				cubemapClearEffect->SetConstantBuffer(deviceContext, &cubemapConstants);
				cubemapClearEffect->Apply(deviceContext, "extract_tag_data_id", 0);
				renderPlatform->DispatchCompute(deviceContext, 1, 1, 1);
				cubemapClearEffect->Unapply(deviceContext);
				cubemapClearEffect->UnbindTextures(deviceContext);

				tagDataIDBuffer.CopyToReadBuffer(deviceContext);
				const uint4* videoIDBuffer = tagDataIDBuffer.OpenReadBuffer(deviceContext);
				if (videoIDBuffer && videoIDBuffer[0].x < 32 && videoIDBuffer[0].w == 110) // sanity check
				{
					int tagDataID = videoIDBuffer[0].x;

					const auto& ct = videoTagDataCubeArray[tagDataID].coreData.cameraTransform;
					videoPos = vec3(ct.position.x, ct.position.y, ct.position.z);

					videoPosDecoded = true;
				}
				tagDataIDBuffer.CloseReadBuffer(deviceContext);
			}
			
			#ifdef _MSC_VER
			UpdateTagDataBuffers(deviceContext);
			if (sessionClient->IsConnected())
			{
				if (videoTexture->IsCubemap())
				{
					const char* technique = clientPipeline.videoConfig.use_alpha_layer_decoding ? "recompose" : "recompose_with_depth_alpha";
					RecomposeVideoTexture(deviceContext, ti->texture, videoTexture, technique);
					RenderVideoTexture(deviceContext, ti->texture, videoTexture, "use_cubemap", "cubemapTexture", deviceContext.viewStruct.invViewProj);
				}
				else
				{
					const char* technique = clientPipeline.videoConfig.use_alpha_layer_decoding ? "recompose_perspective" : "recompose_perspective_with_depth_alpha";
					RecomposeVideoTexture(deviceContext, ti->texture, videoTexture, technique);
					platform::math::Matrix4x4 projInv;
					deviceContext.viewStruct.proj.Inverse(projInv);
					RenderVideoTexture(deviceContext, ti->texture, videoTexture, "use_perspective", "perspectiveTexture", projInv);
				}
			}
			RecomposeCubemap(deviceContext, ti->texture, diffuseCubemapTexture, diffuseCubemapTexture->mips, int2(clientPipeline.videoConfig.diffuse_x, clientPipeline.videoConfig.diffuse_y));
			RecomposeCubemap(deviceContext, ti->texture, specularCubemapTexture, specularCubemapTexture->mips, int2(clientPipeline.videoConfig.specular_x, clientPipeline.videoConfig.specular_y));
			#endif
		}
		//RecomposeCubemap(deviceContext, ti->texture, lightingCubemapTexture, lightingCubemapTexture->mips, int2(videoConfig.light_x, videoConfig.light_y));
		pbrConstants.drawDistance = lastSetupCommand.draw_distance;
		if (sessionClient->IsConnected()||render_local_offline)
			RenderLocalNodes(deviceContext,server_uid,geometryCache);

		{
			const std::map<avs::uid,teleport::client::NodePoseState> &nodePoseStates
				=openXR->GetNodePoseStates(0,renderPlatform->GetFrameNumber());
			auto l=nodePoseStates.find(23);
			std::vector<vec4> hand_pos_press;
			hand_pos_press.resize(2);
			if(l!=nodePoseStates.end())
			{
				avs::Pose leftHand = l->second.pose;
				avs::vec3 pos = LocalToGlobal(leftHand,avs::vec3(0,0.12f, 0));
				vec4 pos4;
				pos4.xyz = (const float*)&pos;
				pos4.w = 0.0f;
				hand_pos_press.push_back(pos4);
			}
			auto r=nodePoseStates.find(24);
			if(r!=nodePoseStates.end())
			{
				avs::Pose rightHand = r->second.pose;
				avs::vec3 pos = LocalToGlobal(rightHand,avs::vec3(0,0.12f, 0));
				vec4 pos4;
				pos4.xyz = (const float*)&pos;
				pos4.w = 0.0f;
				hand_pos_press.push_back(pos4);
			}
			gui.Update(hand_pos_press, have_vr_device);
		}
		

		if (!sessionClient->IsConnected()|| gui.HasFocus())
		{	
			pbrConstants.drawDistance = 1000.0f;
			RenderLocalNodes(deviceContext, 0,localGeometryCache);
			#if 1
			{
				pbrEffect->SetConstantBuffer(deviceContext, &cameraConstants);
				pbrEffect->SetConstantBuffer(deviceContext, &pbrConstants);
				auto pass=pbrEffect->GetTechniqueByName("triangle_test")->GetPass(0);
				renderPlatform->SetTopology(deviceContext, crossplatform::Topology::TRIANGLELIST);
				renderPlatform->ApplyPass(deviceContext, pass);
				renderPlatform->DrawIndexed(deviceContext, 3, 0, 0);
				renderPlatform->UnapplyPass(deviceContext);
			}
			#endif
		}
		gui.Render(deviceContext);
		// We must deactivate the depth buffer here, in order to use it as a texture:
		//hdrFramebuffer->DeactivateDepth(deviceContext);
		if (show_video)
		{
			int W = hdrFramebuffer->GetWidth();
			int H = hdrFramebuffer->GetHeight();
			renderPlatform->DrawTexture(deviceContext, 0, 0, W, H, ti->texture);
		}
		static int lod=0;
		static char tt=0;
		tt--;
		if(!tt)
			lod++;
		lod=lod%8;
		if(show_cubemaps)
		{
			renderPlatform->DrawCubemap(deviceContext,diffuseCubemapTexture,-0.3f,0.5f,0.2f,1.f,1.f, static_cast<float>(lod));
			renderPlatform->DrawCubemap(deviceContext,specularCubemapTexture,0.0f,0.5f,0.2f,1.f,1.f, static_cast<float>(lod));
		}
	}
	vec4 white(1.f, 1.f, 1.f, 1.f);
	if(show_textures)
	{
		std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
		auto& textures = geometryCache.mTextureManager.GetCache(cacheLock);
		static int tw = 128;
		int x = 0, y = 0;//hdrFramebuffer->GetHeight()-tw*2;
	/*	for (auto t : textures)
		{
			pc_client::PC_Texture* pct = static_cast<pc_client::PC_Texture*>(&(*t.second.resource));
			renderPlatform->DrawTexture(deviceContext, x, y, tw, tw, pct->GetSimulTexture());
			x += tw;
			if (x > hdrFramebuffer->GetWidth() - tw)
			{
				x = 0;
				y += tw;
			}
		}
		y += tw;
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyWhite.get()->GetSimulTexture());
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyNormal.get()->GetSimulTexture());
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyCombined.get()->GetSimulTexture());
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyBlack.get()->GetSimulTexture());
		*/
	}
	//hdrFramebuffer->Deactivate(deviceContext);
	//hDRRenderer->Render(deviceContext,hdrFramebuffer->GetTexture(),1.0f,gamma);

	SIMUL_COMBINED_PROFILE_END(deviceContext);
}

void Renderer::ChangePass(ShaderMode newShaderMode)
{
	switch(newShaderMode)
	{
		case ShaderMode::PBR:
			overridePassName = "";
			break;
		case ShaderMode::ALBEDO:
			overridePassName = "albedo_only";
			break;
		case ShaderMode::NORMAL_UNSWIZZLED:
			overridePassName = "normal_unswizzled";
			break;
		case ShaderMode::DEBUG_ANIM:
			overridePassName = "debug_anim";
			break;
		case ShaderMode::LIGHTMAPS:
			overridePassName = "debug_lightmaps";
			break;
		case ShaderMode::NORMAL_VERTEXNORMALS:
			overridePassName = "normal_vertexnormals";
			break;
	}
}
void Renderer::Update(double timestamp_ms)
{
	double timeElapsed_s = (timestamp_ms - previousTimestamp) / 1000.0f;//ms to seconds

	teleport::client::ServerTimestamp::tick(timeElapsed_s);

	geometryCache.Update(static_cast<float>(timeElapsed_s));
	resourceCreator.Update(static_cast<float>(timeElapsed_s));

	localGeometryCache.Update(static_cast<float>(timeElapsed_s));
	localResourceCreator.Update(static_cast<float>(timeElapsed_s));

	previousTimestamp = timestamp_ms;
}

bool Renderer::Match(const std::string& full_string, const std::string& substring)
{
	try
	{
		std::regex regex(substring, std::regex_constants::icase | std::regex::extended);
		std::smatch match;
		if (std::regex_search(full_string, match, regex))
		{
			std::cout << "matches for '" << full_string << "'\n";
			std::cout << "Prefix: '" << match.prefix() << "'\n";
			for (size_t i = 0; i < match.size(); ++i)
				std::cout << i << ": " << match[i] << '\n';
			std::cout << "Suffix: '" << match.suffix() << "\'\n\n";
			return true;
		}
	}
	catch (std::exception&)
	{
		return false;
	}
	catch (...)
	{
		return false;
	}
	return false;
}



void Renderer::OnReceiveVideoTagData(const uint8_t* data, size_t dataSize)
{
	clientrender::SceneCaptureCubeTagData tagData;
	memcpy(&tagData.coreData, data, sizeof(clientrender::SceneCaptureCubeCoreTagData));
	avs::ConvertTransform(lastSetupCommand.axesStandard, avs::AxesStandard::EngineeringStyle, tagData.coreData.cameraTransform);

	tagData.lights.resize(tagData.coreData.lightCount);

	teleport::client::ServerTimestamp::setLastReceivedTimestampUTCUnixMs(tagData.coreData.timestamp_unix_ms);

	// We will check the received light tags agains the current list of lights - rough and temporary.
	/*
	Roderick: we will here ignore the cached lights (CPU-streamed node lights) as they are unordered so may be found in a different order
		to the tag lights. ALL light data will go into the tags, using uid lookup to get any needed data from the unordered cache.
	std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
	auto &cachedLights=geometryCache.mLightManager.GetCache(cacheLock);
	auto &cachedLight=cachedLights.begin();*/
	////

	size_t index = sizeof(clientrender::SceneCaptureCubeCoreTagData);
	for (auto& light : tagData.lights)
	{
		memcpy(&light, &data[index], sizeof(clientrender::LightTagData));
		//avs::ConvertTransform(lastSetupCommand.axesStandard, avs::AxesStandard::EngineeringStyle, light.worldTransform);
		index += sizeof(clientrender::LightTagData);
	}
	if(tagData.coreData.id>= videoTagDataCubeArray.size())
	{
		TELEPORT_CERR_BREAK("Bad tag id",1);
		return;
	}
	videoTagDataCubeArray[tagData.coreData.id] = std::move(tagData);
}

void Renderer::UpdateTagDataBuffers(platform::crossplatform::GraphicsDeviceContext& deviceContext)
{				
	std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
	auto &cachedLights=geometryCache.mLightManager.GetCache(cacheLock);
	for (int i = 0; i < videoTagDataCubeArray.size(); ++i)
	{
		const auto& td = videoTagDataCubeArray[i];
		const auto& pos = td.coreData.cameraTransform.position;
		const auto& rot = td.coreData.cameraTransform.rotation;

		videoTagDataCube[i].cameraPosition = { pos.x, pos.y, pos.z };
		videoTagDataCube[i].cameraRotation = { rot.x, rot.y, rot.z, rot.w };
		videoTagDataCube[i].diffuseAmbientScale=td.coreData.diffuseAmbientScale;
		videoTagDataCube[i].lightCount = static_cast<int>(td.lights.size());
		if(td.lights.size() > 10)
		{
			TELEPORT_CERR_BREAK("Too many lights in tag.",10);
		}
		for(int j=0;j<td.lights.size()&&j<10;j++)
		{
			LightTag &t=videoTagDataCube[i].lightTags[j];
			const clientrender::LightTagData &l=td.lights[j];
			t.uid32=(unsigned)(((uint64_t)0xFFFFFFFF)&l.uid);
			t.colour=*((vec4*)&l.color);
			// Convert from +-1 to [0,1]
			t.shadowTexCoordOffset.x=float(l.texturePosition[0])/float(lastSetupCommand.video_config.video_width);
			t.shadowTexCoordOffset.y=float(l.texturePosition[1])/float(lastSetupCommand.video_config.video_height);
			t.shadowTexCoordScale.x=float(l.textureSize)/float(lastSetupCommand.video_config.video_width);
			t.shadowTexCoordScale.y=float(l.textureSize)/float(lastSetupCommand.video_config.video_height);
			// Tag data has been properly transformed in advance:
			avs::vec3 position		=l.position;
			avs::vec4 orientation	=l.orientation;
			t.position=*((vec3*)&position);
			crossplatform::Quaternionf q((const float*)&orientation);
			t.direction=q*vec3(0,0,1.0f);
			::mat4 worldToShadowMatrix=*((const ::mat4*)&l.worldToShadowMatrix);
				
			t.worldToShadowMatrix	=*((::mat4*)&worldToShadowMatrix);

			auto nodeLight=cachedLights.find(l.uid);
			if(nodeLight!=cachedLights.end()&& nodeLight->second.resource!=nullptr)
			{
				const clientrender::Light::LightCreateInfo &lc=nodeLight->second.resource->GetLightCreateInfo();
				t.is_point=float(lc.type!=clientrender::Light::Type::DIRECTIONAL);
				t.is_spot=float(lc.type==clientrender::Light::Type::SPOT);
				t.radius=lc.lightRadius;
				t.range=lc.lightRange;
				t.shadow_strength=0.0f;
			}
		}
	}	
	tagDataCubeBuffer.SetData(deviceContext, videoTagDataCube);
}

void Renderer::RecomposeVideoTexture(platform::crossplatform::GraphicsDeviceContext& deviceContext, platform::crossplatform::Texture* srcTexture, platform::crossplatform::Texture* targetTexture, const char* technique)
{
	int W = targetTexture->width;
	int H = targetTexture->length;
	cubemapConstants.sourceOffset = { 0, 0 };
	cubemapConstants.targetSize.x = W;
	cubemapConstants.targetSize.y = H;
	cubemapClearEffect->SetTexture(deviceContext, "plainTexture", srcTexture);
	cubemapClearEffect->SetConstantBuffer(deviceContext, &cubemapConstants);
	cubemapClearEffect->SetConstantBuffer(deviceContext, &cameraConstants);
	cubemapClearEffect->SetUnorderedAccessView(deviceContext, "RWTextureTargetArray", targetTexture);
	tagDataIDBuffer.Apply(deviceContext, cubemapClearEffect, cubemapClearEffect->GetShaderResource("TagDataIDBuffer"));
	int zGroups = videoTexture->IsCubemap() ? 6 : 1;
	cubemapClearEffect->Apply(deviceContext, technique, 0);
	renderPlatform->DispatchCompute(deviceContext, W / 16, H / 16, zGroups);
	cubemapClearEffect->Unapply(deviceContext);
	cubemapClearEffect->SetUnorderedAccessView(deviceContext, "RWTextureTargetArray", nullptr);
	cubemapClearEffect->UnbindTextures(deviceContext);
}

void Renderer::RenderVideoTexture(platform::crossplatform::GraphicsDeviceContext& deviceContext, platform::crossplatform::Texture* srcTexture, platform::crossplatform::Texture* targetTexture, const char* technique, const char* shaderTexture, const platform::math::Matrix4x4& invCamMatrix)
{
	tagDataCubeBuffer.Apply(deviceContext, cubemapClearEffect, cubemapClearEffect->GetShaderResource("TagDataCubeBuffer"));
	cubemapConstants.depthOffsetScale = vec4(0, 0, 0, 0);
	cubemapConstants.offsetFromVideo = *((vec3*)&clientDeviceState->headPose.globalPose.position) - videoPos;
	cubemapConstants.cameraPosition = *((vec3*)&clientDeviceState->headPose.globalPose.position);
	cubemapConstants.cameraRotation = *((vec4*)&clientDeviceState->headPose.globalPose.orientation);
	cameraConstants.invWorldViewProj = invCamMatrix;
	cubemapClearEffect->SetConstantBuffer(deviceContext, &cubemapConstants);
	cubemapClearEffect->SetConstantBuffer(deviceContext, &cameraConstants);
	cubemapClearEffect->SetTexture(deviceContext, shaderTexture, targetTexture);
	cubemapClearEffect->SetTexture(deviceContext, "plainTexture", srcTexture);
	cubemapClearEffect->Apply(deviceContext, technique, 0);
	renderPlatform->DrawQuad(deviceContext);
	cubemapClearEffect->Unapply(deviceContext);
	cubemapClearEffect->UnbindTextures(deviceContext);
}

void Renderer::RecomposeCubemap(platform::crossplatform::GraphicsDeviceContext& deviceContext, platform::crossplatform::Texture* srcTexture, platform::crossplatform::Texture* targetTexture, int mips, int2 sourceOffset)
{
	cubemapConstants.sourceOffset = sourceOffset;
	cubemapClearEffect->SetTexture(deviceContext, "plainTexture", srcTexture);
	cubemapClearEffect->SetConstantBuffer(deviceContext, &cameraConstants);

	cubemapConstants.targetSize.x = targetTexture->width;
	cubemapConstants.targetSize.y = targetTexture->length;

	for (int m = 0; m < mips; m++)
	{
		cubemapClearEffect->SetUnorderedAccessView(deviceContext, "RWTextureTargetArray", targetTexture, -1, m);
		cubemapClearEffect->SetConstantBuffer(deviceContext, &cubemapConstants);
		cubemapClearEffect->Apply(deviceContext, "recompose", 0);
		renderPlatform->DispatchCompute(deviceContext, targetTexture->width / 16, targetTexture->width / 16, 6);
		cubemapClearEffect->Unapply(deviceContext);
		cubemapConstants.sourceOffset.x += 3 * cubemapConstants.targetSize.x;
		cubemapConstants.targetSize /= 2;
	}
	cubemapClearEffect->SetUnorderedAccessView(deviceContext, "RWTextureTargetArray", nullptr);
	cubemapClearEffect->UnbindTextures(deviceContext);
}


void Renderer::RenderLocalNodes(platform::crossplatform::GraphicsDeviceContext& deviceContext,avs::uid this_server_uid,clientrender::GeometryCache &g)
{
	deviceContext.viewStruct.Init();

	cameraConstants.invWorldViewProj = deviceContext.viewStruct.invViewProj;
	cameraConstants.view = deviceContext.viewStruct.view;
	cameraConstants.proj = deviceContext.viewStruct.proj;
	cameraConstants.viewProj = deviceContext.viewStruct.viewProj;
	// The following block renders to the hdrFramebuffer's rendertarget:
	cameraConstants.viewPosition = ((const float*)&clientDeviceState->headPose.globalPose.position);
	

	{
		std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
		auto &cachedLights=g.mLightManager.GetCache(cacheLock);
		if(cachedLights.size()>lightsBuffer.count)
		{
			lightsBuffer.InvalidateDeviceObjects();
			lightsBuffer.RestoreDeviceObjects(renderPlatform, static_cast<int>(cachedLights.size()));
		}
		pbrConstants.lightCount = static_cast<int>(cachedLights.size());
	}
	/*
	std::shared_ptr<clientrender::Node> body = g.mNodeManager->GetBody();
	if (body)
	{
		body->SetLocalPosition(clientDeviceState->headPose.globalPose.position + bodyOffsetFromHead);

		//Calculate rotation angle on z-axis, and use to create new quaternion that only rotates the body on the z-axis.
		float angle = std::atan2(clientDeviceState->headPose.globalPose.orientation.z, clientDeviceState->headPose.globalPose.orientation.w);
		clientrender::quat zRotation(0.0f, 0.0f, std::sin(angle), std::cos(angle));
		body->SetLocalRotation(zRotation);
		// force update of model matrices - should not be necessary, but is.
		body->UpdateModelMatrix();
	}*/

	// Now, any nodes bound to OpenXR poses will be updated. This may include hand objects, for example.
	if(openXR)//&&this_server_uid!=0)
	{
		const auto &nodePoseStates=openXR->GetNodePoseStates(this_server_uid,renderPlatform->GetFrameNumber());
		for(auto &n:nodePoseStates)
		{
			clientDeviceState->SetLocalNodePose(n.first,n.second.pose);
			auto &globalPose=clientDeviceState->GetGlobalNodePose(n.first);
			std::shared_ptr<clientrender::Node> node=g.mNodeManager->GetNode(n.first);
			if(node)
			{
			// TODO: Should be done as local child of an origin node, not setting local pos = globalPose.pos
				node->SetLocalPosition(globalPose.position);
				node->SetLocalRotation(globalPose.orientation);
				// force update of model matrices - should not be necessary, but is.
				node->UpdateModelMatrix();
			}
		}
	}
	const clientrender::NodeManager::nodeList_t& nodeList = g.mNodeManager->GetRootNodes();
	for(const std::shared_ptr<clientrender::Node>& node : nodeList)
	{
		if(show_only!=0&&show_only!=node->id)
			continue;
		RenderNode(deviceContext, node,g);
	}
	if(show_node_overlays)
	for (const std::shared_ptr<clientrender::Node>& node : nodeList)
	{
		RenderNodeOverlay(deviceContext, node,g);
	}
}

void Renderer::RenderNode(platform::crossplatform::GraphicsDeviceContext& deviceContext, const std::shared_ptr<clientrender::Node>& node,clientrender::GeometryCache &g,bool force)
{
#if 1
	clientrender::AVSTextureHandle th = avsTexture;
	clientrender::AVSTexture& tx = *th;
	AVSTextureImpl* ti = static_cast<AVSTextureImpl*>(&tx);
	
	if(!force&&(node_select > 0 && node_select != node->id))
		return;
	std::shared_ptr<clientrender::Texture> globalIlluminationTexture ;
	if(node->GetGlobalIlluminationTextureUid() )
		globalIlluminationTexture = g.mTextureManager.Get(node->GetGlobalIlluminationTextureUid());

	std::string passName = "pbr_nolightmap"; //Pass used for rendering geometry.
	if(node->IsStatic())
		passName="pbr_lightmap";
	if(overridePassName.length()>0)
		passName= overridePassName;
	bool force_highlight = (gui.GetSelectedUid() == node->id);
	//Only render visible nodes, but still render children that are close enough.
	if(node->GetPriority()>=0)
	if(node->IsVisible()&&(show_only == 0 || show_only == node->id))
	{
		const std::shared_ptr<clientrender::Mesh> mesh = node->GetMesh();
		if(mesh)
		{
			const auto& meshInfo = mesh->GetMeshCreateInfo();
			static int mat_select=-1;
			for(size_t element = 0; element < node->GetMaterials().size() && element < meshInfo.ib.size(); element++)
			{
				if(mat_select >= 0 && mat_select != element)
					continue;
				auto* vb = meshInfo.vb[element].get();
				const auto* ib = meshInfo.ib[element].get();

				const platform::crossplatform::Buffer* const v[] = {vb->GetSimulVertexBuffer()};
				platform::crossplatform::Layout* layout = vb->GetLayout();

				mat4 model;
				const mat4& globalTransformMatrix = node->GetGlobalTransform().GetTransformMatrix();
				model = reinterpret_cast<const float*>(&globalTransformMatrix);
				static bool override_model=false;
				if(override_model)
				{
					model=mat4::identity();
				}

				mat4::mul(cameraConstants.worldViewProj, *((mat4*)&deviceContext.viewStruct.viewProj), model);
				cameraConstants.world = model;

				std::shared_ptr<clientrender::Texture> gi = globalIlluminationTexture;
				std::shared_ptr<clientrender::Material> material = node->GetMaterials()[element];
				std::string usedPassName = passName;

				std::shared_ptr<clientrender::Skin> skin = node->GetSkin();
				if (skin)
				{
					mat4* scr_matrices = skin->GetBoneMatrices(globalTransformMatrix);
					memcpy(&boneMatrices.boneMatrices, scr_matrices, sizeof(mat4) * clientrender::Skin::MAX_BONES);

					pbrEffect->SetConstantBuffer(deviceContext, &boneMatrices);
					usedPassName = "anim_" + usedPassName;
				}
				crossplatform::EffectPass *pass = pbrEffect->GetTechniqueByName("solid")->GetPass(usedPassName.c_str());
				if(material)
				{
					const clientrender::Material::MaterialCreateInfo& matInfo = material->GetMaterialCreateInfo();
					const clientrender::Material::MaterialData& md = material->GetMaterialData();
					memcpy(&pbrConstants.diffuseOutputScalar, &md, sizeof(md));
					pbrConstants.lightmapScaleOffset=*(const vec4*)(&(node->GetLightmapScaleOffset()));
					std::shared_ptr<clientrender::Texture> diffuse	= matInfo.diffuse.texture;
					std::shared_ptr<clientrender::Texture> normal	= matInfo.normal.texture;
					std::shared_ptr<clientrender::Texture> combined = matInfo.combined.texture;
					std::shared_ptr<clientrender::Texture> emissive = matInfo.emissive.texture;
					
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("diffuseTexture"), diffuse ? diffuse->GetSimulTexture() : nullptr);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("normalTexture"), normal ? normal->GetSimulTexture() : nullptr);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("combinedTexture"), combined ? combined->GetSimulTexture() : nullptr);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("emissiveTexture"), emissive ? emissive->GetSimulTexture() : nullptr);

				}
				else
				{
					pbrConstants.diffuseOutputScalar=vec4(1.0f,1.0f,1.0f,0.5f);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("diffuseTexture"),  nullptr);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("normalTexture"),  nullptr);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("combinedTexture"),  nullptr);
					pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("emissiveTexture"),  nullptr);
					pass = pbrEffect->GetTechniqueByName("solid")->GetPass("local");
				}
				if (node->IsHighlighted()||force_highlight)
				{
					pbrConstants.emissiveOutputScalar += vec4(0.2f, 0.2f, 0.2f, 0.f);
				}
				pbrEffect->SetTexture(deviceContext, pbrEffect->GetShaderResource("globalIlluminationTexture"), gi ? gi->GetSimulTexture() : nullptr);

				pbrEffect->SetTexture(deviceContext, "specularCubemap", specularCubemapTexture);
				pbrEffect->SetTexture(deviceContext, "diffuseCubemap", diffuseCubemapTexture);
				//pbrEffect->SetTexture(deviceContext, "lightingCubemap", lightingCubemapTexture);
				//pbrEffect->SetTexture(deviceContext, "videoTexture", ti->texture);
				//pbrEffect->SetTexture(deviceContext, "lightingCubemap", lightingCubemapTexture);
				
				lightsBuffer.Apply(deviceContext, pbrEffect, _lights );
				tagDataCubeBuffer.Apply(deviceContext, pbrEffect, pbrEffect->GetShaderResource("TagDataCubeBuffer"));
				tagDataIDBuffer.Apply(deviceContext, pbrEffect, pbrEffect->GetShaderResource("TagDataIDBuffer"));

				pbrEffect->SetConstantBuffer(deviceContext, &pbrConstants);
				pbrEffect->SetConstantBuffer(deviceContext, &cameraConstants);
				renderPlatform->SetLayout(deviceContext, layout);
				renderPlatform->SetTopology(deviceContext, crossplatform::Topology::TRIANGLELIST);
				renderPlatform->SetVertexBuffers(deviceContext, 0, 1, v, layout);
				renderPlatform->SetIndexBuffer(deviceContext, ib->GetSimulIndexBuffer());
				renderPlatform->ApplyPass(deviceContext, pass);
				renderPlatform->DrawIndexed(deviceContext, (int)ib->GetIndexBufferCreateInfo().indexCount, 0, 0);
				pbrEffect->UnbindTextures(deviceContext);
				renderPlatform->UnapplyPass(deviceContext);
				layout->Unapply(deviceContext);
			}
		}
	}

	for(std::weak_ptr<clientrender::Node> childPtr : node->GetChildren())
	{
		std::shared_ptr<clientrender::Node> child = childPtr.lock();
		if(child)
		{
			RenderNode(deviceContext, child,g,true);
		}
	}
	#endif
}


void Renderer::RenderNodeOverlay(platform::crossplatform::GraphicsDeviceContext& deviceContext, const std::shared_ptr<clientrender::Node>& node,clientrender::GeometryCache &g,bool force)
{
	clientrender::AVSTextureHandle th = avsTexture;
	clientrender::AVSTexture& tx = *th;
	AVSTextureImpl* ti = static_cast<AVSTextureImpl*>(&tx);
	if(!force&&(node_select > 0 && node_select != node->id))
		return;

	std::shared_ptr<clientrender::Texture> globalIlluminationTexture;
	if (node->GetGlobalIlluminationTextureUid())
		globalIlluminationTexture = g.mTextureManager.Get(node->GetGlobalIlluminationTextureUid());

	//Only render visible nodes, but still render children that are close enough.
	if (node->IsVisible()&& (show_only == 0 || show_only == node->id))
	{
		const std::shared_ptr<clientrender::Mesh> mesh = node->GetMesh();
		const clientrender::AnimationComponent& anim = node->animationComponent;
		avs::vec3 pos = node->GetGlobalPosition();
		avs::vec4 white(1.0f, 1.0f, 1.0f, 1.0f);
		if (node->GetSkin().get())
		{
			std::string str;
			const clientrender::AnimationState* animationState = node->animationComponent.GetCurrentAnimationState();
			if (animationState)
			{
				//const clientrender::AnimationStateMap &animationStates= node->animationComponent.GetAnimationStates();
				static char txt[250];
				//for(const auto &s:animationStates)
				{
					const auto& a = animationState->getAnimation();
					if (a.get())
					{
						str +=fmt::format( "%llu %s %3.3f\n", node->id, a->name.c_str(), node->animationComponent.GetCurrentAnimationTimeSeconds());
						
					}
				}
				renderPlatform->PrintAt3dPos(deviceContext, (const float*)(&pos), str.c_str(), (const float*)(&white));
			}
		}
		else if (mesh)
		{
			std::string str=fmt::format("%llu %s: %s", node->id,node->name.c_str(), mesh->GetMeshCreateInfo().name.c_str());
			renderPlatform->PrintAt3dPos(deviceContext, (const float*)(&pos), str.c_str(), (const float*)(&white), nullptr, 0, 0, false);
		}
		else
		{
			avs::vec4 yellow(1.0f, 1.0f, 0.0f, 1.0f); 
			std::string str=fmt::format("%llu %s", node->id, node->name.c_str());
			renderPlatform->PrintAt3dPos(deviceContext, (const float*)(&pos), str.c_str(), (const float*)(&yellow), nullptr, 0, 0, false);
		}
	}

	for (std::weak_ptr<clientrender::Node> childPtr : node->GetChildren())
	{
		std::shared_ptr<clientrender::Node> child = childPtr.lock();
		if (child)
		{
			RenderNodeOverlay(deviceContext, child,g,true);
		}
	}
}

bool Renderer::OnDeviceRemoved()
{
	InvalidateDeviceObjects();
	return true;
}

void Renderer::OnFrameMove(double fTime,float time_step,bool have_headset)
{
	using_vr = have_headset;
	vec2 clientspace_input;
	static vec2 stored_clientspace_input(0,0);
#ifdef _MSC_VER
	clientspace_input.y=((float)keydown['w']-(float)keydown['s'])*(float)(keydown[VK_SHIFT]);
	clientspace_input.x=((float)keydown['d']-(float)keydown['a'])*(float)(keydown[VK_SHIFT]);
	static int clientspace_timeout=0;
	if(clientspace_input.y!=0||clientspace_input.x!=0)
	{
		stored_clientspace_input=clientspace_input;
		clientspace_timeout=20;
	}
	else if(clientspace_timeout)
	{
		clientspace_timeout--;
		if(!clientspace_timeout)
			stored_clientspace_input=vec2(0,0);
	}
	mouseCameraInput.forward_back_input	=((float)keydown['w']-(float)keydown['s'])*(float)(!keydown[VK_SHIFT]);
	mouseCameraInput.right_left_input	=((float)keydown['d']-(float)keydown['a'])*(float)(!keydown[VK_SHIFT]);
	mouseCameraInput.up_down_input		=((float)keydown['q']-(float)keydown['z'])*(float)(!keydown[VK_SHIFT]);
	
#endif
	if (!have_headset)
	{
		static float spd = 0.5f;
		crossplatform::UpdateMouseCamera(&camera
			, time_step
			, spd
			, mouseCameraState
			, mouseCameraInput
			, 14000.f, false, crossplatform::MouseCameraInput::RIGHT_BUTTON);


		// consider this to be the position relative to the local origin. Don't let it get too far from centre.
		vec3 cam_pos = camera.GetPosition();
		float r = sqrt(cam_pos.x * cam_pos.x + cam_pos.y * cam_pos.y);
		if (cam_pos.z > 2.0f)
			cam_pos.z = 2.0f;
		if (cam_pos.z < 1.0f)
			cam_pos.z = 1.0f;
		platform::math::Quaternion q0(3.1415926536f / 2.0f, platform::math::Vector3(1.f, 0.0f, 0.0f));
		auto q = camera.Orientation.GetQuaternion();
		auto q_rel = q / q0;
		clientDeviceState->SetHeadPose(*((avs::vec3*)&cam_pos), *((clientrender::quat*)&q_rel));
		clientDeviceState->SetInputs( inputs);

	}
	if (openXR)
	{
		const teleport::core::Input& local_inputs=openXR->GetServerInputs(local_server_uid,renderPlatform->GetFrameNumber());
		HandleLocalInputs(local_inputs);
		have_vr_device=openXR->HaveXRDevice();
	}
	// Handle networked session.
	if (sessionClient->IsConnected())
	{
		//vec3 forward=-camera.Orientation.Tz();
		//vec3 right=camera.Orientation.Tx();
		//*((vec3*)&clientDeviceState->originPose.position)+=clientspace_input.y*time_step*forward;
		//*((vec3*)&clientDeviceState->originPose.position)+=clientspace_input.x*time_step*right;
		// std::cout << forward.x << " " << forward.y << " " << forward.z << "\n";
		// The camera has Z backward, X right, Y up.
		// But we want orientation relative to X right, Y forward, Z up.

		avs::DisplayInfo displayInfo = {static_cast<uint32_t>(hdrFramebuffer->GetWidth()), static_cast<uint32_t>(hdrFramebuffer->GetHeight())};
	
		avs::Pose controllerPoses[2];
		controllerPoses[0]=clientDeviceState->controllerPoses[0].globalPose;
		controllerPoses[1]=clientDeviceState->controllerPoses[1].globalPose;
		if (openXR)
		{
			const teleport::core::Input& inputs = openXR->GetServerInputs(server_uid,renderPlatform->GetFrameNumber());
			clientDeviceState->SetInputs(inputs);
		}
		sessionClient->Frame(displayInfo, clientDeviceState->headPose.globalPose, controllerPoses, receivedInitialPos, clientDeviceState->originPose,
			clientDeviceState->input, clientPipeline.decoder.idrRequired(),fTime, time_step);

		if(receivedInitialPos != sessionClient->receivedInitialPos)
		{
			clientDeviceState->originPose = sessionClient->GetOriginPose();
			receivedInitialPos = sessionClient->receivedInitialPos;
			clientDeviceState->UpdateGlobalPoses();
		}
		
		if(receivedRelativePos!=sessionClient->receivedRelativePos)
		{
			receivedRelativePos=sessionClient->receivedRelativePos;
			auto pos =sessionClient->GetOriginToHeadOffset();
			camera.SetPosition((const float*)(&pos));
		}
		
		avs::Result result = clientPipeline.pipeline.process();
		if (result == avs::Result::Network_Disconnection)
		{
			sessionClient->Disconnect(0);
			return;
		}

		static short c = 0;
		if (!(c--))
		{
			const avs::NetworkSourceCounters Counters = clientPipeline.source.getCounterValues();
			std::cout << "Network packets dropped: " << 100.0f*Counters.networkDropped << "%"
				<< "\nDecoder packets dropped: " << 100.0f*Counters.decoderDropped << "%"
				<< std::endl;
		}
	}
	else
	{
		ENetAddress remoteEndpoint; //192.168.3.42 45.132.108.84
		if (canConnect && sessionClient->Discover("", TELEPORT_CLIENT_DISCOVERY_PORT, server_ip.c_str(), server_discovery_port, remoteEndpoint))
		{
			sessionClient->Connect(remoteEndpoint, TELEPORT_TIMEOUT);
			gui.SetConnecting(false);
			canConnect=false;
			gui.Hide();
		}
	}

	if (!have_headset)
	{
		FillInControllerPose(0, -0.5f);
		FillInControllerPose(1, 0.5f);
	}
	// Have parsed these, can free them now.
	inputs.clearEvents();
}

void Renderer::OnMouseButtonPressed(bool bLeftButtonDown, bool bRightButtonDown, bool bMiddleButtonDown, int nMouseWheelDelta)
{
	mouseCameraInput.MouseButtons
		|= (bLeftButtonDown ? crossplatform::MouseCameraInput::LEFT_BUTTON : 0)
		| (bRightButtonDown ? crossplatform::MouseCameraInput::RIGHT_BUTTON : 0)
		| (bMiddleButtonDown ? crossplatform::MouseCameraInput::MIDDLE_BUTTON : 0);
	if(bLeftButtonDown)
	{
		//  usually trigger
		inputs.addAnalogueEvent( inputIdMappings[MouseOrKey::LEFT_BUTTON], 1.0f);
	}
	else if(bRightButtonDown)
	{
		// usually button B
		inputs.addAnalogueEvent(inputIdMappings[MouseOrKey::RIGHT_BUTTON], 1.0f);
		inputs.addBinaryEvent(inputIdMappings[MouseOrKey::RIGHT_BUTTON], true);
	}
	else if(bMiddleButtonDown)
	{
		// usually button A
		inputs.addAnalogueEvent(inputIdMappings[MouseOrKey::MIDDLE_BUTTON], 1.0f);
		inputs.addBinaryEvent(inputIdMappings[MouseOrKey::MIDDLE_BUTTON], true);
	}
}

void Renderer::OnMouseButtonReleased(bool bLeftButtonReleased, bool bRightButtonReleased, bool bMiddleButtonReleased, int nMouseWheelDelta)
{
	mouseCameraInput.MouseButtons
		&= (bLeftButtonReleased ? ~crossplatform::MouseCameraInput::LEFT_BUTTON : crossplatform::MouseCameraInput::ALL_BUTTONS)
		& (bRightButtonReleased ? ~crossplatform::MouseCameraInput::RIGHT_BUTTON : crossplatform::MouseCameraInput::ALL_BUTTONS)
		& (bMiddleButtonReleased ? ~crossplatform::MouseCameraInput::MIDDLE_BUTTON : crossplatform::MouseCameraInput::ALL_BUTTONS);
	if(bLeftButtonReleased)
	{
		inputs.addAnalogueEvent(inputIdMappings[MouseOrKey::LEFT_BUTTON], 0.0f);
	}
	else if(bRightButtonReleased)
	{
		// usually button B
		inputs.addAnalogueEvent(inputIdMappings[MouseOrKey::RIGHT_BUTTON], 0.0f);
		inputs.addBinaryEvent(inputIdMappings[MouseOrKey::RIGHT_BUTTON], false);
	}
	else if(bMiddleButtonReleased)
	{
		inputs.addAnalogueEvent(inputIdMappings[MouseOrKey::MIDDLE_BUTTON], 0.0f);
		inputs.addBinaryEvent(inputIdMappings[MouseOrKey::MIDDLE_BUTTON], false);
	}
}

void Renderer::OnMouseMove(int xPos
			,int yPos,bool bLeftButtonDown
			,bool bRightButtonDown
			,bool bMiddleButtonDown
			,int nMouseWheelDelta
			 )
{
	mouseCameraInput.MouseX=xPos;
	mouseCameraInput.MouseY=yPos;
	mouseCameraInput.MouseButtons
		|= (bLeftButtonDown ? crossplatform::MouseCameraInput::LEFT_BUTTON : 0)
		| (bRightButtonDown ? crossplatform::MouseCameraInput::RIGHT_BUTTON : 0)
		| (bMiddleButtonDown ? crossplatform::MouseCameraInput::MIDDLE_BUTTON : 0);
}


void Renderer::OnKeyboard(unsigned wParam,bool bKeyDown,bool gui_shown)
{
	switch (wParam) 
	{
#ifdef _MSC_VER
		case VK_LEFT: 
		case VK_RIGHT: 
		case VK_UP: 
		case VK_DOWN:
			return;
#endif
		default:
			int  k = tolower(wParam);
			if (k > 255)
				return;
			keydown[k] = bKeyDown ? 1 : 0;
		break; 
	}
	if (!bKeyDown)
	{
		switch (wParam)
		{
		case 'V':
			show_video = !show_video;
			break;
		case 'O':
			show_osd =(show_osd+1)%clientrender::NUM_OSDS;
			break;
		case 'C':
			render_from_video_centre = !render_from_video_centre;
			break;
		case 'U':
			show_cubemaps = !show_cubemaps;
			break;
		case 'H':
			WriteHierarchies();
			break;
		case 'T':
			show_textures = !show_textures;
			break;
		case 'N':
			show_node_overlays = !show_node_overlays;
			break;
		case 'K':
			if(sessionClient->IsConnected())
				sessionClient->Disconnect(0);
			canConnect=!canConnect;
			break;
		case 'M':
			RenderMode++;
			RenderMode = RenderMode % 2;
			break;
		case 'R':
			RecompileShaders();
			break;
		case 'Y':
			if (sessionClient->IsConnected())
				clientPipeline.decoder.toggleShowAlphaAsColor();
			break;
			#ifdef _MSC_VER
		case VK_SPACE:
			gui.ShowHide();
			break;
		case VK_NUMPAD0: //Display full PBR rendering.
			ChangePass(clientrender::ShaderMode::PBR);
			break;
		case VK_NUMPAD1: //Display only albedo/diffuse.
			ChangePass(clientrender::ShaderMode::ALBEDO);
			break;
		case VK_NUMPAD4: //Display normals for native PC client frame-of-reference.
			ChangePass(clientrender::ShaderMode::NORMAL_UNSWIZZLED);
			break;
		case VK_NUMPAD5: //Display normals swizzled for matching Unreal output.
			ChangePass(clientrender::ShaderMode::DEBUG_ANIM);
			break;
		case VK_NUMPAD6: //Display normals swizzled for matching Unity output.
			ChangePass(clientrender::ShaderMode::LIGHTMAPS);
			break;
		case VK_NUMPAD2: //Display normals swizzled for matching Unity output.
			ChangePass(clientrender::ShaderMode::NORMAL_VERTEXNORMALS);
			break;
			#endif
		default:
			break;
		}
	}
}


void Renderer::WriteHierarchy(int tabDepth, std::shared_ptr<clientrender::Node> node)
{
	for(int i = 0; i < tabDepth; i++)
	{
		std::cout << "\t";
	}
	std::cout << node->id << "(" << node->name << ")" << std::endl;

	for(auto child : node->GetChildren())
	{
		WriteHierarchy(tabDepth + 1, child.lock());
	}
}

void Renderer::WriteHierarchies()
{
	std::cout << "Node Tree\n----------------------------------\n";

	for(std::shared_ptr<clientrender::Node> node : geometryCache.mNodeManager->GetRootNodes())
	{
		WriteHierarchy(0, node);
	}

	std::cout << std::endl;
}


// We only ever create one view in this example, but in general, this should return a new value each time it's called.
int Renderer::AddView()
{
	static int last_view_id=0;
	// We override external_framebuffer here and pass "true" to demonstrate how external depth buffers are used.
	// In this case, we use hdrFramebuffer's depth buffer.
	return last_view_id++;
}
void Renderer::ResizeView(int view_id,int W,int H)
{
	if(hDRRenderer)
		hDRRenderer->SetBufferSize(W,H);
	if(hdrFramebuffer)
	{
		hdrFramebuffer->SetWidthAndHeight(W,H);
		hdrFramebuffer->SetAntialiasing(1);
	}
	//const float aspect = W / H;
	//cubemapConstants.localHorizFOV = HFOV * clientrender::DEG_TO_RAD;
	//cubemapConstants.localVertFOV = clientrender::GetVerticalFOVFromHorizontal(cubemapConstants.localHorizFOV, aspect);
}


bool Renderer::OnNodeEnteredBounds(avs::uid id)
{
	return geometryCache.mNodeManager->ShowNode(id);
}

bool Renderer::OnNodeLeftBounds(avs::uid id)
{
	return geometryCache.mNodeManager->HideNode(id);
}

void Renderer::UpdateNodeStructure(const avs::UpdateNodeStructureCommand &updateNodeStructureCommand)
{
	geometryCache.mNodeManager->ReparentNode(updateNodeStructureCommand);
}

void Renderer::UpdateNodeSubtype(const avs::UpdateNodeSubtypeCommand &updateNodeStructureCommand,const std::string &regexPath)
{
	if(regexPath.size())
	{
		openXR->MapNodeToPose(server_uid,updateNodeStructureCommand.nodeID,regexPath);
	}
	else
	{
		TELEPORT_CERR << "Unrecognised node regexPath: "<<regexPath.c_str() << "!\n";
	}
}

void Renderer::SetVisibleNodes(const std::vector<avs::uid>& visibleNodes)
{
	geometryCache.mNodeManager->SetVisibleNodes(visibleNodes);
}

void Renderer::UpdateNodeMovement(const std::vector<avs::MovementUpdate>& updateList)
{
	geometryCache.mNodeManager->UpdateNodeMovement(updateList);
}

void Renderer::UpdateNodeEnabledState(const std::vector<avs::NodeUpdateEnabledState>& updateList)
{
	geometryCache.mNodeManager->UpdateNodeEnabledState(updateList);
}

void Renderer::SetNodeHighlighted(avs::uid nodeID, bool isHighlighted)
{
	geometryCache.mNodeManager->SetNodeHighlighted(nodeID, isHighlighted);
}

void Renderer::UpdateNodeAnimation(const avs::ApplyAnimation& animationUpdate)
{
	geometryCache.mNodeManager->UpdateNodeAnimation(animationUpdate);
}

void Renderer::UpdateNodeAnimationControl(const avs::NodeUpdateAnimationControl& animationControlUpdate)
{
	switch(animationControlUpdate.timeControl)
	{
	case avs::AnimationTimeControl::ANIMATION_TIME:
		geometryCache.mNodeManager->UpdateNodeAnimationControl(animationControlUpdate.nodeID, animationControlUpdate.animationID);
		break;
	/*case avs::AnimationTimeControl::CONTROLLER_0_TRIGGER:
		geometryCache.mNodeManager->UpdateNodeAnimationControl(animationControlUpdate.nodeID, animationControlUpdate.animationID, &inputs.triggerBack, 1.0f);
		break;
	case avs::AnimationTimeControl::CONTROLLER_1_TRIGGER:
		geometryCache.mNodeManager->UpdateNodeAnimationControl(animationControlUpdate.nodeID, animationControlUpdate.animationID, &inputs.triggerBack, 1.0f);
		break;*/
	default:
		TELEPORT_CERR_BREAK("Failed to update node animation control! Time control was set to the invalid value" + std::to_string(static_cast<int>(animationControlUpdate.timeControl)) + "!", -1);
		break;
	}
}

void Renderer::SetNodeAnimationSpeed(avs::uid nodeID, avs::uid animationID, float speed)
{
	geometryCache.mNodeManager->SetNodeAnimationSpeed(nodeID, animationID, speed);
}

void Renderer::Render(int view_id, void* context, void* renderTexture, int w, int h, long long frame, void* context_allocator)
{
	static platform::core::Timer timer;
	static float last_t = 0.0f;
	timer.UpdateTime();
	if (last_t != 0.0f && timer.TimeSum != last_t)
	{
		framerate = 1000.0f / (timer.TimeSum - last_t);
	}
	last_t = timer.TimeSum;
	platform::crossplatform::GraphicsDeviceContext	deviceContext;
	deviceContext.setDefaultRenderTargets(renderTexture, nullptr, 0, 0, w, h);
	deviceContext.platform_context = context;
	deviceContext.renderPlatform = renderPlatform;
	deviceContext.viewStruct.view_id = view_id;
	deviceContext.viewStruct.depthTextureStyle = crossplatform::PROJECTION;

	platform::crossplatform::SetGpuProfilingInterface(deviceContext, renderPlatform->GetGpuProfiler());
	renderPlatform->GetGpuProfiler()->SetMaxLevel(5);
	renderPlatform->GetGpuProfiler()->StartFrame(deviceContext);
	SIMUL_COMBINED_PROFILE_START(deviceContext, "Renderer::Render");
	crossplatform::Viewport viewport = renderPlatform->GetViewport(deviceContext, 0);

	hdrFramebuffer->Activate(deviceContext);
	hdrFramebuffer->Clear(deviceContext, 0.0f, 0.25f, 0.5f, 0.f, reverseDepth ? 0.f : 1.f);
	// 
	vec3 true_pos = camera.GetPosition();
	if (render_from_video_centre)
	{
	#ifdef _MSC_VER
		Sleep(200);
	#endif
		vec3 pos = videoPosDecoded ? videoPos : vec3(0, 0, 0);
		camera.SetPosition(pos);
	};
	float aspect = (float)viewport.w / (float)viewport.h;
	if (reverseDepth)
		deviceContext.viewStruct.proj = camera.MakeDepthReversedProjectionMatrix(aspect);
	else
		deviceContext.viewStruct.proj = camera.MakeProjectionMatrix(aspect);
	platform::math::SimulOrientation globalOrientation;
	// global pos/orientation:
	globalOrientation.SetPosition((const float*)&clientDeviceState->headPose.globalPose.position);

	platform::math::Quaternion q0(3.1415926536f / 2.0f, platform::math::Vector3(-1.f, 0.0f, 0.0f));
	platform::math::Quaternion q1 = (const float*)&clientDeviceState->headPose.globalPose.orientation;

	auto q_rel = q1 / q0;
	globalOrientation.SetOrientation(q_rel);
	deviceContext.viewStruct.view = globalOrientation.GetInverseMatrix().RowPointer(0);

	// MUST call init each frame.
	deviceContext.viewStruct.Init();
	if (externalTexture)
	{
		renderPlatform->DrawTexture(deviceContext, 0, 0, w, h, externalTexture);
	}
	else
	{
		RenderView(deviceContext);
	}

	vec4 white(1.f, 1.f, 1.f, 1.f);
	if (render_from_video_centre)
	{
		camera.SetPosition(true_pos);
		renderPlatform->Print(deviceContext, viewport.w - 16, viewport.h - 16, "C", white);
	}
	// We must deactivate the depth buffer here, in order to use it as a texture:
  	hdrFramebuffer->DeactivateDepth(deviceContext);
	//renderPlatform->DrawDepth(deviceContext, 0, 0, (256 * viewport.w)/ viewport.h, 256, hdrFramebuffer->GetDepthTexture());
	if (show_video)
	{
		clientrender::AVSTextureHandle th = avsTexture;
		clientrender::AVSTexture& tx = *th;
		AVSTextureImpl* ti = static_cast<AVSTextureImpl*>(&tx);
		int W = hdrFramebuffer->GetWidth();
		int H = hdrFramebuffer->GetHeight();
		if(ti)
			renderPlatform->DrawTexture(deviceContext, 0, 0, W, H, ti->texture);
	}
	static int lod = 0;
	static char tt = 0;
	tt--;
	if (!tt)
		lod++;
	lod = lod % 8;
	if(show_cubemaps)
	{
		renderPlatform->DrawCubemap(deviceContext, diffuseCubemapTexture, -0.3f, 0.5f, 0.2f, 1.f, 1.f, static_cast<float>(lod));
		renderPlatform->DrawCubemap(deviceContext, specularCubemapTexture, 0.0f, 0.5f, 0.2f, 1.f, 1.f, static_cast<float>(lod));
	}
	if (show_textures)
	{
		std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
		auto& textures = geometryCache.mTextureManager.GetCache(cacheLock);
		static int tw = 128;
		int x = 0, y = 0;//hdrFramebuffer->GetHeight()-tw*2;
		for (auto t : textures)
		{
			clientrender::Texture* pct = t.second.resource.get();
			renderPlatform->DrawTexture(deviceContext, x, y, tw, tw, pct->GetSimulTexture());
			x += tw;
			if (x > hdrFramebuffer->GetWidth() - tw)
			{
				x = 0;
				y += tw;
			}
		}
		y += tw;
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyWhite.get()->GetSimulTexture());
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyNormal.get()->GetSimulTexture());
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyCombined.get()->GetSimulTexture());
		renderPlatform->DrawTexture(deviceContext, x += tw, y, tw, tw, resourceCreator.m_DummyBlack.get()->GetSimulTexture());
	}

	hdrFramebuffer->Deactivate(deviceContext);
	hDRRenderer->Render(deviceContext, hdrFramebuffer->GetTexture(), 1.0f, gamma);

	if (show_osd)
	{
		DrawOSD(deviceContext);
	}
#ifdef ONSCREEN_PROF
	static std::string profiling_text;
	renderPlatform->LinePrint(deviceContext, profiling_text.c_str());
#endif
	SIMUL_COMBINED_PROFILE_END(deviceContext);
#ifdef ONSCREEN_PROF
	static char c = 0;
	c--;
	if(!c)
		profiling_text=renderPlatform->GetGpuProfiler()->GetDebugText();
#endif
}

void Renderer::SetServer(const char *ip_port)
{
	std::string ip= ip_port;
	size_t pos=ip.find(":");
	if(pos>=ip.length())
	{
		server_discovery_port = TELEPORT_SERVER_DISCOVERY_PORT;
		server_ip=ip;
	}
	else
	{
		server_discovery_port =atoi(ip.substr(pos+1,ip.length()-pos-1).c_str());
		server_ip = ip.substr(0,pos);
	}
}

void Renderer::ConnectButtonHandler(const std::string& url)
{
	SetServer(url.c_str());
	/*size_t pos = url.find(":");
	if (pos < url.length())
	{
		std::string port_str = url.substr(pos + 1, url.length() - pos - 1);
		server_discovery_port = atoi(port_str.c_str());
		std::string url_str = url.substr(0, pos);
		server_ip = url_str;
	}
	else
	{
		server_ip = url;
	}*/
	canConnect = true;
}


void Renderer::RemoveView(int)
{
}


void Renderer::DrawOSD(platform::crossplatform::GraphicsDeviceContext& deviceContext)
{
	if(gui.HasFocus())
		return;
	gui.BeginDebugGui(deviceContext);
	vec4 white(1.f, 1.f, 1.f, 1.f);
	vec4 text_colour={1.0f,1.0f,0.5f,1.0f};
	vec4 background={0.0f,0.0f,0.0f,0.5f};
	const avs::NetworkSourceCounters counters = clientPipeline.source.getCounterValues();
	const avs::DecoderStats vidStats = clientPipeline.decoder.GetStats();

	deviceContext.framePrintX = 8;
	deviceContext.framePrintY = 8;
	gui.LinePrint(sessionClient->IsConnected()? platform::core::QuickFormat("Client %d connected to: %s, port %d"
		, sessionClient->GetClientID(),sessionClient->GetServerIP().c_str(),sessionClient->GetPort()):
		(canConnect?platform::core::QuickFormat("Not connected. Discovering %s port %d", server_ip.c_str(), server_discovery_port):"Offline"),white);
	gui.LinePrint( platform::core::QuickFormat("Framerate: %4.4f", framerate));

	if(show_osd== clientrender::NETWORK_OSD)
	{
		gui.LinePrint( platform::core::QuickFormat("Start timestamp: %d", clientPipeline.pipeline.GetStartTimestamp()));
		gui.LinePrint( platform::core::QuickFormat("Current timestamp: %d",clientPipeline.pipeline.GetTimestamp()));
		gui.LinePrint( platform::core::QuickFormat("Bandwidth KBs: %4.2f", counters.bandwidthKPS));
		gui.LinePrint( platform::core::QuickFormat("Network packets received: %d", counters.networkPacketsReceived));
		gui.LinePrint( platform::core::QuickFormat("Decoder packets received: %d", counters.decoderPacketsReceived));
		gui.LinePrint( platform::core::QuickFormat("Network packets dropped: %d", counters.networkPacketsDropped));
		gui.LinePrint( platform::core::QuickFormat("Decoder packets dropped: %d", counters.decoderPacketsDropped)); 
		gui.LinePrint( platform::core::QuickFormat("Decoder packets incomplete: %d", counters.incompleteDecoderPacketsReceived));
		gui.LinePrint( platform::core::QuickFormat("Decoder packets per sec: %4.2f", counters.decoderPacketsReceivedPerSec));
		gui.LinePrint( platform::core::QuickFormat("Video frames received per sec: %4.2f", vidStats.framesReceivedPerSec));
		gui.LinePrint( platform::core::QuickFormat("Video frames parseed per sec: %4.2f", vidStats.framesProcessedPerSec));
		gui.LinePrint( platform::core::QuickFormat("Video frames displayed per sec: %4.2f", vidStats.framesDisplayedPerSec));
	}
	else if(show_osd== clientrender::CAMERA_OSD)
	{
		vec3 offset=camera.GetPosition();
		gui.LinePrint( receivedInitialPos?(platform::core::QuickFormat("Origin: %4.4f %4.4f %4.4f", clientDeviceState->originPose.position.x, clientDeviceState->originPose.position.y, clientDeviceState->originPose.position.z)):"Origin:", white);
		gui.LinePrint(  platform::core::QuickFormat(" Local: %4.4f %4.4f %4.4f", clientDeviceState->headPose.localPose.position.x, clientDeviceState->headPose.localPose.position.y, clientDeviceState->headPose.localPose.position.z),white);
		gui.LinePrint(  platform::core::QuickFormat(" Final: %4.4f %4.4f %4.4f\n", clientDeviceState->headPose.globalPose.position.x, clientDeviceState->headPose.globalPose.position.y, clientDeviceState->headPose.globalPose.position.z),white);
		if (videoPosDecoded)
		{
			gui.LinePrint( platform::core::QuickFormat(" Video: %4.4f %4.4f %4.4f", videoPos.x, videoPos.y, videoPos.z), white);
		}	
		else
		{
			gui.LinePrint( platform::core::QuickFormat(" Video: -"), white);
		}
	}
	else if(show_osd== clientrender::GEOMETRY_OSD)
	{
		std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
		gui.LinePrint( platform::core::QuickFormat("Nodes: %d",geometryCache.mNodeManager->GetNodeAmount()), white);

		static int nodeLimit = 5;
		auto& rootNodes = geometryCache.mNodeManager->GetRootNodes();
		static int lineLimit = 50;

		gui.LinePrint( platform::core::QuickFormat("Meshes: %d\nLights: %d", geometryCache.mMeshManager.GetCache(cacheLock).size(),
						geometryCache.mLightManager.GetCache(cacheLock).size()), white);

		gui.Anims(geometryCache.mAnimationManager);

		gui.NodeTree( rootNodes);

		auto &missing=geometryCache.m_MissingResources;
		if(missing.size())
		{
			gui.LinePrint( platform::core::QuickFormat("Missing Resources"));
			for(const auto& missingPair : missing)
			{
				const clientrender::MissingResource& missingResource = missingPair.second;
				std::string txt= platform::core::QuickFormat("\t%s %d from ", stringof(missingResource.resourceType), missingResource.id);
				for(auto u:missingResource.waitingResources)
				{
					auto type= u.get()->type;
					avs::uid id=u.get()->id;
					if(type==avs::GeometryPayloadType::Node)
					{
						txt+="Node ";
						auto n = geometryCache.mNodeManager->GetNode(id);
						if(n)
							txt += n->name;
					}
					txt+=platform::core::QuickFormat("%d, ",(uint64_t)id);
				}
				gui.LinePrint( txt.c_str());
			}
		}
	}
	else if(show_osd== clientrender::TAG_OSD)
	{
		std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
		auto& cachedLights = geometryCache.mLightManager.GetCache(cacheLock);
		const char *name="";
		gui.LinePrint("Tags\n");
		for(int i=0;i<videoTagDataCubeArray.size();i++)
		{
			auto &tag=videoTagDataCubeArray[i];
			gui.LinePrint(platform::core::QuickFormat("%d lights",tag.coreData.lightCount));

			auto *gpu_tag_buffer=videoTagDataCube;
			if(gpu_tag_buffer)
			for(int j=0;j<tag.lights.size();j++)
			{
				auto &l=tag.lights[j];
				auto &t=gpu_tag_buffer[j];
				const LightTag &lightTag=t.lightTags[j];
				vec4 clr={l.color.x,l.color.y,l.color.z,1.0f};
				 
				auto& c = cachedLights[l.uid];
				if (c.resource)
				{
					auto& lcr =c.resource->GetLightCreateInfo();
					name=lcr.name.c_str();
				}
				if(l.lightType==clientrender::LightType::Directional)
					gui.LinePrint(platform::core::QuickFormat("%llu: %s, Type: %s, dir: %3.3f %3.3f %3.3f clr: %3.3f %3.3f %3.3f",l.uid,name,ToString((clientrender::Light::Type)l.lightType)
						,lightTag.direction.x,lightTag.direction.y,lightTag.direction.z
						,l.color.x,l.color.y,l.color.z),clr);
				else
					gui.LinePrint(platform::core::QuickFormat("%llu: %s, Type: %s, pos: %3.3f %3.3f %3.3f clr: %3.3f %3.3f %3.3f",l.uid, name, ToString((clientrender::Light::Type)l.lightType)
						,lightTag.position.x
						,lightTag.position.y
						,lightTag.position.z
						,l.color.x,l.color.y,l.color.z),clr);

			}
		}
	}
	else if(show_osd== clientrender::CONTROLLER_OSD)
	{
		gui.LinePrint( "CONTROLS\n");
		if(openXR)
		{
			gui.LinePrint(openXR->GetDebugString().c_str());
		}
		else
		{
		#ifdef _MSC_VER
			gui.LinePrint( platform::core::QuickFormat("     Shift: %d ",keydown[VK_SHIFT]));
		#endif
			gui.LinePrint( platform::core::QuickFormat("     W %d A %d S %d D %d",keydown['w'],keydown['a'],keydown['s'],keydown['d']));
			gui.LinePrint( platform::core::QuickFormat("     Mouse: %d %d %3.3d",mouseCameraInput.MouseX,mouseCameraInput.MouseY,mouseCameraState.right_left_spd));
			gui.LinePrint( platform::core::QuickFormat("      btns: %d",mouseCameraInput.MouseButtons));
		
			gui.LinePrint( platform::core::QuickFormat("   view_dir: %3.3f %3.3f %3.3f", controllerSim.view_dir.x, controllerSim.view_dir.y, controllerSim.view_dir.z));

			gui.LinePrint( platform::core::QuickFormat("   position: %3.3f %3.3f %3.3f", controllerSim.position[0].x, controllerSim.position[0].y, controllerSim.position[0].z));
			gui.LinePrint( platform::core::QuickFormat("           : %3.3f %3.3f %3.3f", controllerSim.position[1].x, controllerSim.position[1].y, controllerSim.position[1].z));

			gui.LinePrint( platform::core::QuickFormat("orientation: %3.3f %3.3f %3.3f", controllerSim.orientation[0].x, controllerSim.orientation[0].y, controllerSim.orientation[0].z, controllerSim.orientation[0].w));
			gui.LinePrint( platform::core::QuickFormat("           : %3.3f %3.3f %3.3f", controllerSim.orientation[1].x, controllerSim.orientation[1].y, controllerSim.orientation[1].z, controllerSim.orientation[1].w));
			gui.LinePrint( platform::core::QuickFormat("        dir: %3.3f %3.3f %3.3f", controllerSim.controller_dir.x, controllerSim.controller_dir.y, controllerSim.controller_dir.z));
			gui.LinePrint( platform::core::QuickFormat("      angle: %3.3f", controllerSim.angle));
			gui.LinePrint( platform::core::QuickFormat(" con offset: %3.3f %3.3f %3.3f", controllerSim.pos_offset[0].x, controllerSim.pos_offset[0].y, controllerSim.pos_offset[0].z));
			gui.LinePrint( platform::core::QuickFormat("           : %3.3f %3.3f %3.3f", controllerSim.pos_offset[1].x, controllerSim.pos_offset[1].y, controllerSim.pos_offset[1].z));
		}
	}
	gui.EndDebugGui(deviceContext);

	//ImGui::PlotLines("Jitter buffer length", statJitterBuffer.data(), statJitterBuffer.count(), 0, nullptr, 0.0f, 100.0f);
	//ImGui::PlotLines("Jitter buffer push calls", statJitterPush.data(), statJitterPush.count(), 0, nullptr, 0.0f, 5.0f);
	//ImGui::PlotLines("Jitter buffer pop calls", statJitterPop.data(), statJitterPop.count(), 0, nullptr, 0.0f, 5.0f);
}


std::vector<uid> Renderer::GetGeometryResources()
{
	return geometryCache.GetAllResourceIDs();
}

void Renderer::ClearGeometryResources()
{
	geometryCache.Clear();
	resourceCreator.Clear();
}

void Renderer::SetExternalTexture(platform::crossplatform::Texture* t)
{
	externalTexture = t;
	have_vr_device = (externalTexture != nullptr);
}

void Renderer::PrintHelpText(platform::crossplatform::GraphicsDeviceContext& deviceContext)
{
	deviceContext.framePrintY = 8;
	deviceContext.framePrintX = hdrFramebuffer->GetWidth() / 2;
	renderPlatform->LinePrint(deviceContext, "K: Connect/Disconnect");
	renderPlatform->LinePrint(deviceContext, "O: Toggle OSD");
	renderPlatform->LinePrint(deviceContext, "V: Show video");
	renderPlatform->LinePrint(deviceContext, "C: Toggle render from centre");
	renderPlatform->LinePrint(deviceContext, "T: Toggle Textures");
	renderPlatform->LinePrint(deviceContext, "M: Change rendermode");
	renderPlatform->LinePrint(deviceContext, "R: Recompile shaders");
	renderPlatform->LinePrint(deviceContext, "NUM 0: PBR");
	renderPlatform->LinePrint(deviceContext, "NUM 1: Albedo");
	renderPlatform->LinePrint(deviceContext, "NUM 4: Unswizzled Normals");
	renderPlatform->LinePrint(deviceContext, "NUM 5: Debug animation");
	renderPlatform->LinePrint(deviceContext, "NUM 6: Lightmaps");
	renderPlatform->LinePrint(deviceContext, "NUM 2: Vertex Normals");
}


void Renderer::OnLightingSetupChanged(const avs::SetupLightingCommand &l)
{
	lastSetupLightingCommand=l;
}

void Renderer::OnInputsSetupChanged(const std::vector<avs::InputDefinition> &inputDefinitions_)
{
	inputDefinitions = inputDefinitions_;
	if (openXR)
		openXR->OnInputsSetupChanged(server_uid,inputDefinitions_);
	// for each input, we will match it 
	inputIdMappings.clear();
	std::string leftButton = "/interaction_profiles/simul/mouse_ext/input/trigger/value";
	std::string rightButton = "/interaction_profiles/simul/mouse_ext/input/a/click";
	std::string middleButton = "/interaction_profiles/simul/mouse_ext/input/b/click";
	for (const auto& d : inputDefinitions)
	{
		if (Match(leftButton, d.regexPath))
		{
			inputIdMappings[MouseOrKey::LEFT_BUTTON] = d.inputId;
		}
		if (Match(rightButton, d.regexPath))
		{
			inputIdMappings[MouseOrKey::RIGHT_BUTTON] = d.inputId;
		}
		if (Match(middleButton, d.regexPath))
		{
			inputIdMappings[MouseOrKey::MIDDLE_BUTTON] = d.inputId;
		}
	}
}

avs::DecoderBackendInterface* Renderer::CreateVideoDecoder()
{
	AVSTextureHandle th = avsTexture;
	AVSTextureImpl* t = static_cast<AVSTextureImpl*>(th.get());
	return new VideoDecoder(renderPlatform, t->texture);
}

bool Renderer::OnSetupCommandReceived(const char *server_ip,const avs::SetupCommand &setupCommand,avs::Handshake &handshake)
{
	ConfigureVideo(setupCommand.video_config);

	TELEPORT_CLIENT_WARN("SETUP COMMAND RECEIVED: server_streaming_port %d clr %d x %d dpth %d x %d\n", setupCommand.server_streaming_port, clientPipeline.videoConfig.video_width, clientPipeline.videoConfig.video_height
																	, clientPipeline.videoConfig.depth_width, clientPipeline.videoConfig.depth_height	);
	videoPosDecoded=false;

	videoTagDataCubeArray.clear();
	videoTagDataCubeArray.resize(maxTagDataSize);

	teleport::client::ServerTimestamp::setLastReceivedTimestampUTCUnixMs(setupCommand.startTimestamp_utc_unix_ms);
	sessionClient->SetPeerTimeout(setupCommand.idle_connection_timeout);

	const uint32_t geoStreamID = 80;
	std::vector<avs::NetworkSourceStream> streams = { { 20 }, { 40 } };
	if (AudioStream)
	{
		streams.push_back({ 60 });
	}
	if (GeoStream)
	{
		streams.push_back({ geoStreamID });
	}

	avs::NetworkSourceParams sourceParams;
	sourceParams.connectionTimeout = setupCommand.idle_connection_timeout;
	sourceParams.remoteIP = server_ip;
	sourceParams.remotePort = setupCommand.server_streaming_port;
	sourceParams.remoteHTTPPort = setupCommand.server_http_port;
	sourceParams.maxHTTPConnections = 10;
	sourceParams.httpStreamID = geoStreamID;
	sourceParams.useSSL = setupCommand.using_ssl;

	// Configure for video stream, tag data stream, audio stream and geometry stream.
	if (!clientPipeline.source.configure(std::move(streams), sourceParams))
	{
		TELEPORT_BREAK_ONCE("Failed to configure network source node\n");
		return false;
	}

	clientPipeline.source.setDebugStream(setupCommand.debug_stream);
	clientPipeline.source.setDoChecksums(setupCommand.do_checksums);
	clientPipeline.source.setDebugNetworkPackets(setupCommand.debug_network_packets);

	//test
	//avs::HTTPPayloadRequest req;
	//req.fileName = "meshes/engineering/Cube_Cube.mesh";
	//req.type = avs::FilePayloadType::Mesh;
	//clientPipeline.source.GetHTTPRequestQueue().emplace(std::move(req));

	bodyOffsetFromHead = setupCommand.bodyOffsetFromHead;
	avs::ConvertPosition(setupCommand.axesStandard, avs::AxesStandard::EngineeringStyle, bodyOffsetFromHead);
	
	clientPipeline.decoderParams.deferDisplay = false;
	clientPipeline.decoderParams.decodeFrequency = avs::DecodeFrequency::NALUnit;
	clientPipeline.decoderParams.codec = clientPipeline.videoConfig.videoCodec;
	clientPipeline.decoderParams.use10BitDecoding = clientPipeline.videoConfig.use_10_bit_decoding;
	clientPipeline.decoderParams.useYUV444ChromaFormat = clientPipeline.videoConfig.use_yuv_444_decoding;
	clientPipeline.decoderParams.useAlphaLayerDecoding = clientPipeline.videoConfig.use_alpha_layer_decoding;

	avs::DeviceHandle dev;
	
#if TELEPORT_CLIENT_USE_D3D12
	dev.handle = renderPlatform->AsD3D12Device();
	dev.type = avs::DeviceType::Direct3D12;
#elif TELEPORT_CLIENT_USE_D3D11
	dev.handle = renderPlatform->AsD3D11Device();
	dev.type = avs::DeviceType::Direct3D11;
#else
	dev.handle = renderPlatform->AsVulkanDevice();
	dev.type = avs::DeviceType::Vulkan;
#endif

	clientPipeline.pipeline.reset();
	// Top of the clientPipeline.pipeline, we have the network clientPipeline.source.
	clientPipeline.pipeline.add(&clientPipeline.source);

	AVSTextureImpl* ti = (AVSTextureImpl*)(avsTexture.get());
	if (ti)
	{
		SAFE_DELETE(ti->texture);
	}
	
	/* Now for each stream, we add both a DECODER and a SURFACE node. e.g. for two streams:
					 /->decoder -> surface
			source -<
					 \->decoder	-> surface
	*/
	size_t stream_width = clientPipeline.videoConfig.video_width;
	size_t stream_height = clientPipeline.videoConfig.video_height;

	if (clientPipeline.videoConfig.use_cubemap)
	{
		if(clientPipeline.videoConfig.colour_cubemap_size)
			videoTexture->ensureTextureArraySizeAndFormat(renderPlatform, clientPipeline.videoConfig.colour_cubemap_size, clientPipeline.videoConfig.colour_cubemap_size, 1, 1,
				crossplatform::PixelFormat::RGBA_32_FLOAT, true, false, true);
	}
	else
	{
		videoTexture->ensureTextureArraySizeAndFormat(renderPlatform, clientPipeline.videoConfig.perspective_width, clientPipeline.videoConfig.perspective_height, 1, 1,
			crossplatform::PixelFormat::RGBA_32_FLOAT, true, false, false);
	}
	specularCubemapTexture->ensureTextureArraySizeAndFormat(renderPlatform, clientPipeline.videoConfig.specular_cubemap_size, clientPipeline.videoConfig.specular_cubemap_size, 1, clientPipeline.videoConfig.specular_mips, crossplatform::PixelFormat::RGBA_8_UNORM, true, false, true);
	diffuseCubemapTexture->ensureTextureArraySizeAndFormat(renderPlatform, clientPipeline.videoConfig.diffuse_cubemap_size, clientPipeline.videoConfig.diffuse_cubemap_size, 1, 1,
		crossplatform::PixelFormat::RGBA_8_UNORM, true, false, true);

	const float aspect = setupCommand.video_config.perspective_width / static_cast<float>(setupCommand.video_config.perspective_height);
	const float horzFOV = setupCommand.video_config.perspective_fov * clientrender::DEG_TO_RAD;
	const float vertFOV = clientrender::GetVerticalFOVFromHorizontal(horzFOV, aspect);

	cubemapConstants.serverProj = crossplatform::Camera::MakeDepthReversedProjectionMatrix(horzFOV, vertFOV, 0.01f, 0);

	colourOffsetScale.x = 0;
	colourOffsetScale.y = 0;
	colourOffsetScale.z = 1.0f;
	colourOffsetScale.w = float(clientPipeline.videoConfig.video_height) / float(stream_height);

	
	CreateTexture(avsTexture, int(stream_width), int(stream_height));

// Set to a custom backend that uses platform api video decoder if using D3D12 and non NVidia card. 
#if TELEPORT_CLIENT_USE_PLATFORM_VIDEO_DECODER
	clientPipeline.decoder.setBackend(CreateVideoDecoder());
#endif

	// Video streams are 0+...
	if (!clientPipeline.decoder.configure(dev, (int)stream_width, (int)stream_height, clientPipeline.decoderParams, 20))
	{
		TELEPORT_CERR << "Failed to configure decoder node!\n";
	}
	if (!clientPipeline.surface.configure(avsTexture->createSurface()))
	{
		TELEPORT_CERR << "Failed to configure output surface node!\n";
	}

	clientPipeline.videoQueue.configure(300000, 16, "VideoQueue");

	avs::PipelineNode::link(clientPipeline.source, clientPipeline.videoQueue);
	avs::PipelineNode::link(clientPipeline.videoQueue, clientPipeline.decoder);
	clientPipeline.pipeline.link({ &clientPipeline.decoder, &clientPipeline.surface });
	
	// Tag Data
	{
		auto f = std::bind(&Renderer::OnReceiveVideoTagData, this, std::placeholders::_1, std::placeholders::_2);
		if (!clientPipeline.tagDataDecoder.configure(40, f))
		{
			TELEPORT_CERR << "Failed to configure video tag data decoder node!\n";
		}

		clientPipeline.tagDataQueue.configure(200, 16, "clientPipeline.tagDataQueue");

		avs::PipelineNode::link(clientPipeline.source, clientPipeline.tagDataQueue);
		clientPipeline.pipeline.link({ &clientPipeline.tagDataQueue, &clientPipeline.tagDataDecoder });
	}

	// Audio
	if (AudioStream)
	{
		clientPipeline.avsAudioDecoder.configure(60);
		sca::AudioSettings audioSettings;
		audioSettings.codec = sca::AudioCodec::PCM;
		audioSettings.numChannels = 1;
		audioSettings.sampleRate = 48000;
		audioSettings.bitsPerSample = 32;
		// This will be deconfigured automatically when the clientPipeline.pipeline is deconfigured.
		#ifdef _MSC_VER
		audioPlayer.configure(audioSettings);
		audioStreamTarget.reset(new sca::AudioStreamTarget(&audioPlayer));
		#endif
		clientPipeline.avsAudioTarget.configure(audioStreamTarget.get());

		clientPipeline.audioQueue.configure(4096, 120, "AudioQueue");

		avs::PipelineNode::link(clientPipeline.source, clientPipeline.audioQueue);
		avs::PipelineNode::link(clientPipeline.audioQueue, clientPipeline.avsAudioDecoder);
		clientPipeline.pipeline.link({ &clientPipeline.avsAudioDecoder, &clientPipeline.avsAudioTarget });

		// Audio Input
		if (setupCommand.audio_input_enabled)
		{
			sca::NetworkSettings networkSettings =
			{
					setupCommand.server_streaming_port + 1, server_ip, setupCommand.server_streaming_port
					, static_cast<int32_t>(handshake.maxBandwidthKpS)
					, static_cast<int32_t>(handshake.udpBufferSize)
					, setupCommand.requiredLatencyMs
					, (int32_t)setupCommand.idle_connection_timeout
			};

			audioInputNetworkPipeline.reset(new sca::NetworkPipeline());
			audioInputQueue.configure(4096, 120, "AudioInputQueue");
			audioInputNetworkPipeline->initialise(networkSettings, &audioInputQueue);

			// The callback will be called when audio input is received.
			auto f = [this](const uint8_t* data, size_t dataSize) -> void
			{
				size_t bytesWritten;
				if (audioInputQueue.write(nullptr, data, dataSize, bytesWritten))
				{
					audioInputNetworkPipeline->process();
				}
			};
		#ifdef _MSC_VER
			// The audio player will stop recording automatically when deconfigured. 
			audioPlayer.startRecording(f);
		#endif
		}
	}

	// We will add a GEOMETRY PIPE
	if(GeoStream)
	{
		clientPipeline.avsGeometryDecoder.configure(80, &geometryDecoder);
		clientPipeline.avsGeometryTarget.configure(&resourceCreator);

		clientPipeline.geometryQueue.configure(10000, 200, "clientPipeline.geometryQueue");

		avs::PipelineNode::link(clientPipeline.source, clientPipeline.geometryQueue);
		avs::PipelineNode::link(clientPipeline.geometryQueue, clientPipeline.avsGeometryDecoder);
		clientPipeline.pipeline.link({ &clientPipeline.avsGeometryDecoder, &clientPipeline.avsGeometryTarget });
	}

	handshake.startDisplayInfo.width = hdrFramebuffer->GetWidth();
	handshake.startDisplayInfo.height = hdrFramebuffer->GetHeight();
	handshake.axesStandard = avs::AxesStandard::EngineeringStyle;
	handshake.MetresPerUnit = 1.0f;
	handshake.FOV = 90.0f;
	handshake.isVR = false;
	handshake.framerate = 60;
	handshake.udpBufferSize = static_cast<uint32_t>(clientPipeline.source.getSystemBufferSize());
	handshake.maxBandwidthKpS = handshake.udpBufferSize * handshake.framerate;
	handshake.maxLightsSupported=10;
	handshake.clientStreamingPort = setupCommand.server_streaming_port + 1;
	lastSetupCommand = setupCommand;

	//java->Env->CallVoidMethod(java->ActivityObject, jni.initializeVideoStreamMethod, port, width, height, mVideoSurfaceTexture->GetJavaObject());
	return true;
}

void Renderer::OnVideoStreamClosed()
{
	TELEPORT_CLIENT_WARN("VIDEO STREAM CLOSED\n");
	clientPipeline.pipeline.deconfigure();
	clientPipeline.videoQueue.deconfigure();
	clientPipeline.audioQueue.deconfigure();
	clientPipeline.geometryQueue.deconfigure();

	//const ovrJava* java = app->GetJava();
	//java->Env->CallVoidMethod(java->ActivityObject, jni.closeVideoStreamMethod);

	receivedInitialPos = 0;
}

void Renderer::OnReconfigureVideo(const avs::ReconfigureVideoCommand& reconfigureVideoCommand)
{
	clientPipeline.videoConfig = reconfigureVideoCommand.video_config;

	TELEPORT_CLIENT_WARN("VIDEO STREAM RECONFIGURED: clr %d x %d dpth %d x %d", clientPipeline.videoConfig.video_width, clientPipeline.videoConfig.video_height
		, clientPipeline.videoConfig.depth_width, clientPipeline.videoConfig.depth_height);

	clientPipeline.decoderParams.deferDisplay = false;
	clientPipeline.decoderParams.decodeFrequency = avs::DecodeFrequency::NALUnit;
	clientPipeline.decoderParams.codec = clientPipeline.videoConfig.videoCodec;
	clientPipeline.decoderParams.use10BitDecoding = clientPipeline.videoConfig.use_10_bit_decoding;
	clientPipeline.decoderParams.useYUV444ChromaFormat = clientPipeline.videoConfig.use_yuv_444_decoding;
	clientPipeline.decoderParams.useAlphaLayerDecoding = clientPipeline.videoConfig.use_alpha_layer_decoding;

	avs::DeviceHandle dev;
#if TELEPORT_CLIENT_USE_D3D12
	dev.handle = renderPlatform->AsD3D12Device();;
	dev.type = avs::DeviceType::Direct3D12;
#else
	dev.handle = renderPlatform->AsD3D11Device();
	dev.type = avs::DeviceType::Direct3D11;
#endif

	size_t stream_width = clientPipeline.videoConfig.video_width;
	size_t stream_height = clientPipeline.videoConfig.video_height;

	if (clientPipeline.videoConfig.use_cubemap)
	{
		videoTexture->ensureTextureArraySizeAndFormat(renderPlatform, clientPipeline.videoConfig.colour_cubemap_size, clientPipeline.videoConfig.colour_cubemap_size, 1, 1,
			crossplatform::PixelFormat::RGBA_32_FLOAT, true, false, true);
	}
	else
	{
		videoTexture->ensureTextureArraySizeAndFormat(renderPlatform, clientPipeline.videoConfig.perspective_width, clientPipeline.videoConfig.perspective_height, 1, 1,
			crossplatform::PixelFormat::RGBA_32_FLOAT, true, false, false);
	}

	colourOffsetScale.x = 0;
	colourOffsetScale.y = 0;
	colourOffsetScale.z = 1.0f;
	colourOffsetScale.w = float(clientPipeline.videoConfig.video_height) / float(stream_height);

	AVSTextureImpl* ti = (AVSTextureImpl*)(avsTexture.get());
	// Only create new texture and register new surface if resolution has changed
	if (ti && ti->texture->GetWidth() != stream_width || ti->texture->GetLength() != stream_height)
	{
		SAFE_DELETE(ti->texture);

		if (!clientPipeline.decoder.unregisterSurface())
		{
			throw std::runtime_error("Failed to unregister decoder surface");
		}

		CreateTexture(avsTexture, int(stream_width), int(stream_height));
	}

	if (!clientPipeline.decoder.reconfigure((int)stream_width, (int)stream_height, clientPipeline.decoderParams))
	{
		throw std::runtime_error("Failed to reconfigure decoder");
	}
	
	lastSetupCommand.video_config = reconfigureVideoCommand.video_config;
}

void Renderer::HandleLocalInputs(const teleport::core::Input& local_inputs)
{
	for(const  auto &i:local_inputs.binaryEvents)
	{
		if(i.inputID==local_menu_input_id)
		{
			// do this on *releasing* the button:
			if(i.activated==false)
				gui.ShowHide();
		}
	}
}