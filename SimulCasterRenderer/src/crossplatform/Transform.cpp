#include "Transform.h"

using namespace scr;

bool Transform::s_UninitialisedUB = true;
std::shared_ptr<UniformBuffer> Transform::s_UB = nullptr;

Transform::Transform()
	: Transform(TransformCreateInfo{nullptr}, avs::vec3(), quat(0, 0, 0, 1.0f), avs::vec3(1.0f, 1.0f, 1.0f))
{}

Transform::Transform(const TransformCreateInfo& pTransformCreateInfo)
	: Transform(pTransformCreateInfo, avs::vec3(), quat(0, 0, 0, 1.0f), avs::vec3(1.0f, 1.0f, 1.0f))
{}

Transform::Transform(const TransformCreateInfo& pTransformCreateInfo, avs::vec3 translation, quat rotation, avs::vec3 scale)
	: m_Translation(translation), m_Rotation(rotation), m_Scale(scale)
{
	if(false)//s_UninitialisedUB)
	{
		UniformBuffer::UniformBufferCreateInfo ub_ci;
		ub_ci.bindingLocation = 1;
		ub_ci.size = sizeof(TransformData);
		ub_ci.data = &m_TransformData;

		s_UB = m_CI.renderPlatform->InstantiateUniformBuffer();
		s_UB->Create(&ub_ci);
		s_UninitialisedUB = false;
	}

	m_ShaderResourceLayout.AddBinding(1, ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, Shader::Stage::SHADER_STAGE_VERTEX);

	m_ShaderResource = ShaderResource({m_ShaderResourceLayout});
	m_ShaderResource.AddBuffer( ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 1, "u_NodeUBO", {s_UB.get(), 0, sizeof(TransformData)});

	m_TransformData.m_ModelMatrix = mat4::Translation(translation) * mat4::Rotation(rotation) * mat4::Scale(scale);
}

Transform::Transform(const TransformCreateInfo& pTransformCreateInfo, scr::mat4 matrix)
{
	m_TransformData.m_ModelMatrix = matrix;

	m_Translation = matrix.GetTranslation();
	m_Rotation = matrix.GetRotation();
	m_Scale = matrix.GetScale();
}

Transform::Transform(const avs::Transform& transform)
	:m_Translation(transform.position), m_Rotation(transform.rotation), m_Scale(transform.scale)
{
	m_TransformData.m_ModelMatrix = mat4::Translation(m_Translation) * mat4::Rotation(m_Rotation) * mat4::Scale(m_Scale);
}

void Transform::UpdateModelMatrix()
{
	m_TransformData.m_ModelMatrix = mat4::Translation(m_Translation) * mat4::Rotation(m_Rotation) * mat4::Scale(m_Scale);
}

bool Transform::UpdateModelMatrix(const avs::vec3& translation, const quat& rotation, const avs::vec3& scale)
{
	if(scale.x < 0.0001f)
	{
		SCR_CERR << "Failed to update model matrix of transform! Scale.x is zero!\n";
		return false;
	}

	if(m_Translation != translation || m_Rotation != rotation || m_Scale != scale)
	{
		m_Translation = translation;
		m_Rotation = rotation;
		m_Scale = scale;
		UpdateModelMatrix();

		return true;
	}
	return false;
}