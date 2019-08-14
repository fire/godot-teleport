#include "ResourceCreator.h"

using namespace avs;

ResourceCreator::ResourceCreator()
{
}

ResourceCreator::~ResourceCreator()
{
}

void ResourceCreator::SetRenderPlatform(scr::API::APIType api)
{
	m_API.SetAPI(api);
	
#if defined(_WIN32) || defined(WIN32) || defined (_WIN64) || defined(WIN64)
	switch (m_API.GetAPI())
	{
	case scr::API::APIType::UNKNOWN:	m_pRenderPlatform = nullptr; break;
	case scr::API::APIType::D3D11:		m_pRenderPlatform = std::make_unique<scr::PC_RenderPlatform>(); break;
	case scr::API::APIType::D3D12:		m_pRenderPlatform = std::make_unique<scr::PC_RenderPlatform>(); break;
	case scr::API::APIType::OPENGL:		m_pRenderPlatform = std::make_unique<scr::PC_RenderPlatform>(); break;
	case scr::API::APIType::OPENGLES:	m_pRenderPlatform = nullptr; break;
	case scr::API::APIType::VULKAN:		m_pRenderPlatform = std::make_unique<scr::PC_RenderPlatform>(); break;

	default:							m_pRenderPlatform = nullptr; break;
	}
#elif defined(__ANDROID__)
	switch (m_API.GetAPI())
	{
	case scr::API::APIType::UNKNOWN:	m_pRenderPlatform = nullptr; break;
	case scr::API::APIType::D3D11:		m_pRenderPlatform = nullptr; break;
	case scr::API::APIType::D3D12:		m_pRenderPlatform = nullptr; break;
	case scr::API::APIType::OPENGL:		m_pRenderPlatform = nullptr; break;
	case scr::API::APIType::OPENGLES:	m_pRenderPlatform = std::make_unique<scr::GL_RenderPlatform>(); break;
	case scr::API::APIType::VULKAN:		m_pRenderPlatform = nullptr; break;

	default:							m_pRenderPlatform = nullptr; break;
	}
#endif

}

void ResourceCreator::ensureVertices(unsigned long long shape_uid, int startVertex, int vertexCount, const avs::vec3* vertices)
{
	CHECK_SHAPE_UID(shape_uid);

	m_VertexCount = vertexCount;
	m_Vertices = vertices;
}

void ResourceCreator::ensureNormals(unsigned long long shape_uid, int startNormal, int normalCount, const avs::vec3* normals)
{
	CHECK_SHAPE_UID(shape_uid);
	if (normalCount != m_VertexCount)
		return;

	m_Normals = normals;
}

void ResourceCreator::ensureTangents(unsigned long long shape_uid, int startTangent, int tangentCount, const avs::vec4* tangents)
{
	CHECK_SHAPE_UID(shape_uid);
	if (tangentCount != m_VertexCount)
		return;

	m_Tangents = tangents;
}

void ResourceCreator::ensureTexCoord0(unsigned long long shape_uid, int startTexCoord0, int texCoordCount0, const avs::vec2* texCoords0)
{
	CHECK_SHAPE_UID(shape_uid);
	if (texCoordCount0 != m_VertexCount)
		return;

	m_UV0s = texCoords0;
}

void ResourceCreator::ensureTexCoord1(unsigned long long shape_uid, int startTexCoord1, int texCoordCount1, const avs::vec2* texCoords1)
{
	CHECK_SHAPE_UID(shape_uid);
	if (texCoordCount1 != m_VertexCount)
		return;

	m_UV1s = texCoords1;
}

void ResourceCreator::ensureColors(unsigned long long shape_uid, int startColor, int colorCount, const avs::vec4* colors)
{
	CHECK_SHAPE_UID(shape_uid);
	if (colorCount != m_VertexCount)
		return;

	m_Colors = colors;
}

void ResourceCreator::ensureJoints(unsigned long long shape_uid, int startJoint, int jointCount, const avs::vec4* joints)
{
	CHECK_SHAPE_UID(shape_uid);
	if (jointCount != m_VertexCount)
		return;

	m_Joints = joints;
}

void ResourceCreator::ensureWeights(unsigned long long shape_uid, int startWeight, int weightCount, const avs::vec4* weights)
{
	CHECK_SHAPE_UID(shape_uid);
	if (weightCount != m_VertexCount)
		return;

	size_t bufferSize = m_VertexCount * sizeof(avs::vec4);
	m_Weights = weights;
}

void ResourceCreator::ensureIndices(unsigned long long shape_uid, int startIndex, int indexCount, int indexSize, const unsigned char* indices)
{
	CHECK_SHAPE_UID(shape_uid);

	if (indexCount % 3 > 0)
	{
		SCR_CERR_BREAK("indexCount is not a multiple of 3.\n", -1);
		return;
	}
	m_PolygonCount = indexCount / 3;
	
	m_IndexCount = indexCount;
	m_Indices = indices;

}

