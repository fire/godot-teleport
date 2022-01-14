// (C) Copyright 2018-2022 Simul Software Ltd

#include "Camera.h"

using namespace clientrender;

// A couple of globals... wtf?
bool Camera::s_UninitialisedUB = true;
std::shared_ptr<UniformBuffer> Camera::s_UB = nullptr;

Camera::Camera(CameraCreateInfo* pCameraCreateInfo)
	:m_CI(*pCameraCreateInfo)
{
	if (s_UninitialisedUB)
	{
		UniformBuffer::UniformBufferCreateInfo ub_ci;
		ub_ci.name="u_cameraData";
		ub_ci.bindingLocation = 0;
		ub_ci.size = sizeof(CameraData);
		ub_ci.data =  &m_CameraData;

		s_UB = m_CI.renderPlatform->InstantiateUniformBuffer();
		s_UB->Create(&ub_ci);
		s_UninitialisedUB = false;
	}
	
	UpdatePosition(m_CI.position);
	UpdateOrientation(m_CI.orientation);

	UpdateView();

	UpdateDrawDistance(m_CI.drawDistance);

	m_ShaderResourceLayout.AddBinding(0, ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, Shader::Stage::SHADER_STAGE_VERTEX);

	m_ShaderResource = ShaderResource({ m_ShaderResourceLayout });
	m_ShaderResource.AddBuffer( ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 0, "u_CameraData", { s_UB.get(), 0, sizeof(CameraData) });
}

void Camera::UpdatePosition(const avs::vec3& position)
{
	m_CameraData.m_Position = position;
}

void Camera::UpdateOrientation(const quat& orientation)
{
	m_CameraData.m_Orientation = orientation;
}

void Camera::UpdateDrawDistance(float distance)
{
	m_CameraData.m_DrawDistance = distance;
}

const ShaderResource& Camera::GetShaderResource() const
{
	// I THINK this updates the values on the GPU...
	s_UB->Update();
	return m_ShaderResource;
}

void Camera::UpdateView()
{
	//Inverse for a translation matrix is a -position input. Inverse for a rotation matrix is its transpose.
	m_CameraData.m_ViewMatrix = mat4::Translation((m_CameraData.m_Position * -1)) * mat4::Rotation(m_CameraData.m_Orientation).Transposed();
}
void Camera::UpdateProjection(float horizontalFOV, float aspectRatio, float zNear, float zFar)
{
	if (m_CI.type != Camera::ProjectionType::PERSPECTIVE)
	{
		SCR_CERR<<"Invalid ProjectionType.\n";
		return;
	}
	m_CameraData.m_ProjectionMatrix = mat4::Perspective(horizontalFOV, aspectRatio, zNear, zFar);
}
void Camera::UpdateProjection(float left, float right, float bottom, float top, float near, float far)
{
	if (m_CI.type != Camera::ProjectionType::ORTHOGRAPHIC)
	{
		SCR_CERR<<"Invalid ProjectionType.\n";
		return;
	}
	m_CameraData.m_ProjectionMatrix = mat4::Orthographic(left, right, bottom, top, near, far);
}