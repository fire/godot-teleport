//
// Created by roder on 06/04/2020.
//
#include "ClientRenderer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <GLES3/gl32.h>

#include "OVR_Math.h"
#include <VrApi_Types.h>
#include <VrApi_Input.h>
#include <VrApi_Helpers.h>

#include "TeleportClient/ServerTimestamp.h"

#include "ClientDeviceState.h"
#include "OVRNode.h"
#include "OVRNodeManager.h"
#include "Config.h"
#include "AndroidDiscoveryService.h"
#include "VideoSurface.h"
#include "AudioCommon.h"

using namespace OVR;
using namespace OVRFW;
using namespace teleport;
using namespace client;

ovrQuatf QuaternionMultiply(const ovrQuatf &p, const ovrQuatf &q)
{
	ovrQuatf r;
	r.w = p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z;
	r.x = p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y;
	r.y = p.w * q.y + p.y * q.w + p.z * q.x - p.x * q.z;
	r.z = p.w * q.z + p.z * q.w + p.x * q.y - p.y * q.x;
	return r;
}
static const char *ToString(clientrender::Light::Type type)
{
	const char *lightTypeName = "";
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
		case clientrender::Light::Type::DISC:
			lightTypeName = " Disc";
			break;
		default:
			lightTypeName = "UNKNOWN";
			break;
	};
	return lightTypeName;
}

avs::vec3 QuaternionTimesVector(const ovrQuatf &q, const avs::vec3 &vec)
{
	const float &x0 = vec.x;
	const float &y0 = vec.y;
	const float &z0 = vec.z;
	float s1 = q.x * x0 + q.y * y0 + q.z * z0;
	float x1 = q.w * x0 + q.y * z0 - q.z * y0;
	float y1 = q.w * y0 + q.z * x0 - q.x * z0;
	float z1 = q.w * z0 + q.x * y0 - q.y * x0;
	avs::vec3 ret = {s1 * q.x + q.w * x1 + q.y * z1 - q.z * y1, s1 * q.y + q.w * y1 + q.z * x1
																- q.x * z1, s1 * q.z + q.w * z1
																			+ q.x * y1 - q.y * x1};
	return ret;
}

ClientRenderer::ClientRenderer
(
    ClientAppInterface *c,
    teleport::client::ClientDeviceState *s,
    Controllers *cn
    ,SessionCommandInterface *sc
)
    :Renderer(new OVRNodeManager(),new OVRNodeManager())
    	,controllers(cn)
		,clientAppInterface(c)
		,sessionClient(sc, std::make_unique<AndroidDiscoveryService>())
		,clientDeviceState(s)
		,mShowWebcam(true)
{

	sessionClient.SetResourceCreator(&resourceCreator);
	sessionClient.SetGeometryCache(&geometryCache);
}

ClientRenderer::~ClientRenderer()
{
	ExitedVR();
}

