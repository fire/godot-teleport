// (C) Copyright 2018-2019 Simul Software Ltd

#include "Camera.h"

using namespace scr;

bool Camera::s_UninitialisedUB = true;

Camera::Camera(CameraCreateInfo* pCameraCreateInfo)
	:m_CI(*pCameraCreateInfo)
{
	if (s_UninitialisedUB)
	{

		UniformBuffer::UniformBufferCreateInfo ub_ci;
		ub_ci.bindingLocation = 0;
		ub_ci.size = sizeof(CameraData);
		ub_ci.data =  &m_CameraData;

		m_UB = m_CI.renderPlatform->InstantiateUniformBuffer();
		m_UB->Create(&ub_ci);
		s_UninitialisedUB = false;
	}
	
	UpdatePosition(m_CI.position);
	UpdateOrientation(m_CI.orientation);

	UpdateView();

	m_ShaderResourceLayout.AddBinding(0, ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, Shader::Stage::SHADER_STAGE_VERTEX);

	m_ShaderResource = ShaderResource({ m_ShaderResourceLayout });
	m_ShaderResource.AddBuffer(0, ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 0, "u_CameraData", { m_UB.get(), 0, sizeof(CameraData) });
}

void Camera::UpdatePosition(const vec3& position)
{
	m_CameraData.m_Position = position;
}
void Camera::UpdateOrientation(const quat& orientation)
{
	m_CameraData.m_Orientation = orientation;
}
void Camera::UpdateView()
{
	m_CameraData.m_ViewMatrix = mat4::Rotation(m_CameraData.m_Orientation) * mat4::Translation(m_CameraData.m_Position);
}
void Camera::UpdateProjection(float horizontalFOV, float aspectRatio, float zNear, float zFar)
{
	if (m_CI.type != Camera::ProjectionType::PERSPECTIVE)
	{
		SCR_COUT("Invalid ProjectionType.");
		return;
	}
	m_CameraData.m_ProjectionMatrix = mat4::Perspective(horizontalFOV, aspectRatio, zNear, zFar);
}
void Camera::UpdateProjection(float left, float right, float bottom, float top, float near, float far)
{
	if (m_CI.type != Camera::ProjectionType::ORTHOGRAPHIC)
	{
		SCR_COUT("Invalid ProjectionType.");
		return;
	}
	m_CameraData.m_ProjectionMatrix = mat4::Orthographic(left, right, bottom, top, near, far);
}