avs::Result ResourceCreator::Assemble()
{
	using namespace scr;

	if(m_VertexBufferManager->Has(shape_uid) ||	m_IndexBufferManager->Has(shape_uid))
		return avs::Result::OK;

	if (!m_pRenderPlatform)
	{
		SCR_CERR("No valid render platform was found.");
        return avs::Result::GeometryDecoder_ClientRendererError;
	}

	std::shared_ptr<VertexBufferLayout> layout;
	if (m_Vertices)	{ layout->AddAttribute((uint32_t)AttributeSemantic::POSITION, VertexBufferLayout::ComponentCount::VEC3, VertexBufferLayout::Type::FLOAT);	}
	if (m_Normals)	{ layout->AddAttribute((uint32_t)AttributeSemantic::NORMAL, VertexBufferLayout::ComponentCount::VEC3, VertexBufferLayout::Type::FLOAT);		}
	if (m_Tangents)	{ layout->AddAttribute((uint32_t)AttributeSemantic::TANGENT, VertexBufferLayout::ComponentCount::VEC4, VertexBufferLayout::Type::FLOAT);		}
	if (m_UV0s)		{ layout->AddAttribute((uint32_t)AttributeSemantic::TEXCOORD_0, VertexBufferLayout::ComponentCount::VEC2, VertexBufferLayout::Type::FLOAT);	}
	if (m_UV1s)		{ layout->AddAttribute((uint32_t)AttributeSemantic::TEXCOORD_1, VertexBufferLayout::ComponentCount::VEC2, VertexBufferLayout::Type::FLOAT);	}
	if (m_Colors)	{ layout->AddAttribute((uint32_t)AttributeSemantic::COLOR_0, VertexBufferLayout::ComponentCount::VEC4, VertexBufferLayout::Type::FLOAT);		}
	if (m_Joints)	{ layout->AddAttribute((uint32_t)AttributeSemantic::JOINTS_0, VertexBufferLayout::ComponentCount::VEC4, VertexBufferLayout::Type::FLOAT);	}
	if (m_Weights)	{ layout->AddAttribute((uint32_t)AttributeSemantic::WEIGHTS_0, VertexBufferLayout::ComponentCount::VEC4, VertexBufferLayout::Type::FLOAT);	}
	layout->CalculateStride();

	size_t interleavedVBSize = 0;
	std::unique_ptr<float[]> interleavedVB = nullptr;
	interleavedVBSize = layout->m_Stride * m_VertexCount;
	interleavedVB = std::make_unique<float[]>(interleavedVBSize);

	for (size_t i = 0; i < m_VertexCount; i++)
	{
		size_t intraStrideOffset = 0;
		if(m_Vertices)	{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_Vertices + i, sizeof(vec3));intraStrideOffset +=3;}
		if(m_Normals)	{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_Normals + i, sizeof(vec3));	intraStrideOffset +=3;}
		if(m_Tangents)	{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_Tangents + i, sizeof(vec4));intraStrideOffset +=4;}
		if(m_UV0s)		{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_UV0s + i, sizeof(vec2));	intraStrideOffset +=2;}
		if(m_UV1s)		{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_UV1s + i, sizeof(vec2));	intraStrideOffset +=2;}
		if(m_Colors)	{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_Colors + i, sizeof(vec4));	intraStrideOffset +=4;}
		if(m_Joints)	{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_Joints + i, sizeof(vec4));	intraStrideOffset +=4;}
		if(m_Weights)	{memcpy(interleavedVB.get() + (layout->m_Stride * i) + intraStrideOffset, m_Weights + i, sizeof(vec4));	intraStrideOffset +=4;}
	}

	if (interleavedVBSize == 0 || interleavedVB == nullptr || m_IndexCount == 0 || m_Indices == nullptr)
	{
		SCR_CERR("Unable to construct vertex and index buffers.");
		return avs::Result::GeometryDecoder_ClientRendererError;
	}

	VertexBuffer::VertexBufferCreateInfo vb_ci;
	vb_ci.layout = std::move(layout);
	vb_ci.usage = (BufferUsageBit)(STATIC_BIT | DRAW_BIT);
	std::shared_ptr<scr::VertexBuffer> vb = m_pRenderPlatform->InstantiateVertexBuffer();
	vb->Create(&vb_ci, interleavedVBSize, (const void*)interleavedVB.get());

	IndexBuffer::IndexBufferCreateInfo ib_ci;
	ib_ci.usage = (BufferUsageBit)(STATIC_BIT | DRAW_BIT);
	std::shared_ptr<scr::IndexBuffer> ib = m_pRenderPlatform->InstantiateIndexBuffer();
	ib->Create(&ib_ci, m_IndexCount, (uint32_t*)m_Indices);

	m_VertexBufferManager->Add(shape_uid, std::move(vb), m_PostUseLifetime);
	m_IndexBufferManager->Add(shape_uid, std::move(ib), m_PostUseLifetime);

	m_Vertices = nullptr;
	m_Normals = nullptr;
	m_Tangents = nullptr;
	m_UV0s = nullptr;
	m_UV1s = nullptr;
	m_Colors = nullptr;
	m_Joints = nullptr;
	m_Weights = nullptr;
	m_Indices = nullptr;

    return avs::Result::OK;
}