void ClientRenderer::EnteredVR(const ovrJava *java)
{
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();

	resourceCreator.Initialize((&globalGraphicsResources.renderPlatform),
			clientrender::VertexBufferLayout::PackingStyle::INTERLEAVED);
	resourceCreator.SetGeometryCache(&geometryCache);
	//VideoSurfaceProgram
	{
		{
			mVideoUB = globalGraphicsResources.renderPlatform.InstantiateUniformBuffer();
			clientrender::UniformBuffer::UniformBufferCreateInfo uniformBufferCreateInfo = {"mVideoUB",1
					, sizeof(VideoUB)
					, &videoUB};
			mVideoUB->Create(&uniformBufferCreateInfo);
		}
		static ovrProgramParm uniformParms[] =
				{
						  {"renderTexture", ovrProgramParmType::TEXTURE_SAMPLED}
						, {"videoUB"       , ovrProgramParmType::BUFFER_UNIFORM}
						, {"TagDataCube"   , ovrProgramParmType::BUFFER_STORAGE}
						,};

		// Cubemap
		{
			std::string videoSurfaceVert = clientAppInterface->LoadTextFile(
					"shaders/VideoSurfaceSphere.vert");
			std::string videoSurfaceFrag = clientAppInterface->LoadTextFile(
					"shaders/VideoSurfaceSphere.frag");
			mCubeVideoSurfaceProgram = GlProgram::Build(
					nullptr, videoSurfaceVert.c_str(),
					"#extension GL_OES_EGL_image_external_essl3 : require\n",
					videoSurfaceFrag.c_str(),
					uniformParms, sizeof(uniformParms) / sizeof(ovrProgramParm), 310);
			if (!mCubeVideoSurfaceProgram.IsValid()) {
				OVR_FAIL("Failed to build video surface shader program for cubemap rendering");
			}
		}
		// Perspective
		{
			std::string videoSurfaceVert = clientAppInterface->LoadTextFile(
					"shaders/VideoSurfaceSpherePersp.vert");
			std::string videoSurfaceFrag = clientAppInterface->LoadTextFile(
					"shaders/VideoSurfaceSpherePersp.frag");
			m2DVideoSurfaceProgram = GlProgram::Build(
					nullptr, videoSurfaceVert.c_str(),
					"#extension GL_OES_EGL_image_external_essl3 : require\n",
					videoSurfaceFrag.c_str(),
					uniformParms, sizeof(uniformParms) / sizeof(ovrProgramParm), 310);
			if (!m2DVideoSurfaceProgram.IsValid()) {
				OVR_FAIL("Failed to build video surface shader program for perspective rendering");
			}
		}
	}
	{
		mVideoSurfaceTexture = new OVRFW::SurfaceTexture(java->Env);
        mAlphaSurfaceTexture = new OVRFW::SurfaceTexture(java->Env);
		mVideoTexture = globalGraphicsResources.renderPlatform.InstantiateTexture();
		mAlphaVideoTexture = globalGraphicsResources.renderPlatform.InstantiateTexture();
		mCubemapUB = globalGraphicsResources.renderPlatform.InstantiateUniformBuffer();
		mRenderTexture = globalGraphicsResources.renderPlatform.InstantiateTexture();
		diffuseCubemapTexture = globalGraphicsResources.renderPlatform.InstantiateTexture();
		specularCubemapTexture = globalGraphicsResources.renderPlatform.InstantiateTexture();

		mCubemapLightingTexture = globalGraphicsResources.renderPlatform.InstantiateTexture();
		mTagDataIDBuffer = globalGraphicsResources.renderPlatform.InstantiateShaderStorageBuffer();
		mTagDataArrayBuffer = globalGraphicsResources.renderPlatform.InstantiateShaderStorageBuffer();
	}
	// Tag Data ID
	{
		clientrender::ShaderStorageBuffer::ShaderStorageBufferCreateInfo shaderStorageBufferCreateInfo = {
				0, clientrender::ShaderStorageBuffer::Access::NONE, sizeof(clientrender::uvec4), (void *) &mTagDataID
		};
		mTagDataIDBuffer->Create(&shaderStorageBufferCreateInfo);
	}


	// Tag Data Cube Buffer
	VideoTagDataCube shaderTagDataCubeArray[MAX_TAG_DATA_COUNT];
	shaderTagDataCubeArray[0].cameraPosition.x = 1.0f;
	clientrender::ShaderStorageBuffer::ShaderStorageBufferCreateInfo tagBufferCreateInfo = {
			1, clientrender::ShaderStorageBuffer::Access::READ_WRITE_BIT, sizeof(VideoTagDataCube)
			, (void *) nullptr
	};
	globalGraphicsResources.mTagDataBuffer->Create(&tagBufferCreateInfo);

	clientrender::ShaderStorageBuffer::ShaderStorageBufferCreateInfo arrayBufferCreateInfo = {
			2, clientrender::ShaderStorageBuffer::Access::READ_WRITE_BIT, sizeof(VideoTagDataCube)
																 * MAX_TAG_DATA_COUNT
			, (void *) &shaderTagDataCubeArray
	};
	mTagDataArrayBuffer->Create(&arrayBufferCreateInfo);


	{
		CopyCubemapSrc = clientAppInterface->LoadTextFile("shaders/CopyCubemap.comp");
		mCopyCubemapEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();
		mCopyCubemapWithDepthEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();
		mCopyCubemapWithAlphaLayerEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();
		mCopyPerspectiveEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();
        mCopyPerspectiveWithDepthEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();
		mExtractTagDataIDEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();
		mExtractOneTagEffect = globalGraphicsResources.renderPlatform.InstantiateEffect();

		clientrender::Effect::EffectCreateInfo effectCreateInfo = {};
		effectCreateInfo.effectName = "CopyCubemap";
		mCopyCubemapEffect->Create(&effectCreateInfo);

		effectCreateInfo.effectName = "CopyCubemapWithDepth";
		mCopyCubemapWithDepthEffect->Create(&effectCreateInfo);

		effectCreateInfo.effectName = "CopyCubemapWithAlphaLayer";
		mCopyCubemapWithAlphaLayerEffect->Create(&effectCreateInfo);

		effectCreateInfo.effectName = "CopyPerspective";
		mCopyPerspectiveEffect->Create(&effectCreateInfo);

        effectCreateInfo.effectName = "CopyPerspectiveWithDepth";
        mCopyPerspectiveWithDepthEffect->Create(&effectCreateInfo);

		effectCreateInfo.effectName = "ExtractTagDataID";
		mExtractTagDataIDEffect->Create(&effectCreateInfo);

		effectCreateInfo.effectName = "ExtractOneTag";
		mExtractOneTagEffect->Create(&effectCreateInfo);

		clientrender::ShaderSystem::PipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.m_Count = 1;
		pipelineCreateInfo.m_PipelineType = clientrender::ShaderSystem::PipelineType::PIPELINE_TYPE_COMPUTE;
		pipelineCreateInfo.m_ShaderCreateInfo[0].stage = clientrender::Shader::Stage::SHADER_STAGE_COMPUTE;
		pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "colour_only";
		pipelineCreateInfo.m_ShaderCreateInfo[0].filepath = "shaders/CopyCubemap.comp";
		pipelineCreateInfo.m_ShaderCreateInfo[0].sourceCode = CopyCubemapSrc;
		clientrender::ShaderSystem::Pipeline cp(&globalGraphicsResources.renderPlatform,
									   &pipelineCreateInfo);

		clientrender::Effect::EffectPassCreateInfo effectPassCreateInfo;
		effectPassCreateInfo.effectPassName = "CopyCubemap";
		effectPassCreateInfo.pipeline = cp;
		mCopyCubemapEffect->CreatePass(&effectPassCreateInfo);

		pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "colour_and_depth";
		clientrender::ShaderSystem::Pipeline cp2(&globalGraphicsResources.renderPlatform,
										&pipelineCreateInfo);

		effectPassCreateInfo.effectPassName = "ColourAndDepth";
		effectPassCreateInfo.pipeline = cp2;
		mCopyCubemapWithDepthEffect->CreatePass(&effectPassCreateInfo);

		pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "colour_and_alpha_layer";
		clientrender::ShaderSystem::Pipeline cp3(&globalGraphicsResources.renderPlatform,
										&pipelineCreateInfo);

		effectPassCreateInfo.effectPassName = "ColourAndAlphaLayer";
		effectPassCreateInfo.pipeline = cp3;
		mCopyCubemapWithAlphaLayerEffect->CreatePass(&effectPassCreateInfo);

		{
			std::string copyPerspectiveSrc = clientAppInterface->LoadTextFile(
					"shaders/CopyPerspective.comp");
			// pass to extract from the array into a single tag buffer:
			pipelineCreateInfo.m_ShaderCreateInfo[0].filepath = "shaders/CopyPerspective.comp";
			pipelineCreateInfo.m_ShaderCreateInfo[0].sourceCode = copyPerspectiveSrc;
			pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "colour_only";
			clientrender::ShaderSystem::Pipeline cp4(&globalGraphicsResources.renderPlatform,
											&pipelineCreateInfo);
			effectPassCreateInfo.effectPassName = "PerspectiveColour";
			effectPassCreateInfo.pipeline = cp4;
			mCopyPerspectiveEffect->CreatePass(&effectPassCreateInfo);

            pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "colour_and_depth";
            clientrender::ShaderSystem::Pipeline cp5(&globalGraphicsResources.renderPlatform,
                                            &pipelineCreateInfo);

            effectPassCreateInfo.effectPassName = "PerspectiveColourAndDepth";
            effectPassCreateInfo.pipeline = cp5;
            mCopyPerspectiveWithDepthEffect->CreatePass(&effectPassCreateInfo);
		}

		{
			ExtractTagDataIDSrc = clientAppInterface->LoadTextFile("shaders/ExtractTagDataID.comp");
			pipelineCreateInfo.m_ShaderCreateInfo[0].filepath = "shaders/ExtractTagDataID.comp";
			pipelineCreateInfo.m_ShaderCreateInfo[0].sourceCode = ExtractTagDataIDSrc;
			pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "extract_tag_data_id";
			clientrender::ShaderSystem::Pipeline cp6(&globalGraphicsResources.renderPlatform,
											&pipelineCreateInfo);
			effectPassCreateInfo.effectPassName = "ExtractTagDataID";
			effectPassCreateInfo.pipeline = cp6;
			mExtractTagDataIDEffect->CreatePass(&effectPassCreateInfo);

			std::string ExtractTagDataSrc = clientAppInterface->LoadTextFile(
					"shaders/ExtractOneTag.comp");
			// pass to extract from the array into a single tag buffer:
			pipelineCreateInfo.m_ShaderCreateInfo[0].filepath = "shaders/ExtractOneTag.comp";
			pipelineCreateInfo.m_ShaderCreateInfo[0].sourceCode = ExtractTagDataSrc;
			pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "extract_tag_data";
			clientrender::ShaderSystem::Pipeline cp7(&globalGraphicsResources.renderPlatform,
											&pipelineCreateInfo);
			effectPassCreateInfo.effectPassName = "ExtractOneTag";
			effectPassCreateInfo.pipeline = cp7;
			mExtractOneTagEffect->CreatePass(&effectPassCreateInfo);

			clientrender::UniformBuffer::UniformBufferCreateInfo uniformBufferCreateInfo = {"mCubemapUB",3
					, sizeof(CubemapUB)
					, &cubemapUB};
			mCubemapUB->Create(&uniformBufferCreateInfo);
		}
		GLCheckErrorsWithTitle("mCubemapUB:Create");

		clientrender::ShaderResourceLayout layout;
		layout.AddBinding(0, clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_IMAGE,
						  clientrender::Shader::Stage::SHADER_STAGE_COMPUTE);
		layout.AddBinding(1, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,
						  clientrender::Shader::Stage::SHADER_STAGE_COMPUTE);
		layout.AddBinding(2, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,
						  clientrender::Shader::Stage::SHADER_STAGE_COMPUTE);
		layout.AddBinding(3, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,
						  clientrender::Shader::Stage::SHADER_STAGE_COMPUTE);

		mColourAndDepthShaderResources.SetLayout(layout);
		mColourAndDepthShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_IMAGE, 0, "destTex",
				{globalGraphicsResources.cubeMipMapSampler, mRenderTexture, 0, uint32_t(-1)});
		mColourAndDepthShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 1,
				"videoFrameTexture", {globalGraphicsResources.sampler, mVideoTexture});
		mColourAndDepthShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 2,
				"alphaVideoFrameTexture", {globalGraphicsResources.sampler, mAlphaVideoTexture});
		mColourAndDepthShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 3, "cubemapUB",
				{mCubemapUB.get(), 0, mCubemapUB->GetUniformBufferCreateInfo().size});

		// We can't share same resources between copy cubemap effects because dest texture of below resources changes for different cases.
		mCopyCubemapShaderResources.SetLayout(layout);
		mCopyCubemapShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_IMAGE, 0,
				"destTex", {});
		mCopyCubemapShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 1,
				"videoFrameTexture", {globalGraphicsResources.sampler, mVideoTexture});
		mCopyCubemapShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 3, "cubemapUB",
				{mCubemapUB.get(), 0, mCubemapUB->GetUniformBufferCreateInfo().size});

        mCopyPerspectiveShaderResources.SetLayout(layout);
		mCopyPerspectiveShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_IMAGE, 0,
				"destTex", {globalGraphicsResources.sampler, mRenderTexture});
		mCopyPerspectiveShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 1,
				"videoFrameTexture", {globalGraphicsResources.sampler, mVideoTexture});
		mCopyPerspectiveShaderResources.AddImage(
                clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 2,
                "alphaVideoFrameTexture", {globalGraphicsResources.sampler, mAlphaVideoTexture});
		mCopyPerspectiveShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 3, "cubemapUB",
				{mCubemapUB.get(), 0, mCubemapUB->GetUniformBufferCreateInfo().size});

		mExtractTagShaderResources.SetLayout(layout);
		mExtractTagShaderResources.AddImage(
				clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER, 1,
				"videoFrameTexture", {globalGraphicsResources.sampler, mVideoTexture});
		mExtractTagShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 3, "cubemapUB",
				{mCubemapUB.get(), 0, mCubemapUB->GetUniformBufferCreateInfo().size});
		mExtractTagShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER, 0, "TagDataID",
				{mTagDataIDBuffer.get()});
		mExtractTagShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER, 1,
				"TagDataCube_ssbo", {globalGraphicsResources.mTagDataBuffer.get()});
		mExtractTagShaderResources.AddBuffer(
				clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER, 2,
				"TagDataCubeArray_ssbo", {mTagDataArrayBuffer.get()});


        mCopyCubemapWithAlphaLayerEffect->LinkShaders("ColourAndAlphaLayer",{mColourAndDepthShaderResources});
		mCopyCubemapWithDepthEffect->LinkShaders("ColourAndDepth",{mColourAndDepthShaderResources});
		mCopyCubemapEffect->LinkShaders("CopyCubemap", {mCopyCubemapShaderResources});
        mCopyPerspectiveWithDepthEffect->LinkShaders("PerspectiveColourAndDepth",{mCopyPerspectiveShaderResources});
		mCopyPerspectiveEffect->LinkShaders("PerspectiveColour",{mCopyPerspectiveShaderResources});
		mExtractTagDataIDEffect->LinkShaders("ExtractTagDataID", {mExtractTagShaderResources});
		mExtractOneTagEffect->LinkShaders("ExtractOneTag", {mExtractTagShaderResources});
	}

	mVideoSurfaceDef.surfaceName = "VideoSurface";
	mVideoSurfaceDef.geo = BuildGlobe(1.f, 1.f, 6.f,32,64);
	mVideoSurfaceDef.graphicsCommand.GpuState.depthEnable = true;
	mVideoSurfaceDef.graphicsCommand.GpuState.depthMaskEnable = false;
	mVideoSurfaceDef.graphicsCommand.GpuState.cullEnable = false;
	mVideoSurfaceDef.graphicsCommand.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_DISABLE;

	mWebcamResources.Init(clientAppInterface,"Webcam");
	mDebugTextureResources.Init(clientAppInterface,"FlatTexture");
	//Set up clientrender::Camera
	clientrender::Camera::CameraCreateInfo c_ci = {
			(clientrender::RenderPlatform *) (&globalGraphicsResources.renderPlatform)
			,clientrender::Camera::ProjectionType::PERSPECTIVE, clientrender::quat(0.0f, 0.0f, 0.0f, 1.0f)
			,clientDeviceState->headPose.globalPose.position, 5.0f
	};
	globalGraphicsResources.scrCamera = std::make_shared<clientrender::Camera>(&c_ci);

	clientrender::VertexBufferLayout layout;
	layout.AddAttribute(0, clientrender::VertexBufferLayout::ComponentCount::VEC3,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(1, clientrender::VertexBufferLayout::ComponentCount::VEC3,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(2, clientrender::VertexBufferLayout::ComponentCount::VEC4,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(3, clientrender::VertexBufferLayout::ComponentCount::VEC2,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(4, clientrender::VertexBufferLayout::ComponentCount::VEC2,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(5, clientrender::VertexBufferLayout::ComponentCount::VEC4,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(6, clientrender::VertexBufferLayout::ComponentCount::VEC4,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.AddAttribute(7, clientrender::VertexBufferLayout::ComponentCount::VEC4,clientrender::VertexBufferLayout::Type::FLOAT);
	layout.CalculateStride();

	clientrender::ShaderResourceLayout shaderResourceLayout;
	shaderResourceLayout.AddBinding(0, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_VERTEX);
	shaderResourceLayout.AddBinding(1, clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_VERTEX);
	shaderResourceLayout.AddBinding(4, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_VERTEX);
	shaderResourceLayout.AddBinding(2, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_VERTEX);
	shaderResourceLayout.AddBinding(5, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_VERTEX);

	shaderResourceLayout.AddBinding(2, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(10, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(11, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(12, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(13, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(5, clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(14, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(15, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);
	shaderResourceLayout.AddBinding(16, clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT);

	clientrender::ShaderResource pbrShaderResource(shaderResourceLayout);
	pbrShaderResource.AddBuffer(clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,0,"u_CameraData", {});
	pbrShaderResource.AddBuffer(clientrender::ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER,1,"TagDataID", {});
	pbrShaderResource.AddBuffer(clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,4,"u_BoneData", {});
	pbrShaderResource.AddBuffer(clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,2,"u_MaterialData", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,10,"u_DiffuseTexture", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,11,"u_NormalTexture", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,12,"u_CombinedTexture", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,13,"u_EmissiveTexture", {});
	pbrShaderResource.AddBuffer(clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER,5,"u_PerMeshInstanceData", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,16,"u_LightmapTexture", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,14,"u_SpecularCubemap", {});
	pbrShaderResource.AddImage(	clientrender::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER,15,"u_DiffuseCubemap", {});

	passNames.clear();
	passNames.push_back("OpaqueAlbedo");
	passNames.push_back("OpaqueNormal");
	passNames.push_back("OpaquePBRAmbient");
	passNames.push_back("OpaquePBRDebug");
	debugPassNames.clear();
	debugPassNames.push_back("");
	debugPassNames.push_back("OpaqueAlbedo");
	debugPassNames.push_back("OpaqueNormal");
	debugPassNames.push_back("OpaquePBRAmbient");
	debugPassNames.push_back("OpaquePBRDebug");

	clientrender::ShaderSystem::PipelineCreateInfo pipelineCreateInfo;

	pipelineCreateInfo.m_Count = 2;
	pipelineCreateInfo.m_PipelineType = clientrender::ShaderSystem::PipelineType::PIPELINE_TYPE_GRAPHICS;
	pipelineCreateInfo.m_ShaderCreateInfo[0].stage = clientrender::Shader::Stage::SHADER_STAGE_VERTEX;
	pipelineCreateInfo.m_ShaderCreateInfo[0].filepath = "shaders/OpaquePBR.vert";
	pipelineCreateInfo.m_ShaderCreateInfo[0].sourceCode = clientAppInterface->LoadTextFile(
			"shaders/OpaquePBR.vert");
	pipelineCreateInfo.m_ShaderCreateInfo[1].stage = clientrender::Shader::Stage::SHADER_STAGE_FRAGMENT;
	pipelineCreateInfo.m_ShaderCreateInfo[1].filepath = "shaders/OpaquePBR.frag";
	pipelineCreateInfo.m_ShaderCreateInfo[1].sourceCode = clientAppInterface->LoadTextFile(
				"shaders/OpaquePBR.frag");

	std::string &src = pipelineCreateInfo.m_ShaderCreateInfo[1].sourceCode;
	// Now we will GENERATE the variants of the fragment shader, while adding them to the passNames list:
	for(int lightmap=0;lightmap<2;lightmap++)
	{
		for(int emissive=0;emissive<2;emissive++)
		{
			for(int combined=0;combined<2;combined++)
			{
				for(int normal=0;normal<2;normal++)
				{
					for(int diffuse=0;diffuse<2;diffuse++)
					{
						for(int lightCount=0;lightCount<2;lightCount++)
						{
							for(int highlight=0;highlight<2;highlight++)
							{
								std::string passname= GlobalGraphicsResources::GenerateShaderPassName(
										lightmap!=0,diffuse!=0, normal!=0, combined!=0, emissive!=0, lightCount, highlight!=0);
								passNames.push_back(passname.c_str());

								static char txt2[2000];
								char const  *true_or_false[] = {"false", "true"};
								sprintf(txt2, "\nvoid %s()\n{\nPBR(%s,%s,%s,%s,%s,%s, %d, %s);\n}",
										passname.c_str(), true_or_false[lightmap], true_or_false[diffuse], true_or_false[normal],
										true_or_false[combined], true_or_false[emissive],true_or_false[!lightmap]
										,lightCount, true_or_false[highlight]);
								src += txt2;
							}
						}
					}
				}
			}
		}
	}

	//Static passes.
	pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "Static";
	for(const std::string& passName : passNames)
	{
		std::string completeName = "Static_" + passName;
		pipelineCreateInfo.m_ShaderCreateInfo[1].entryPoint = passName.c_str();
		clientAppInterface->BuildEffectPass(completeName.c_str(), &layout, &pipelineCreateInfo,{pbrShaderResource});
	}
	//Skinned passes.
	pipelineCreateInfo.m_ShaderCreateInfo[0].entryPoint = "Animated";
	for(const std::string& passName : passNames)
	{
		std::string effectName = "Animated_" + passName;
		pipelineCreateInfo.m_ShaderCreateInfo[1].entryPoint = passName.c_str();
		clientAppInterface->BuildEffectPass(effectName.c_str(), &layout, &pipelineCreateInfo,{pbrShaderResource});
	}
}

void ClientRenderer::WebcamResources::Init(ClientAppInterface* clientAppInterface,const char *shader_name)
{
	if (initialized)
	{
		return;
	}
	static ovrProgramParm wcUniformParms[] =
			{
					{"videoTexture", ovrProgramParmType::TEXTURE_SAMPLED},
					{"webcamUB",       ovrProgramParmType::BUFFER_UNIFORM}
			};
	std::string vert_file="shaders/";
	std::string frag_file="shaders/";
	vert_file+=shader_name;
	frag_file+=shader_name;
	vert_file+=".vert";
	frag_file+=".frag";
	std::string videoSurfaceVert = clientAppInterface->LoadTextFile(vert_file.c_str());
	std::string videoSurfaceFrag = clientAppInterface->LoadTextFile(frag_file.c_str());
	program = GlProgram::Build(
			nullptr, videoSurfaceVert.c_str(),
			"#extension GL_OES_EGL_image_external_essl3 : require\n",
			videoSurfaceFrag.c_str(),
			wcUniformParms, sizeof(wcUniformParms) / sizeof(ovrProgramParm), 310);
	if (!program.IsValid()) {
		OVR_FAIL("Failed to build video surface shader program for rendering webcam texture");
	}

	surfaceDef.surfaceName = "WebcamSurface";
	surfaceDef.graphicsCommand.GpuState.depthEnable = false;
	surfaceDef.graphicsCommand.GpuState.depthMaskEnable = false;
	surfaceDef.graphicsCommand.GpuState.cullEnable = false;
	surfaceDef.graphicsCommand.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_DISABLE;
	surfaceDef.graphicsCommand.GpuState.frontFace = GL_CW;
	surfaceDef.graphicsCommand.GpuState.polygonMode = GL_FILL;
	surfaceDef.graphicsCommand.Program = program;

	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();

	// Create vertex layout and associated buffers
	std::shared_ptr<clientrender::VertexBufferLayout> layout(new clientrender::VertexBufferLayout);
	layout->AddAttribute((uint32_t)avs::AttributeSemantic::POSITION, clientrender::VertexBufferLayout::ComponentCount::VEC3, clientrender::VertexBufferLayout::Type::FLOAT);
	layout->CalculateStride();
	layout->m_PackingStyle = clientrender::VertexBufferLayout::PackingStyle::INTERLEAVED;

	static constexpr size_t camVertexCount = 4;
	static constexpr size_t camIndexCount = 6;
	static constexpr avs::vec3 vertices[camVertexCount] = {{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}};
	static constexpr uint16_t indices[camIndexCount] = {0, 1, 3, 1, 2, 3}; // Clockwise

	size_t constructedVBSize = layout->m_Stride * camVertexCount;

	vertexBuffer = globalGraphicsResources.renderPlatform.InstantiateVertexBuffer();
	clientrender::VertexBuffer::VertexBufferCreateInfo vb_ci;
	vb_ci.layout = layout;
	vb_ci.usage = clientrender::BufferUsageBit::STATIC_BIT | clientrender::BufferUsageBit::DRAW_BIT;
	vb_ci.vertexCount = camVertexCount;
	vb_ci.size = constructedVBSize;
	vb_ci.data = (const void*)vertices;
	vertexBuffer->Create(&vb_ci);

	indexBuffer = globalGraphicsResources.renderPlatform.InstantiateIndexBuffer();
	clientrender::IndexBuffer::IndexBufferCreateInfo ib_ci;
	ib_ci.usage = clientrender::BufferUsageBit::STATIC_BIT | clientrender::BufferUsageBit::DRAW_BIT;
	ib_ci.indexCount = camIndexCount;
	ib_ci.stride = sizeof(uint16_t);
	ib_ci.data = (const uint8_t*)indices;
	indexBuffer->Create(&ib_ci);

	std::shared_ptr<scc::GL_VertexBuffer> gl_vb = std::dynamic_pointer_cast<scc::GL_VertexBuffer>(vertexBuffer);
	std::shared_ptr<scc::GL_IndexBuffer> gl_ib = std::dynamic_pointer_cast<scc::GL_IndexBuffer>(indexBuffer);

	gl_vb->CreateVAO(gl_ib->GetIndexID());

	// Create the GlGeometry for OVR and reference the GL buffers
	GlGeometry& geo = surfaceDef.geo;
	geo.vertexBuffer = gl_vb->GetVertexID();
	geo.indexBuffer = gl_ib->GetIndexID();
	geo.vertexArrayObject = gl_vb->GetVertexArrayID();
	geo.primitiveType = GL_TRIANGLES;
	geo.vertexCount = (int)gl_vb->GetVertexCount();
	geo.indexCount = (int)gl_ib->GetIndexBufferCreateInfo().indexCount;

	// Set up the uniform buffer
	webcamUB = globalGraphicsResources.renderPlatform.InstantiateUniformBuffer();
	clientrender::UniformBuffer::UniformBufferCreateInfo uniformBufferCreateInfo = {"WebcamUB",1
			, sizeof(WebcamUB)
			, &webcamUBData};
	webcamUB->Create(&uniformBufferCreateInfo);

	// Bottom right corner
	const avs::vec2 offset = { 0.5f, 0.0f};
	const avs::vec2 pos =  { 1.0f - offset.x - (WEBCAM_WIDTH * 0.5f), 0 }; //-1.0f + offset.y + (WEBCAM_HEIGHT * 0.5f) };
	SetPosition(pos);

	initialized = true;
}

void ClientRenderer::WebcamResources::SetPosition(const avs::vec2& position)
{
	ovrMatrix4f translation = ovrMatrix4f_CreateTranslation(position.x, position.y, 0);

	// Width and height of original quad is 2 so scale will be half the width/height
	const avs::vec2 s = { WEBCAM_WIDTH * 0.5f, WEBCAM_HEIGHT * 0.5f };
	ovrMatrix4f scale = ovrMatrix4f_CreateScale(s.x, s.y, 1);

	auto t = ovrMatrix4f_Multiply(&translation, &scale);
	transform = t;
}

void ClientRenderer::WebcamResources::Destroy()
{
	if (initialized)
	{
		surfaceDef.geo.Free();
		GlProgram::Free(program);
		vertexBuffer->Destroy();
		indexBuffer->Destroy();
		webcamUB->Destroy();
		initialized = false;
	}
}

void ClientRenderer::SetWebcamPosition(const avs::vec2& position)
{
	mWebcamResources.SetPosition(position);
}

void ClientRenderer::RenderWebcam(OVRFW::ovrRendererOutput& res)
{
	// Set data to send to the shader:
	if (mShowWebcam && clientPipeline.videoConfig.stream_webcam && mVideoTexture->IsValid() && mWebcamResources.initialized)
	{
		mWebcamResources.surfaceDef.graphicsCommand.UniformData[0].Data = &(((scc::GL_Texture *) mVideoTexture.get())->GetGlTexture());
		mWebcamResources.surfaceDef.graphicsCommand.UniformData[1].Data = &(((scc::GL_UniformBuffer *) mWebcamResources.webcamUB.get())->GetGlBuffer());
		res.Surfaces.emplace_back(mWebcamResources.transform, &mWebcamResources.surfaceDef);
		mWebcamResources.webcamUB->Submit();
	}
}

void ClientRenderer::ExitedVR()
{
	delete mVideoSurfaceTexture;
	mVideoSurfaceTexture= nullptr;
    delete mAlphaSurfaceTexture;
	mAlphaSurfaceTexture= nullptr;
	mVideoSurfaceDef.geo.Free();
	GlProgram::Free(mCubeVideoSurfaceProgram);
	GlProgram::Free(m2DVideoSurfaceProgram);
	mWebcamResources.Destroy();
	mDebugTextureResources.Destroy();
	sessionClient.Disconnect(TELEPORT_TIMEOUT);
}

void ClientRenderer::ConfigureVideo(const avs::VideoConfig &vc)
{
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();
	clientPipeline.videoConfig = vc;
	//Build Video Cubemap or perspective texture
	if (vc.use_cubemap)
	{
		clientrender::Texture::TextureCreateInfo textureCreateInfo =
				{
						"Cubemap Texture", clientPipeline.videoConfig.colour_cubemap_size
						, clientPipeline.videoConfig.colour_cubemap_size, 1, 4, 1, 1, clientrender::Texture::Slot::UNKNOWN
						, clientrender::Texture::Type::TEXTURE_CUBE_MAP, clientrender::Texture::Format::RGBA8
						, clientrender::Texture::SampleCountBit::SAMPLE_COUNT_1_BIT, {}, {}
						, clientrender::Texture::CompressionFormat::UNCOMPRESSED
				};
		mRenderTexture->Create(textureCreateInfo);
		mRenderTexture->UseSampler(globalGraphicsResources.cubeMipMapSampler);

		mVideoSurfaceDef.graphicsCommand.Program = mCubeVideoSurfaceProgram;
	}
	else
	{
		clientrender::Texture::TextureCreateInfo textureCreateInfo =
				{
						"Perspective Texture", clientPipeline.videoConfig.perspective_width
						, clientPipeline.videoConfig.perspective_height, 1, 4, 1, 1, clientrender::Texture::Slot::UNKNOWN
						, clientrender::Texture::Type::TEXTURE_2D, clientrender::Texture::Format::RGBA8
						, clientrender::Texture::SampleCountBit::SAMPLE_COUNT_1_BIT, {}, {}
						, clientrender::Texture::CompressionFormat::UNCOMPRESSED
				};
		mRenderTexture->Create(textureCreateInfo);
		mRenderTexture->UseSampler(globalGraphicsResources.sampler);

		mVideoSurfaceDef.graphicsCommand.Program = m2DVideoSurfaceProgram;
	}

	const float aspect = vc.perspective_width / vc.perspective_height;
	const float vertFOV = clientrender::GetVerticalFOVFromHorizontalInDegrees(vc.perspective_fov, aspect);
	// Takes FOV values in degrees
	ovrMatrix4f serverProj = ovrMatrix4f_CreateProjectionFov( vc.perspective_fov, vertFOV, 0.0f, 0.0f, 0.1f, 0.0f );
	videoUB.serverProj = ovrMatrix4f_Transpose(&serverProj);

	if (vc.stream_webcam)
	{
		// Set webcam uniform buffer data
		const auto ci =  mVideoTexture->GetTextureCreateInfo();
		mWebcamResources.webcamUBData.sourceTexSize = { ci.width, ci.height };
		mWebcamResources.webcamUBData.sourceOffset = { vc.webcam_offset_x, vc.webcam_offset_y };
		mWebcamResources.webcamUBData.camTexSize = { vc.webcam_width, vc.webcam_height };
	}


	//GLCheckErrorsWithTitle("Built Video Cubemap");
	//Build Lighting Cubemap
	{
		clientrender::Texture::TextureCreateInfo textureCreateInfo //TODO: Check this against the incoming texture from the video stream
				{
						"Cubemap Sub-Textures", 128, 128, 1, 4, 1, 3, clientrender::Texture::Slot::UNKNOWN
						, clientrender::Texture::Type::TEXTURE_CUBE_MAP, clientrender::Texture::Format::RGBA8
						, clientrender::Texture::SampleCountBit::SAMPLE_COUNT_1_BIT, {}, {}
						, clientrender::Texture::CompressionFormat::UNCOMPRESSED
				};
		textureCreateInfo.mipCount = std::min(6,clientPipeline.videoConfig.specular_mips);
		textureCreateInfo.width = clientPipeline.videoConfig.specular_cubemap_size;
		textureCreateInfo.height = clientPipeline.videoConfig.specular_cubemap_size;
		specularCubemapTexture->Create(textureCreateInfo);
		textureCreateInfo.mipCount = 1;
		textureCreateInfo.width = clientPipeline.videoConfig.diffuse_cubemap_size;
		textureCreateInfo.height = clientPipeline.videoConfig.diffuse_cubemap_size;
		diffuseCubemapTexture->Create(textureCreateInfo);
		textureCreateInfo.width = clientPipeline.videoConfig.light_cubemap_size;
		textureCreateInfo.height = clientPipeline.videoConfig.light_cubemap_size;
		mCubemapLightingTexture->Create(textureCreateInfo);
		diffuseCubemapTexture->UseSampler(globalGraphicsResources.cubeMipMapSampler);
		specularCubemapTexture->UseSampler(globalGraphicsResources.cubeMipMapSampler);
		mCubemapLightingTexture->UseSampler(globalGraphicsResources.cubeMipMapSampler);
	}
}

void ClientRenderer::OnReceiveVideoTagData(const uint8_t *data, size_t dataSize)
{
	clientrender::SceneCaptureCubeTagData tagData;
	memcpy(&tagData.coreData, data, sizeof(clientrender::SceneCaptureCubeCoreTagData));
	avs::ConvertTransform(lastSetupCommand.axesStandard, avs::AxesStandard::GlStyle,
						  tagData.coreData.cameraTransform);

	tagData.lights.resize(std::min(tagData.coreData.lightCount, (uint32_t) 4));

	teleport::client::ServerTimestamp::setLastReceivedTimestampUTCUnixMs(tagData.coreData.timestamp_unix_ms);

	// Aidan : View and proj matrices are currently unchanged from Unity
	size_t index = sizeof(clientrender::SceneCaptureCubeCoreTagData);
	for (auto &light : tagData.lights)
	{
		memcpy(&light, &data[index], sizeof(clientrender::LightTagData));
		avs::ConvertTransform(lastSetupCommand.axesStandard, avs::AxesStandard::GlStyle,
							  light.worldTransform);
		index += sizeof(clientrender::LightTagData);
	}

	VideoTagDataCube shaderData;
	shaderData.cameraPosition = tagData.coreData.cameraTransform.position;
	shaderData.cameraRotation = tagData.coreData.cameraTransform.rotation;
	shaderData.ambientMultipliers.x=tagData.coreData.diffuseAmbientScale;
	shaderData.lightCount = tagData.lights.size();

	uint32_t offset = sizeof(VideoTagDataCube) * tagData.coreData.id;
	mTagDataArrayBuffer->Update(sizeof(VideoTagDataCube), (void *) &shaderData, offset);

	videoTagDataCubeArray[tagData.coreData.id] = std::move(tagData);
}

void ClientRenderer::CopyToCubemaps(scc::GL_DeviceContext &mDeviceContext)
{
	clientrender::ivec2 specularOffset = {clientPipeline.videoConfig.specular_x, clientPipeline.videoConfig.specular_y};
	clientrender::ivec2 diffuseOffset = {clientPipeline.videoConfig.diffuse_x, clientPipeline.videoConfig.diffuse_y};
	//clientrender::ivec2  lightOffset={2 * specularSize+3 * specularSize / 2, specularSize * 2};
	// Here the compute shader to copy from the video texture into the cubemap/s.
	auto &tc = mRenderTexture->GetTextureCreateInfo();
	if (mRenderTexture->IsValid())
	{
		const uint32_t ThreadCount = 4;
		GLint max_u, max_v, max_w;
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &max_u);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &max_v);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &max_w);

		clientrender::uvec3 size = {tc.width / ThreadCount, tc.height / ThreadCount, 6};

		size.x = std::min(size.x, (uint32_t) max_u);
		size.y = std::min(size.y, (uint32_t) max_v);
		size.z = std::min(size.z, (uint32_t) max_w);

		cubemapUB.faceSize = tc.width;
		cubemapUB.sourceOffset = {0, 0};
		cubemapUB.mip = 0;
		cubemapUB.face = 0;

		if (tc.type == clientrender::Texture::Type::TEXTURE_CUBE_MAP)
		{
			cubemapUB.dimensions = {cubemapUB.faceSize * 3, cubemapUB.faceSize * 2};

			clientrender::InputCommandCreateInfo inputCommandCreateInfo;

			if (clientPipeline.videoConfig.use_alpha_layer_decoding)
            {
                inputCommandCreateInfo.effectPassName = "ColourAndAlphaLayer";

                clientrender::InputCommand_Compute inputCommand(&inputCommandCreateInfo, size,
                                                       mCopyCubemapWithAlphaLayerEffect,
                                                       {mColourAndDepthShaderResources});

                mDeviceContext.DispatchCompute(&inputCommand);
            }
			else
            {
                inputCommandCreateInfo.effectPassName = "ColourAndDepth";

                clientrender::InputCommand_Compute inputCommand(&inputCommandCreateInfo, size,
                                                       mCopyCubemapWithDepthEffect,
                                                       {mColourAndDepthShaderResources});

                mDeviceContext.DispatchCompute(&inputCommand);
            }
		}
		else
		{
			clientrender::uvec3 perspSize = { size.x, size.y, 1};
			const auto texInfo = mRenderTexture->GetTextureCreateInfo();
			cubemapUB.dimensions = { texInfo.width, texInfo.height };
            clientrender::InputCommandCreateInfo inputCommandCreateInfo;

            if (clientPipeline.videoConfig.use_alpha_layer_decoding)
            {
                inputCommandCreateInfo.effectPassName = "PerspectiveColour";

                clientrender::InputCommand_Compute inputCommand(&inputCommandCreateInfo, perspSize,
                                                       mCopyPerspectiveEffect,
                                                       {mCopyPerspectiveShaderResources});

                mDeviceContext.DispatchCompute(&inputCommand);
            }
            else
            {
                inputCommandCreateInfo.effectPassName = "PerspectiveColourAndDepth";

                clientrender::InputCommand_Compute inputCommand(&inputCommandCreateInfo, perspSize,
                                                       mCopyPerspectiveWithDepthEffect,
                                                       {mCopyPerspectiveShaderResources});

                mDeviceContext.DispatchCompute(&inputCommand);
            }
		}

		GLCheckErrorsWithTitle("Frame: CopyToCubemaps - Main");

		clientrender::InputCommandCreateInfo inputCommandCreateInfo;
		inputCommandCreateInfo.effectPassName = "CopyCubemap";
		clientrender::InputCommand_Compute inputCommand(&inputCommandCreateInfo, size,
											   mCopyCubemapEffect,
											   {mCopyCubemapShaderResources});
		cubemapUB.faceSize = 0;
		clientrender::ivec2 offset0 = {0, 0};
		int32_t mip_x = 0;

		if (diffuseCubemapTexture->IsValid())
		{
			static uint32_t face = 0;
			mip_x = 0;
			int32_t mip_size = clientPipeline.videoConfig.diffuse_cubemap_size;
			uint32_t M = diffuseCubemapTexture->GetTextureCreateInfo().mipCount;
			clientrender::ivec2 offset = {offset0.x + diffuseOffset.x, offset0.y + diffuseOffset.y};
			for (uint32_t m = 0; m < M; m++)
			{
				inputCommand.m_WorkGroupSize = {(mip_size + ThreadCount- 1) / ThreadCount, (mip_size + ThreadCount- 1)
																						   / ThreadCount, 6};
				mCopyCubemapShaderResources.SetImageInfo(0, {diffuseCubemapTexture->GetSampler()
						, diffuseCubemapTexture, m});
				cubemapUB.sourceOffset = {offset.x+ mip_x, offset.y };
				cubemapUB.faceSize = uint32_t(mip_size);
				cubemapUB.mip = m;
				cubemapUB.face = 0;
				inputCommand.m_ShaderResources = {&mCopyCubemapShaderResources};
				mDeviceContext.DispatchCompute(&inputCommand);
				//OVR_LOG("Dispatch offset=%d %d wgSize=%d %d %d mipSize=%d",cubemapUB.sourceOffset.x,cubemapUB.sourceOffset.y,inputCommand.m_WorkGroupSize.x,inputCommand.m_WorkGroupSize.y,inputCommand.m_WorkGroupSize.z,cubemapUB.faceSize);

				mip_x += 3 * mip_size;
				mip_size /= 2;
			}
			face++;
			face = face % 6;
		}
		if (specularCubemapTexture->IsValid())
		{
			mip_x = 0;
			int32_t mip_size = clientPipeline.videoConfig.specular_cubemap_size;
			uint32_t M = specularCubemapTexture->GetTextureCreateInfo().mipCount;
			clientrender::ivec2 offset = {
					offset0.x + specularOffset.x, offset0.y + specularOffset.y};
			for (uint32_t m = 0; m < M; m++)
			{
				uint32_t s=std::max(uint32_t(1),(mip_size+ThreadCount- 1) / ThreadCount);
				inputCommand.m_WorkGroupSize = {s,s, 6};
				mCopyCubemapShaderResources.SetImageInfo(
						0, {specularCubemapTexture->GetSampler(), specularCubemapTexture, m});
				cubemapUB.sourceOffset = {offset.x+ mip_x, offset.y };
				cubemapUB.faceSize = uint32_t(mip_size);
				cubemapUB.mip = m;
				cubemapUB.face = 0;
				inputCommand.m_ShaderResources = {&mCopyCubemapShaderResources};
				mDeviceContext.DispatchCompute(&inputCommand);
				mip_x += 3 * mip_size;
				mip_size /= 2;
			}
		}
		GLCheckErrorsWithTitle("Frame: CopyToCubemaps - Lighting");
		if (mVideoTexture->IsValid())
		{
			inputCommandCreateInfo.effectPassName = "ExtractTagDataID";
			clientrender::uvec3 size = {1, 1, 1};
			clientrender::InputCommand_Compute inputCommand(&inputCommandCreateInfo, size,
												   mExtractTagDataIDEffect,
												   {mExtractTagShaderResources});
			cubemapUB.faceSize = tc.width;
			cubemapUB.sourceOffset = {
					(int32_t) mVideoTexture->GetTextureCreateInfo().width - (32 * 4),
					(int32_t) mVideoTexture->GetTextureCreateInfo().height - 4};
			mDeviceContext.DispatchCompute(&inputCommand);

			inputCommandCreateInfo.effectPassName = "ExtractOneTag";
			clientrender::InputCommand_Compute extractTagCommand(&inputCommandCreateInfo, size,
														mExtractOneTagEffect,
														{mExtractTagShaderResources});
			mDeviceContext.DispatchCompute(&extractTagCommand);
		}
	}
	UpdateTagDataBuffers();
}

void ClientRenderer::UpdateTagDataBuffers()
{
	// TODO: too slow.
	std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
	auto &cachedLights = geometryCache.mLightManager.GetCache(cacheLock);
	VideoTagDataCube *data = static_cast<VideoTagDataCube *>(mTagDataArrayBuffer->Map());
	if (data)
	{
		//VideoTagDataCube &data=*pdata;
		for (size_t i = 0; i < videoTagDataCubeArray.size(); ++i)
		{
			const auto &td = videoTagDataCubeArray[i];
			const auto &pos = td.coreData.cameraTransform.position;
			const auto &rot = td.coreData.cameraTransform.rotation;
			data[i].cameraPosition = {pos.x, pos.y, pos.z};
			data[i].cameraRotation = {rot.x, rot.y, rot.z, rot.w};
			data[i].ambientMultipliers={td.coreData.diffuseAmbientScale,0,0,0};
			data[i].lightCount = td.lights.size();
			for (size_t j = 0; j < td.lights.size(); j++)
			{
				LightTag &t = data[i].lightTags[j];
				const clientrender::LightTagData &l = td.lights[j];
				t.uid32 = (unsigned) (((uint64_t) 0xFFFFFFFF) & l.uid);
				t.colour = *((avs::vec4 *) &l.color);
				// Convert from +-1 to [0,1]
				t.shadowTexCoordOffset.x = float(l.texturePosition[0])
										   / float(lastSetupCommand.video_config.video_width);
				t.shadowTexCoordOffset.y = float(l.texturePosition[1])
										   / float(lastSetupCommand.video_config.video_height);
				t.shadowTexCoordScale.x =
						float(l.textureSize) / float(lastSetupCommand.video_config.video_width);
				t.shadowTexCoordScale.y = float(l.textureSize)
										  / float(lastSetupCommand.video_config.video_height);
				t.position = *((avs::vec3 *) &l.position);
				ovrQuatf q = {l.orientation.x, l.orientation.y, l.orientation.z, l.orientation.w};
				// Note: we transform from a local y pointing light into the global direction.
				// this is equivalent e.g. of +z in Unity, but what about Unreal etc? Need common way
				// to express this.
				static avs::vec3 z = {0, 1.0f, 0.0f};
				t.direction = QuaternionTimesVector(q, z);
				clientrender::mat4 worldToShadowMatrix = clientrender::mat4((const float *) &l.worldToShadowMatrix);

				t.worldToShadowMatrix = *((ovrMatrix4f *) &worldToShadowMatrix);

				const auto &nodeLight = cachedLights.find(l.uid);
				if (nodeLight != cachedLights.end() && nodeLight->second.resource!=nullptr)
				{
					auto &lc		=nodeLight->second.resource->GetLightCreateInfo();
					t.is_point		=float(lc.type!=clientrender::Light::Type::DIRECTIONAL);
					t.is_spot		=float(lc.type==clientrender::Light::Type::SPOT);
					t.radius		=lc.lightRadius;
					t.range			=lc.lightRange;
					t.shadow_strength=0.0f;
				}
			}
		}
		mTagDataArrayBuffer->Unmap();
	}
}

void ClientRenderer::RenderVideo(scc::GL_DeviceContext &mDeviceContext, OVRFW::ovrRendererOutput &res)
{
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();
	{
		ovrMatrix4f eye0 = res.FrameMatrices.EyeView[0];
		eye0.M[0][3] = 0.0f;
		eye0.M[1][3] = 0.0f;
		eye0.M[2][3] = 0.0f;
		ovrMatrix4f viewProj0 = res.FrameMatrices.EyeProjection[0] * eye0;
		ovrMatrix4f viewProjT0 = ovrMatrix4f_Transpose(&viewProj0);
		videoUB.invViewProj[0] = ovrMatrix4f_Inverse(&viewProjT0);
		ovrMatrix4f eye1 = res.FrameMatrices.EyeView[1];
		eye1.M[0][3] = 0.0f;
		eye1.M[1][3] = 0.0f;
		eye1.M[2][3] = 0.0f;
		ovrMatrix4f viewProj1 = res.FrameMatrices.EyeProjection[1] * eye1;
		ovrMatrix4f viewProjT1 = ovrMatrix4f_Transpose(&viewProj1);
		videoUB.invViewProj[1] = ovrMatrix4f_Inverse(&viewProjT1);
	}
	// Set data to send to the shader:
	ovrQuatf X0 = {1.0f, 0.f, 0.f, 0.0f};
	ovrQuatf headPoseQ = {clientDeviceState->headPose.globalPose.orientation.x
			, clientDeviceState->headPose.globalPose.orientation.y
			, clientDeviceState->headPose.globalPose.orientation.z
			, clientDeviceState->headPose.globalPose.orientation.w};
	ovrQuatf headPoseC = {-clientDeviceState->headPose.globalPose.orientation.x
			, -clientDeviceState->headPose.globalPose.orientation.y
			, -clientDeviceState->headPose.globalPose.orientation.z
			, clientDeviceState->headPose.globalPose.orientation.w};
	ovrQuatf xDir = QuaternionMultiply(QuaternionMultiply(headPoseQ, X0), headPoseC);
	float w = eyeSeparation / 2.0f;//.04f; //half separation.
	avs::vec4 eye = {w * xDir.x, w * xDir.y, w * xDir.z, 0.0f};
	avs::vec4 left_eye = {-eye.x, -eye.y, -eye.z, 0.0f};
	videoUB.eyeOffsets[0] = left_eye;        // left eye
	videoUB.eyeOffsets[1] = eye;    // right eye.
	videoUB.cameraPosition = clientDeviceState->headPose.globalPose.position;
	videoUB.cameraRotation = clientDeviceState->headPose.globalPose.orientation;
	videoUB.viewProj=res.FrameMatrices.EyeProjection[0]*res.FrameMatrices.CenterView;
	if (mRenderTexture->IsValid())
	{
		mVideoSurfaceDef.graphicsCommand.UniformData[0].Data = &(((scc::GL_Texture *) mRenderTexture.get())->GetGlTexture());
	}
	mVideoSurfaceDef.graphicsCommand.UniformData[1].Data = &(((scc::GL_UniformBuffer *) mVideoUB.get())->GetGlBuffer());
	OVRFW::GlBuffer &buf = ((scc::GL_ShaderStorageBuffer *) globalGraphicsResources.mTagDataBuffer.get())->GetGlBuffer();
	mVideoSurfaceDef.graphicsCommand.UniformData[2].Data = &buf;
	res.Surfaces.push_back(ovrDrawSurface(&mVideoSurfaceDef));
	mVideoUB->Submit();
}

void ClientRenderer::RenderLocalNodes(OVRFW::ovrRendererOutput &res)
{
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();

	//Render local nodes.
	const clientrender::NodeManager::nodeList_t &distanceSortedRootNodes = geometryCache.mNodeManager->GetSortedRootNodes();
	for (std::shared_ptr<clientrender::Node> node : distanceSortedRootNodes)
	{
		RenderNode(res, node);
		node->distance=length(globalGraphicsResources.scrCamera->GetPosition()-node->GetGlobalTransform().m_Translation);
	}

	//Render player, if parts exist.
#ifdef RENDER_BODY_SEPARATELY
	std::shared_ptr<clientrender::Node> body = geometryCache.mNodeManager->GetBody();
	if (body)
	{
		RenderNode(res, body);
	}
	std::shared_ptr<clientrender::Node> leftHand = geometryCache.mNodeManager->GetLeftHand();
	if (leftHand)
	{
		RenderNode(res, leftHand);
	}
	std::shared_ptr<clientrender::Node> rightHand = geometryCache.mNodeManager->GetRightHand();
	if (rightHand)
	{
		RenderNode(res, rightHand);
	}
#endif
}

void ClientRenderer::RenderNode(OVRFW::ovrRendererOutput &res, std::shared_ptr<clientrender::Node> node)
{
	if(node->IsVisible()&&node->GetPriority()>=minimumPriority)
	{
		std::shared_ptr<OVRNode> ovrNode = std::static_pointer_cast<OVRNode>(node);
		if(ovrNode->GetSurfaces().size() != 0)
		{
			// How can this happen?
			if(node->GetMesh().get()== nullptr)
			{
				return;
			}
			GlobalGraphicsResources &globalGraphicsResources = GlobalGraphicsResources::GetInstance();
			// Get lightmap texture.
			std::shared_ptr<clientrender::Texture>		lightmapTexture;
			if(node->GetGlobalIlluminationTextureUid()!=0)
			{
				lightmapTexture = geometryCache.mTextureManager.Get(node->GetGlobalIlluminationTextureUid());
				if(!lightmapTexture.get())
				{
					lightmapTexture = resourceCreator.m_DummyWhite;
				}
				else
				{
					lightmapTexture->UseSampler(globalGraphicsResources.noMipsampler);
				}
			}

			//Get final transform.
			clientrender::mat4 globalMatrix = node->GetGlobalTransform().GetTransformMatrix();
			clientDeviceState->transformToLocalOrigin = clientrender::mat4::Identity();//Translation(-clientDeviceState->localFootPos);
			clientrender::mat4 scr_Transform = clientDeviceState->transformToLocalOrigin * globalMatrix;

			//Convert transform to OVR type.
			OVR::Matrix4f transform;
			memcpy(&transform.M[0][0], &scr_Transform.a, 16 * sizeof(float));

			//Update skin uniform buffer to animate skinned meshes.
			std::shared_ptr<clientrender::Skin> skin = ovrNode->GetSkin();
			if(skin)
			{
				skin->UpdateBoneMatrices(globalMatrix);
			}

			std::vector<const clientrender::ShaderResource *> pbrShaderResources;
			const clientrender::ShaderResource& r=globalGraphicsResources.scrCamera->GetShaderResource();
			const clientrender::ShaderResource *sr=&r;
			// TODO: Why does THIS crash?
			//pbrShaderResources.push_back(&r);
			//pbrShaderResources.push_back(&globalGraphicsResources.GetPerMeshInstanceShaderResource(perMeshInstanceData));
			//Push surfaces onto render queue.
			for(ovrSurfaceDef& surfaceDef : ovrNode->GetSurfaces())
			{
				int j = 0;
				// Must update the uniforms. e.g. camera pos.
				// The below seems to only apply/work for camerapos anyway:
				//for(const clientrender::ShaderResource *sr : pbrShaderResources)
				const std::vector<clientrender::ShaderResource::WriteShaderResource> &shaderResourceSet = sr->GetWriteShaderResources();
				for(const clientrender::ShaderResource::WriteShaderResource &resource : shaderResourceSet)
				{
					clientrender::ShaderResourceLayout::ShaderResourceType type = resource.shaderResourceType;
					if(type == clientrender::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER && resource.bufferInfo.buffer)
					{
						scc::GL_UniformBuffer *gl_uniformBuffer = static_cast<scc::GL_UniformBuffer *>(resource.bufferInfo.buffer);
						if(gl_uniformBuffer!=nullptr)
						{
							surfaceDef.graphicsCommand.UniformData[j].Data = &(gl_uniformBuffer->GetGlBuffer());
						}
					}
				}
				j++;
				if(skin)
				{
					//OVRFW::GlBuffer &buf = ((scc::GL_ShaderStorageBuffer*)globalGraphicsResources.mTagDataBuffer.get())->GetGlBuffer();
					//surfaceDef.graphicsCommand.UniformData[4].Data = &buf;
				}

				OVRFW::GlBuffer &buf = ((scc::GL_ShaderStorageBuffer *)globalGraphicsResources.mTagDataBuffer.get())->GetGlBuffer();
				surfaceDef.graphicsCommand.UniformData[1].Data = &buf;
				//surfaceDef.graphicsCommand.UniformData[3].Data = &ovrNode->perMeshInstanceData.u_LightmapScaleOffset;
				if(lightmapTexture.get())
				{
					auto gl_texture = dynamic_cast<scc::GL_Texture *>(lightmapTexture.get());
					surfaceDef.graphicsCommand.UniformData[9].Data =&(gl_texture->GetGlTexture());
				}
				res.Surfaces.emplace_back(transform, &surfaceDef);
			}
		}
	}

	//Render children.
	for(std::weak_ptr<clientrender::Node> childPtr : node->GetChildren())
	{
		std::shared_ptr<clientrender::Node> child = childPtr.lock();
		if(child)
		{
			RenderNode(res, child);
		}
	}
}

void ClientRenderer::CycleShaderMode()
{
	OVRNodeManager *nodeManager = dynamic_cast<OVRNodeManager *>(geometryCache.mNodeManager.get());
	passSelector++;
	passSelector = passSelector % (debugPassNames.size());
	nodeManager->ChangeEffectPass(passSelector>0?debugPassNames[passSelector].c_str():"");
}

void ClientRenderer::ListNode(const std::shared_ptr<clientrender::Node>& node, int indent, size_t& linesRemaining)
{
	//Return if we do not want to print any more lines.
	if(linesRemaining <= 0)
	{
		return;
	}
	--linesRemaining;

	//Set indent string to indent amount.
	static char indent_txt[20];
	indent_txt[indent] = 0;
	if(indent > 0)
	{
		indent_txt[indent - 1] = ' ';
	}

	//Retrieve info string on mesh on node, if the node has a mesh.
	std::string meshInfoString;
	const std::shared_ptr<clientrender::Mesh>& mesh = node->GetMesh();
	if(mesh)
	{
		meshInfoString = "mesh ";
		meshInfoString+=mesh->GetMeshCreateInfo().name.c_str();
	}
	avs::vec3 pos=node->GetGlobalPosition();
	//Print details on node to screen.
	OVR_LOG("%s%llu %s (%4.4f,%4.4f,%4.4f) %s", indent_txt, node->id, node->name.c_str()
			,pos.x,pos.y,pos.z
			, meshInfoString.c_str());

	//Print information on children to screen.
	const std::vector<std::weak_ptr<clientrender::Node>>& children = node->GetChildren();
	for(const auto c : children)
	{
		ListNode(c.lock(), indent + 1, linesRemaining);
	}
}

void ClientRenderer::WriteDebugOutput()
{
	std::unique_ptr<std::lock_guard<std::mutex>> cacheLock;
	auto& rootNodes = geometryCache.mNodeManager->GetRootNodes();
	OVR_LOG("Root Nodes: %zu   Total Nodes: %zu",rootNodes.size(),geometryCache.mNodeManager->GetNodeAmount());
	size_t linesRemaining = 20;
	static uid show_only=0;
	for(const std::shared_ptr<clientrender::Node>& node : rootNodes)
	{
		if(show_only!=0&&show_only!=node->id)
			continue;
		ListNode(node, 1, linesRemaining);
		if(linesRemaining <= 0)
		{
			break;
		}
	}
}

void ClientRenderer::ToggleWebcam()
{
	mShowWebcam = !mShowWebcam;
}

void ClientRenderer::CycleOSD()
{
	show_osd = (show_osd + 1) % clientrender::NUM_OSDS;
}
void ClientRenderer::CycleOSDSelection()
{
	osd_selection++;
}

void ClientRenderer::SetStickOffset(float x, float y)
{
	//ovrFrameInput *f = const_cast<ovrFrameInput *>(&vrFrame);
	//f->Input.sticks[0][0] += dx;
	//f->Input.sticks[0][1] += dy;
}

void ClientRenderer::DrawOSD(OVRFW::ovrRendererOutput& res)
{
	static avs::vec3 offset={0,0,4.5f};
	static avs::vec4 colour={1.0f,0.7f,0.5f,0.5f};
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();
	if(passSelector!=0)
	{
		static avs::vec3 passoffset={0,2.5f,5.0f};
		clientAppInterface->PrintText(passoffset,colour,"%s",globalGraphicsResources.effectPassName.c_str());
	}
	auto ctr = clientPipeline.source.getCounterValues();
	auto vidStats = clientPipeline.decoder.GetStats();

	switch(show_osd)
	{
		case clientrender::NO_OSD:
			break;
		case clientrender::CAMERA_OSD:
		{
			avs::vec3 vidPos(0, 0, 0);
			if(videoTagDataCubeArray.size())
			{
				vidPos = videoTagDataCubeArray[0].coreData.cameraTransform.position;
			}
			clientAppInterface->PrintText(
					offset, colour,
					"Devices\n\n"
	 				"Foot pos: %1.2f, %1.2f, %1.2f    yaw: %1.2f\n"
					" Camera Relative: %1.2f, %1.2f, %1.2f Abs: %1.2f, %1.2f, %1.2f\n"
					"  Video Position: %1.2f, %1.2f, %1.2f\n"
					"Controller 0 rel: (%1.2f, %1.2f, %1.2f) {%1.2f, %1.2f, %1.2f}\n"
					"             abs: (%1.2f, %1.2f, %1.2f) {%1.2f, %1.2f, %1.2f}\n"
					"Controller 1 rel: (%1.2f, %1.2f, %1.2f) {%1.2f, %1.2f, %1.2f}\n"
					"             abs: (%1.2f, %1.2f, %1.2f) {%1.2f, %1.2f, %1.2f}\n"
					, clientDeviceState->originPose.position.x,	clientDeviceState->originPose.position.y,clientDeviceState->originPose.position.z,
					clientDeviceState->headPose.localPose.position.x, clientDeviceState->headPose.localPose.position.y,clientDeviceState->headPose.localPose.position.z,
					clientDeviceState->headPose.globalPose.position.x, clientDeviceState->headPose.globalPose.position.y,clientDeviceState->headPose.globalPose.position.z,
					clientDeviceState->stickYaw,
					vidPos.x, vidPos.y, vidPos.z,
					clientDeviceState->controllerPoses[0].localPose.position.x,
					clientDeviceState->controllerPoses[0].localPose.position.y,
					clientDeviceState->controllerPoses[0].localPose.position.z,
					clientDeviceState->controllerPoses[0].localPose.orientation.x,
					clientDeviceState->controllerPoses[0].localPose.orientation.y,
					clientDeviceState->controllerPoses[0].localPose.orientation.z,
					clientDeviceState->controllerPoses[0].globalPose.position.x,
					clientDeviceState->controllerPoses[0].globalPose.position.y,
					clientDeviceState->controllerPoses[0].globalPose.position.z,
					clientDeviceState->controllerPoses[0].globalPose.orientation.x,
					clientDeviceState->controllerPoses[0].globalPose.orientation.y,
					clientDeviceState->controllerPoses[0].globalPose.orientation.z,
					clientDeviceState->controllerPoses[1].localPose.position.x,
					clientDeviceState->controllerPoses[1].localPose.position.y,
					clientDeviceState->controllerPoses[1].localPose.position.z,
					clientDeviceState->controllerPoses[1].localPose.orientation.x,
					clientDeviceState->controllerPoses[1].localPose.orientation.y,
					clientDeviceState->controllerPoses[1].localPose.orientation.z,
					clientDeviceState->controllerPoses[1].globalPose.position.x,
					clientDeviceState->controllerPoses[1].globalPose.position.y,
					clientDeviceState->controllerPoses[1].globalPose.position.z,
					clientDeviceState->controllerPoses[1].globalPose.orientation.x,
					clientDeviceState->controllerPoses[1].globalPose.orientation.y,
					clientDeviceState->controllerPoses[1].globalPose.orientation.z
			);

			break;
		}
		case clientrender::NETWORK_OSD:
		{
			clientAppInterface->PrintText(
					offset, colour,
					"Network\n\n"
					"Frames: %d\nPackets Dropped: Network %d | Decoder %d\n"
					"Incomplete Decoder Packets: %d\n"
					"Bandwidth(kbps): %4.2f\n"
                    "Decoder Packets Per Sec: %1.2f\n"
					"Video Frames Received Per Sec: %1.2f\n"
					"Video frames Processed Per Sec: %1.2f\n"
					"Video Frames Displayed Per Sec: %1.2f\n",
					vidStats.framesProcessed,
					ctr.networkPacketsDropped,
					ctr.decoderPacketsDropped,
					ctr.incompleteDecoderPacketsReceived,
					ctr.bandwidthKPS,
					ctr.decoderPacketsReceivedPerSec,
					vidStats.framesReceivedPerSec,
					vidStats.framesProcessedPerSec,
					vidStats.framesDisplayedPerSec);
			break;
		}
		case clientrender::GEOMETRY_OSD:
		{
			std::ostringstream str;
			const clientrender::NodeManager::nodeList_t &rootNodes = geometryCache.mNodeManager->GetRootNodes();

			str <<"Geometry\n\nNodes: "<<static_cast<uint64_t>(geometryCache.mNodeManager->GetNodeAmount()) << "\n";
			for(std::shared_ptr<clientrender::Node> node : rootNodes)
			{
				str << node->id << ": "<<node->name.c_str()<<"\n";
			}
			str << "\n";

			const auto &missingResources = geometryCache.m_MissingResources;
			if(missingResources.size() > 0)
			{
				str << "Missing Resources\n";

				size_t resourcesOnLine = 0;
				for(const auto &missingPair : missingResources)
				{
					const clientrender::MissingResource &missingResource = missingPair.second;
					str << stringOf(missingResource.resourceType) << " " << missingResource.id;

					resourcesOnLine++;
					if(resourcesOnLine >= MAX_RESOURCES_PER_LINE)
					{
						str << std::endl;
						resourcesOnLine = 0;
					}
					else
					{
						str << " | ";
					}
				}
			}
			if(str.str().length()<1700)
				clientAppInterface->PrintText(
						offset, colour, str.str().c_str()
			);

			break;
		}
		case clientrender::TEXTURES_OSD:
		{
			std::ostringstream sstr;
			std::setprecision(5);
			const std::vector<uid> texture_uids=geometryCache.mTextureManager.GetAllIDs();
			if(osd_selection>=texture_uids.size())
				osd_selection=0;
			sstr << "Textures\n\n" << std::setw(4);
			sstr << "Total: " << texture_uids.size()<<"\n";
			if(osd_selection>=0&&osd_selection<texture_uids.size())
			{
				auto texture = geometryCache.mTextureManager.Get(texture_uids[osd_selection]);
				const auto &ci =  texture->GetTextureCreateInfo();
				sstr << "\tSelected: " << ci.name.c_str()<<"\n";
				sstr << "\t" << ci.width<<" x "<<ci.height <<"\n";
				mDebugTextureResources.surfaceDef.graphicsCommand.UniformData[0].Data = &(((scc::GL_Texture *) texture.get())->GetGlTexture());
				res.Surfaces.emplace_back(mDebugTextureResources.transform, &mDebugTextureResources.surfaceDef);
			}
			clientAppInterface->PrintText(
					offset, colour,
					sstr.str().c_str());
		}
		break;
		case clientrender::TAG_OSD:
		{
			std::ostringstream sstr;
			std::setprecision(5);
			sstr << "Tags\n\n" << std::setw(4);
			static int ii = 0;
			static char iii = 0;
			iii++;
			if(!iii)
			{
				ii++;
				if(ii > 2)
				{
					ii = 0;
				}
			}
			for(size_t i = 0; i < std::min((size_t)8, videoTagDataCubeArray.size()); i++)
			{
				auto &tag = videoTagDataCubeArray[i + 8 * ii];
				sstr << tag.coreData.lightCount << " lights\n";
				for(size_t j = 0; j < tag.lights.size(); j++)
				{
					auto &l = tag.lights[j];
					sstr << "\t" << l.uid << ": Type " << ToString((clientrender::Light::Type)l.lightType)
						 << ", clr " << l.color.x << "," << l.color.y << "," << l.color.z;
					if(l.lightType == clientrender::LightType::Directional)
					{
						ovrQuatf q = {l.orientation.x, l.orientation.y, l.orientation.z, l.orientation.w};
						avs::vec3 z = {0, 0, 1.0f};
						avs::vec3 direction = QuaternionTimesVector(q, z);
						sstr << ", d " << direction.x << "," << direction.y << "," << direction.z;
					}
					sstr << "\n";
				}
			}
			clientAppInterface->PrintText(offset, colour, sstr.str().c_str());

			break;
		}
		case clientrender::CONTROLLER_OSD:
		{
			avs::vec3 leftHandPosition, rightHandPosition;
			avs::vec4 leftHandOrientation, rightHandOrientation;

			std::shared_ptr<clientrender::Node> leftHand = geometryCache.mNodeManager->GetLeftHand();
			if(leftHand)
			{
				leftHandPosition = leftHand->GetGlobalTransform().m_Translation;
				leftHandOrientation = leftHand->GetGlobalTransform().m_Rotation;
			}
			std::shared_ptr<clientrender::Node> rightHand = geometryCache.mNodeManager->GetRightHand();
			if(rightHand)
			{
				rightHandPosition = rightHand->GetGlobalTransform().m_Translation;
				rightHandOrientation = rightHand->GetGlobalTransform().m_Rotation;
			}

			clientAppInterface->PrintText(
							offset, colour,
							"Controllers\n\n"
							"Left Hand:		(%1.2f, %1.2f, %1.2f) {%1.2f, %1.2f, %1.2f}\n"
							"Right Hand:	(%1.2f, %1.2f, %1.2f) {%1.2f, %1.2f, %1.2f}\n"
							,leftHandPosition.x, leftHandPosition.y, leftHandPosition.z,
							leftHandOrientation.x, leftHandOrientation.y, leftHandOrientation.z,
							rightHandPosition.x, rightHandPosition.y, rightHandPosition.z,
							rightHandOrientation.x, rightHandOrientation.y, rightHandOrientation.z
					);

			break;
		}
		default:
			break;
	}
}


void ClientRenderer::UpdateHandObjects(ovrMobile *ovrm)
{
	//Poll controller state from the Oculus API.
	for(int i=0; i < 2; i++)
	{
		ovrTracking remoteState;
		if(controllers->mControllerIDs[i] != 0)
		{
			if(vrapi_GetInputTrackingState(ovrm, controllers->mControllerIDs[i], 0, &remoteState) >= 0)
			{
				clientDeviceState->SetControllerPose(i,	*((const avs::vec3 *)(&remoteState.HeadPose.Pose.Position)),
						*((const clientrender::quat *)(&remoteState.HeadPose.Pose.Orientation)));
			}
		}
	}

	std::shared_ptr<clientrender::Node> body = geometryCache.mNodeManager->GetBody();
	if(body)
	{
		// TODO: SHould this be globalPose??
		body->SetLocalPosition(clientDeviceState->headPose.localPose.position + lastSetupCommand.bodyOffsetFromHead);

		//Calculate rotation angle on y-axis, and use to create new quaternion that only rotates the body on the y-axis.
		float angle = std::atan2(clientDeviceState->headPose.localPose.orientation.y, clientDeviceState->headPose.localPose.orientation.w);
		clientrender::quat yRotation(0.0f, std::sin(angle), 0.0f, std::cos(angle));
		body->SetLocalRotation(yRotation);
	}

	// Left and right hands have no parent and their position/orientation is relative to the current local space.
	std::shared_ptr<clientrender::Node> rightHand = geometryCache.mNodeManager->GetRightHand();
	if(rightHand)
	{
		rightHand->SetLocalPosition(clientDeviceState->controllerPoses[1].globalPose.position);
		rightHand->SetLocalRotation(clientDeviceState->controllerPoses[1].globalPose.orientation);
	}

	std::shared_ptr<clientrender::Node> leftHand = geometryCache.mNodeManager->GetLeftHand();
	if(leftHand)
	{
		leftHand->SetLocalPosition(clientDeviceState->controllerPoses[0].globalPose.position);
		leftHand->SetLocalRotation(clientDeviceState->controllerPoses[0].globalPose.orientation);
	}
}

bool ClientRenderer::OnSetupCommandReceived(const char *server_ip, const avs::SetupCommand &setupCommand, avs::Handshake &handshake)
{
	avs::VideoConfig &videoConfig=clientPipeline.videoConfig;
	videoConfig = setupCommand.video_config;
	if (!mPipelineConfigured)
	{
		OVR_WARN("VIDEO STREAM CHANGED: server port %d %d %d, cubemap %d", setupCommand.server_streaming_port,
				videoConfig.video_width, videoConfig.video_height,
				videoConfig.colour_cubemap_size);

		teleport::client::ServerTimestamp::setLastReceivedTimestampUTCUnixMs(setupCommand.startTimestamp_utc_unix_ms);
		sessionClient.SetPeerTimeout(setupCommand.idle_connection_timeout);

		const uint32_t geoStreamID = 80;
		std::vector<avs::NetworkSourceStream> streams = {{20}, {40}};
		if (AudioStream)
		{
			streams.push_back({60});
		}
		if (GeoStream)
		{
			streams.push_back({geoStreamID});
		}

		avs::NetworkSourceParams sourceParams;
		sourceParams.connectionTimeout = setupCommand.idle_connection_timeout;
		sourceParams.remoteIP = sessionClient.GetServerIP().c_str();
		sourceParams.remotePort = setupCommand.server_streaming_port;
		sourceParams.remoteHTTPPort = setupCommand.server_http_port;
		sourceParams.maxHTTPConnections = 10;
		sourceParams.httpStreamID = geoStreamID;
		sourceParams.useSSL = setupCommand.using_ssl;

		if (!clientPipeline.source.configure(std::move(streams), sourceParams))
		{
			OVR_WARN("OnSetupCommandReceived: Failed to configure network source node.");
			return false;
		}
		clientPipeline.source.setDebugStream(setupCommand.debug_stream);
		clientPipeline.source.setDebugNetworkPackets(setupCommand.debug_network_packets);
		clientPipeline.source.setDoChecksums(setupCommand.do_checksums);

		handshake.minimumPriority=GetMinimumPriority();
		// Don't use these on Android:
		handshake.renderingFeatures.normals=false;
		handshake.renderingFeatures.ambientOcclusion=false;
		clientPipeline.pipeline.add(&clientPipeline.source);

		videoTagDataCubeArray.clear();
		videoTagDataCubeArray.resize(MAX_TAG_DATA_COUNT);

		avs::DecoderParams &decoderParams = clientPipeline.decoderParams;
		decoderParams.codec = videoConfig.videoCodec;
		decoderParams.decodeFrequency = avs::DecodeFrequency::NALUnit;
		decoderParams.prependStartCodes = false;
		decoderParams.deferDisplay = false;
		decoderParams.use10BitDecoding = videoConfig.use_10_bit_decoding;
		decoderParams.useYUV444ChromaFormat = videoConfig.use_yuv_444_decoding;
		decoderParams.useAlphaLayerDecoding = videoConfig.use_alpha_layer_decoding;

		size_t stream_width = videoConfig.video_width;
		size_t stream_height = videoConfig.video_height;

		// Video
		if (!clientPipeline.decoder.configure(avs::DeviceHandle(), stream_width, stream_height,
				decoderParams, 20))
		{
			OVR_WARN("OnSetupCommandReceived: Failed to configure decoder node");
			clientPipeline.source.deconfigure();
			return false;
		}
		{
			clientrender::Texture::TextureCreateInfo textureCreateInfo = {};
			textureCreateInfo.externalResource = true;
			// Slot 1
			textureCreateInfo.slot = clientrender::Texture::Slot::NORMAL;
			textureCreateInfo.format = clientrender::Texture::Format::RGBA8;
			textureCreateInfo.type = clientrender::Texture::Type::TEXTURE_2D_EXTERNAL_OES;
			textureCreateInfo.height = videoConfig.video_height;
			textureCreateInfo.width = videoConfig.video_width;
			textureCreateInfo.depth = 1;

			mVideoTexture->Create(textureCreateInfo);
			((scc::GL_Texture *) (mVideoTexture.get()))->SetExternalGlTexture(
					mVideoSurfaceTexture->GetTextureId());

			// Slot 2
			textureCreateInfo.slot = clientrender::Texture::Slot::COMBINED;
			mAlphaVideoTexture->Create(textureCreateInfo);
			((scc::GL_Texture *) (mAlphaVideoTexture.get()))->SetExternalGlTexture(
					mAlphaSurfaceTexture->GetTextureId());
		}

		VideoSurface* alphaSurface;
		if (videoConfig.use_alpha_layer_decoding)
		{
			alphaSurface = new VideoSurface(mAlphaSurfaceTexture);
		}
		else
		{
			alphaSurface = nullptr;
		}

		clientPipeline.surface.configure(new VideoSurface(mVideoSurfaceTexture), alphaSurface);

		clientPipeline.videoQueue.configure(200000, 16, "VideoQueue");

		avs::PipelineNode::link(clientPipeline.source, clientPipeline.videoQueue);
		avs::PipelineNode::link(clientPipeline.videoQueue, clientPipeline.decoder);
		clientPipeline.pipeline.link({&clientPipeline.decoder, &clientPipeline.surface});


		// Tag Data
		{
			auto f = std::bind(&ClientRenderer::OnReceiveVideoTagData, this,
					std::placeholders::_1, std::placeholders::_2);
			if (!clientPipeline.tagDataDecoder.configure(40, f)) {
				OVR_WARN("OnSetupCommandReceived: Failed to configure tag data decoder node.");
				return false;
			}
			clientPipeline.tagDataQueue.configure(200, 16, "TagDataQueue");

			avs::PipelineNode::link(clientPipeline.source, clientPipeline.tagDataQueue);
			clientPipeline.pipeline.link({&clientPipeline.tagDataQueue, &clientPipeline.tagDataDecoder});
		}


		// Audio
		if (AudioStream)
		{
			clientPipeline.avsAudioDecoder.configure(60);
			sca::AudioSettings audioSettings;
			audioSettings.codec = sca::AudioCodec::PCM;
			audioSettings.numChannels = 2;
			audioSettings.sampleRate = 48000;
			audioSettings.bitsPerSample = 32;
			// This will be deconfigured automatically when the pipeline is deconfigured.
			audioPlayer->configure(audioSettings);
			audioStreamTarget.reset(new sca::AudioStreamTarget(audioPlayer));
			clientPipeline.avsAudioTarget.configure(audioStreamTarget.get());
			clientPipeline.audioQueue.configure(4096, 120, "AudioQueue");

			avs::PipelineNode::link(clientPipeline.source, clientPipeline.audioQueue);
			avs::PipelineNode::link(clientPipeline.audioQueue, clientPipeline.avsAudioDecoder);
			clientPipeline.pipeline.link({&clientPipeline.avsAudioDecoder, &clientPipeline.avsAudioTarget});

			// Audio Input
			if (setupCommand.audio_input_enabled)
			{
				sca::NetworkSettings networkSettings =
											 {
													 setupCommand.server_streaming_port + 1, server_ip, setupCommand.server_streaming_port
													 , static_cast<int32_t>(handshake.maxBandwidthKpS)
													 , static_cast<int32_t>(handshake.udpBufferSize)
													 , setupCommand.requiredLatencyMs
													 , (int32_t) setupCommand.idle_connection_timeout
											 };

				mNetworkPipeline.reset(new sca::NetworkPipeline());
				mAudioInputQueue.configure(4096, 120, "AudioInputQueue");
				mNetworkPipeline->initialise(networkSettings, &mAudioInputQueue);

				// Callback called on separate thread when recording buffer is full
				auto f = [this](const uint8_t *data, size_t dataSize) -> void
				{
					size_t bytesWritten;
					if (mAudioInputQueue.write(nullptr, data, dataSize, bytesWritten))
					{
						mNetworkPipeline->process();
					}
				};
				audioPlayer->startRecording(f);
			}
		}

		if (GeoStream)
		{
			clientPipeline.avsGeometryDecoder.configure(80, &geometryDecoder);
			clientPipeline.avsGeometryTarget.configure(&resourceCreator);
			clientPipeline.geometryQueue.configure(2500000, 100, "GeometryQueue");

			avs::PipelineNode::link(clientPipeline.source, clientPipeline.geometryQueue);
			avs::PipelineNode::link(clientPipeline.geometryQueue, clientPipeline.avsGeometryDecoder);
			clientPipeline.pipeline.link({&clientPipeline.avsGeometryDecoder, &clientPipeline.avsGeometryTarget});
		}
		//GL_CheckErrors("Pre-Build Cubemap");
		ConfigureVideo(videoConfig);

		mPipelineConfigured = true;
	}
	//GLCheckErrorsWithTitle("Built Lighting Cubemap");

	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();
	// Set discard distance to the sphere detection radius of the server for use in pixel shader.
	if(setupCommand.draw_distance!=0.0f)
		globalGraphicsResources.scrCamera->UpdateDrawDistance(setupCommand.draw_distance);
	else
		globalGraphicsResources.scrCamera->UpdateDrawDistance(10000.0f);

	handshake.startDisplayInfo.width = 1440;
	handshake.startDisplayInfo.height = 1600;
	handshake.framerate = 60;
	handshake.FOV = 110;
	handshake.isVR = true;
	handshake.udpBufferSize = static_cast<uint32_t>(clientPipeline.source.getSystemBufferSize());
	handshake.maxBandwidthKpS = 10 * handshake.udpBufferSize * static_cast<uint32_t>(handshake.framerate);
	handshake.axesStandard = avs::AxesStandard::GlStyle;
	handshake.MetresPerUnit = 1.0f;
	handshake.usingHands = true;
	handshake.maxLightsSupported=TELEPORT_MAX_LIGHTS;
	handshake.clientStreamingPort=client_streaming_port;

	lastSetupCommand = setupCommand;
	avs::ConvertPosition(setupCommand.axesStandard, avs::AxesStandard::GlStyle, lastSetupCommand.bodyOffsetFromHead);
	return true;
}
