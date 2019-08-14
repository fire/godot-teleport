// (C) Copyright 2018-2019 Simul Software Ltd

#include "Material.h"

using namespace scr;

bool Material::s_UninitialisedUB = false;

Material::Material(MaterialCreateInfo* pMaterialCreateInfo)
	:m_CI(*pMaterialCreateInfo)
{
	//Set up UB
	if (s_UninitialisedUB)
	{
		UniformBuffer::UniformBufferCreateInfo ub_ci;
		ub_ci.bindingLocation = 2;

		const float zero[sizeof(MaterialData)] = { 0 };
		m_UB->Create(&ub_ci, sizeof(MaterialData), zero);
		s_UninitialisedUB = true;
	}

	m_MaterialData.diffuseOutputScalar			= m_CI.diffuse.textureOutputScalar;
	m_MaterialData.diffuseTexCoordsScalar_R		= m_CI.diffuse.texCoordsScalar[0];
	m_MaterialData.diffuseTexCoordsScalar_G		= m_CI.diffuse.texCoordsScalar[1];
	m_MaterialData.diffuseTexCoordsScalar_B		= m_CI.diffuse.texCoordsScalar[2];
	m_MaterialData.diffuseTexCoordsScalar_A		= m_CI.diffuse.texCoordsScalar[3];

	m_MaterialData.normalOutputScalar			= m_CI.normal.textureOutputScalar;
	m_MaterialData.normalTexCoordsScalar_R		= m_CI.normal.texCoordsScalar[0];
	m_MaterialData.normalTexCoordsScalar_G		= m_CI.normal.texCoordsScalar[1];
	m_MaterialData.normalTexCoordsScalar_B		= m_CI.normal.texCoordsScalar[2];
	m_MaterialData.normalTexCoordsScalar_A		= m_CI.normal.texCoordsScalar[3];

	m_MaterialData.combinedOutputScalar			= m_CI.combined.textureOutputScalar;
	m_MaterialData.combinedTexCoordsScalar_R	= m_CI.combined.texCoordsScalar[0];
	m_MaterialData.combinedTexCoordsScalar_G	= m_CI.combined.texCoordsScalar[1];
	m_MaterialData.combinedTexCoordsScalar_B	= m_CI.combined.texCoordsScalar[2];
	m_MaterialData.combinedTexCoordsScalar_A	= m_CI.combined.texCoordsScalar[3];

	//Set up Descriptor Set for Textures and UB
	//UB from 0 - 9, Texture/Samplers 10+
	m_SetLayout.AddBinding(10, DescriptorSetLayout::DescriptorType::COMBINED_IMAGE_SAMPLER, Shader::Stage::SHADER_STAGE_FRAGMENT);
	m_SetLayout.AddBinding(11, DescriptorSetLayout::DescriptorType::COMBINED_IMAGE_SAMPLER, Shader::Stage::SHADER_STAGE_FRAGMENT);
	m_SetLayout.AddBinding(12, DescriptorSetLayout::DescriptorType::COMBINED_IMAGE_SAMPLER, Shader::Stage::SHADER_STAGE_FRAGMENT);
	m_SetLayout.AddBinding(3, DescriptorSetLayout::DescriptorType::UNIFORM_BUFFER, Shader::Stage::SHADER_STAGE_FRAGMENT);

	m_Set = DescriptorSet({ m_SetLayout });
	m_Set.AddImage(0, DescriptorSetLayout::DescriptorType::COMBINED_IMAGE_SAMPLER, 10, "u_Diffuse",  { m_CI.diffuse.texture->GetSampler(), m_CI.diffuse.texture });
	m_Set.AddImage(0, DescriptorSetLayout::DescriptorType::COMBINED_IMAGE_SAMPLER, 11, "u_Normal",   { m_CI.normal.texture->GetSampler(), m_CI.normal.texture });
	m_Set.AddImage(0, DescriptorSetLayout::DescriptorType::COMBINED_IMAGE_SAMPLER, 12, "u_Combined", { m_CI.combined.texture->GetSampler(), m_CI.combined.texture });
	m_Set.AddBuffer(0, DescriptorSetLayout::DescriptorType::UNIFORM_BUFFER, 3, "u_MaterialData", { m_UB.get(), 0, sizeof(MaterialData) });
}

void Material::UpdateMaterialUB()
{
	m_UB->Update(0, sizeof(MaterialData), &m_MaterialData);
}