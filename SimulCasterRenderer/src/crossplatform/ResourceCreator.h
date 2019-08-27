// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once
#include <libavstream/mesh.hpp>
#include <libavstream/geometry/mesh_interface.hpp>

#include "API.h"
#include "ResourceManager.h"
#include "ActorManager.h"
#include "api/RenderPlatform.h"

namespace scr
{
	class Material;
}

#if 0
// Removed circular dependencies.
#if defined(_WIN32) || defined(WIN32) || defined (_WIN64) || defined(WIN64)
#include "../../pc_client/SCR_Class_PC_Impl/PC_RenderPlatform.h" //Access to the PC_Client's Simul's DX11 and DX12 implementation of SCR;
#elif defined(__ANDROID__)
#include "../../client/SimulCasterClient/src/SCR_Class_GL_Impl/GL_RenderPlatform.h" //Access to the Android OpenGL ES 3.0 implementation of SCR;
#endif
#endif

/*! A class to receive geometry stream instructions and create meshes. It will then manage them for rendering and destroy them when done.*/
class ResourceCreator final : public avs::GeometryTargetBackendInterface
{
public:
	ResourceCreator();
	~ResourceCreator();
	
	void SetRenderPlatform(scr::RenderPlatform *r);

	inline void AssociateActorManager(scr::ActorManager* actorManager)
	{
		m_pActorManager = actorManager;
	}

	inline void AssociateResourceManagers(
		ResourceManager<std::shared_ptr<scr::IndexBuffer>>* indexBufferManager,
		ResourceManager<std::shared_ptr<scr::Shader>>* shaderManager,
		ResourceManager<scr::Material> *materialManager,
		ResourceManager<std::shared_ptr<scr::Texture>>* textureManager,
		ResourceManager<std::shared_ptr<scr::UniformBuffer>>* uniformBufferManager,
		ResourceManager<std::shared_ptr<scr::VertexBuffer>>* vertexBufferManager)
	{
		m_IndexBufferManager = indexBufferManager;
		m_ShaderManager = shaderManager;
		this->materialManager = materialManager;
		m_TextureManager = textureManager;
		m_UniformBufferManager = uniformBufferManager;
		m_VertexBufferManager = vertexBufferManager;
	}

private:
	// Inherited via GeometryTargetBackendInterface
	void ensureVertices(avs::uid shape_uid, int startVertex, int vertexCount, const avs::vec3* vertices) override;
	void ensureNormals(avs::uid shape_uid, int startNormal, int normalCount, const avs::vec3* normals) override;
	void ensureTangentNormals(avs::uid shape_uid, int startNormal, int tnCount, size_t tnSize, const uint8_t* tn) override;
	void ensureTangents(avs::uid shape_uid, int startTangent, int tangentCount, const avs::vec4* tangents) override;
	void ensureTexCoord0(avs::uid shape_uid, int startTexCoord0, int texCoordCount0, const avs::vec2* texCoords0) override;
	void ensureTexCoord1(avs::uid shape_uid, int startTexCoord1, int texCoordCount1, const avs::vec2* texCoords1) override;
	void ensureColors(avs::uid shape_uid, int startColor, int colorCount, const avs::vec4* colors) override;
	void ensureJoints(avs::uid shape_uid, int startJoint, int jointCount, const avs::vec4* joints) override;
	void ensureWeights(avs::uid shape_uid, int startWeight, int weightCount, const avs::vec4* weights) override;
	void ensureIndices(avs::uid shape_uid, int startIndex, int indexCount, int indexSize, const unsigned char* indices) override;
	void ensureMaterialUID(avs::uid shape_uid, avs::uid _material_uid) override;
	avs::Result Assemble() override;

	//Material and Texture
	void passTexture(avs::uid texture_uid, const avs::Texture& texture) override;
	void passMaterial(avs::uid material_uid, const avs::Material& material) override;

	//Transforms
	void passNode(avs::uid node_uid, avs::DataNode& node) override;

	//Actor
	void CreateActor(std::pair<avs::uid, avs::uid>& meshMaterialPair, avs::uid transform_uid) override;

	inline bool SetAndCheckShapeUID(const avs::uid& uid)
	{
		if (shape_uid == (avs::uid)-1)
		{
			shape_uid = uid;
			return true;
		}
		else if (shape_uid == uid)
		{
			return true;
		}
		else
			return false;
	}

#define CHECK_SHAPE_UID(x) if (!SetAndCheckShapeUID(x)) { SCR_COUT("Invalid shape_uid.\n"); return; }

public:

private:
	scr::API m_API;
	avs::uid shape_uid = (avs::uid)-1;
	scr::RenderPlatform* m_pRenderPlatform;
	
	uint32_t m_PostUseLifetime = 30000; //30,000ms = 30s
	ResourceManager<std::shared_ptr<scr::IndexBuffer>>*		m_IndexBufferManager;
	ResourceManager<scr::Material>							*materialManager;
	ResourceManager<std::shared_ptr<scr::Shader>>*			m_ShaderManager;
	ResourceManager<std::shared_ptr<scr::Texture>>*			m_TextureManager;
	ResourceManager<std::shared_ptr<scr::UniformBuffer>>*	m_UniformBufferManager;
	ResourceManager<std::shared_ptr<scr::VertexBuffer>>*	m_VertexBufferManager;

	scr::ActorManager* m_pActorManager;

	size_t m_VertexCount	= 0;
	size_t m_IndexCount		= 0;
	size_t m_PolygonCount	= 0;
	size_t m_IndexSize		= 0;

	const avs::vec3* m_Vertices		= nullptr;
	const avs::vec3* m_Normals		= nullptr;
	const avs::vec4* m_Tangents		= nullptr;
	const avs::vec2* m_UV0s			= nullptr;
	const avs::vec2* m_UV1s			= nullptr;
	const avs::vec4* m_Colors		= nullptr;
	const avs::vec4* m_Joints		= nullptr;
	const avs::vec4* m_Weights		= nullptr;
	const unsigned char* m_Indices	= nullptr;

	const uint8_t* m_TangentNormals = nullptr;
	size_t m_TangentNormalSize = 0;

	static std::vector<std::pair<avs::uid, avs::uid>> m_MeshMaterialUIDPairs;

	std::map<avs::uid, std::shared_ptr<avs::DataNode>> nodes;
};

