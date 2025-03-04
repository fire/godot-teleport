// (C) Copyright 2018-2019 Simul Software Ltd
#include "GL_DeviceContext.h"
#include "../GlobalGraphicsResources.h"
#include <GLES3/gl32.h>
using namespace scc;
using namespace clientrender;

void GL_DeviceContext::Create(DeviceContextCreateInfo* pDeviceContextCreateInfo)
{
    m_CI = *pDeviceContextCreateInfo;
}

void GL_DeviceContext::Draw(InputCommand* pInputCommand)
{
    //Set up for DescriptorSet binding
    std::vector<const ShaderResource*> descriptorSets;
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();
	 GL_Effect* effect = dynamic_cast<GL_Effect*>(&globalGraphicsResources.defaultPBREffect);
    //Default Init
    dynamic_cast<GL_FrameBuffer *>(pInputCommand->pFBs)[0].BeginFrame();
    descriptorSets.push_back(&(pInputCommand->pCamera->GetShaderResource()));

    //Switch for types
    switch (pInputCommand->type)
    {
        case INPUT_COMMAND:
        {
            //NULL No other command to execute.
            break;
        }
        case INPUT_COMMAND_MESH_MATERIAL_TRANSFORM:
        {
            InputCommand_Mesh_Material_Transform* ic_mmt = dynamic_cast<InputCommand_Mesh_Material_Transform*>(pInputCommand);

            //Mesh
            const GL_VertexBuffer *gl_vertexbuffer=static_cast<const GL_VertexBuffer*>(ic_mmt->pVertexBuffer.get());
            gl_vertexbuffer->Bind();

            const GL_IndexBuffer* gl_IndexBuffer = static_cast<const GL_IndexBuffer*>(ic_mmt->pIndexBuffer.get());
            gl_IndexBuffer->Bind();
            m_IndexCount = static_cast<GLsizei>(gl_IndexBuffer->GetIndexBufferCreateInfo().indexCount);
            size_t stride = gl_IndexBuffer->GetIndexBufferCreateInfo().stride;
            m_Type = stride == 4 ? GL_UNSIGNED_INT : stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

            //Material
            const char* effectPassName = "";
            descriptorSets.push_back(&(ic_mmt->pMaterial->GetShaderResource()));
            effect->Bind(effectPassName);
            auto *pass=effect->GetEffectPassCreateInfo(effectPassName);
            if(pass)
            {
                m_Topology = GL_Effect::ToGLTopology(pass->topology);
            }

            //Transform
            descriptorSets.push_back(&(ic_mmt->pTransform.GetDescriptorSet()));

            break;
        }
        case INPUT_COMMAND_COMPUTE:
        {
            TELEPORT_CERR<<"Invalid Input Command";
        }
    }
    BindShaderResources(descriptorSets, effect, pInputCommand->effectPassName);
    glDrawElements(m_Topology, m_IndexCount, m_Type, nullptr);
}

void GL_DeviceContext::DispatchCompute(InputCommand* pInputCommand)
{
    const InputCommand_Compute& ic_c = *(dynamic_cast<InputCommand_Compute*>(pInputCommand));
    BindShaderResources({ic_c.m_ShaderResources}, ic_c.m_pComputeEffect.get(), pInputCommand->effectPassName);
    const uvec3& size = ic_c.m_WorkGroupSize;
#ifdef DEBUG
	if(size.x*size.y*size.z==0)
	{
		TELEPORT_CERR<<"Empty compute dispatch!\n";
	}
#endif
	GLCheckErrorsWithTitle("DispatchCompute: 1");
    glDispatchCompute(size.x, size.y, size.z);
    GLCheckErrorsWithTitle("DispatchCompute: 2");
    glMemoryBarrier(GL_UNIFORM_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    GLCheckErrorsWithTitle("DispatchCompute: 3");
}

void GL_DeviceContext::BeginFrame()
{

}

void GL_DeviceContext::EndFrame()
{

}

void GL_DeviceContext::BindShaderResources(const std::vector<const ShaderResource*>& shaderResources, Effect* pEffect, const char* effectPassName)
{
    //TODO: Move to OpenGL ES 3.2 for explicit in-shader UniformBlockBinding with the 'binding = X' layout qualifier!
    if(!pEffect)
        return; //SCR_CERR_BREAK("Invalid effect. Can not bind descriptor sets!", -1);
    //Set Uniforms for textures and UBs!
	GL_Effect *glEffect =dynamic_cast<GL_Effect*>(pEffect);
    GLuint& program = glEffect->GetGlProgram(effectPassName)->Program;
    glUseProgram(program);
	GLCheckErrorsWithTitle("BindShaderResources: 0");
    for(auto& sr : shaderResources)
    {
        for(auto& wsr : sr->GetWriteShaderResources())
	    {
	        ShaderResourceLayout::ShaderResourceType type = wsr.shaderResourceType;
	        if(type == ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER)
	        {
	            GLint location = glGetUniformLocation(program, wsr.shaderResourceName);
				GLCheckErrorsWithTitle("BindShaderResources: 1");

	            glUniform1i(location, wsr.dstBinding);
	            GLCheckErrorsWithTitle("BindShaderResources: 2");
	        }
	        if(type == ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER)
	        {
	            GLuint blockIndex = glGetUniformBlockIndex(program, wsr.shaderResourceName);
	            glUniformBlockBinding(program, blockIndex, wsr.dstBinding);
	            GLCheckErrorsWithTitle("BindShaderResources: 3");
	        }
	        else if(type == ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER)
	        {
	            GL_ShaderStorageBuffer *gl_shaderStorageBuffer = static_cast<GL_ShaderStorageBuffer *>(wsr.bufferInfo.buffer);
	            glBindBufferBase( GL_SHADER_STORAGE_BUFFER, wsr.dstBinding, gl_shaderStorageBuffer->GetGlBuffer().GetBuffer() );
	            GLCheckErrorsWithTitle("BindShaderResources: 3");
	            //NULL
	        }
            else
	        {
	            continue;
	        }
	    }
    }

    //Bind Resources
    for(auto& sr : shaderResources)
    {
        for(auto& wsr : sr->GetWriteShaderResources())
	    {
	        ShaderResourceLayout::ShaderResourceType type = wsr.shaderResourceType;
	        if(type == ShaderResourceLayout::ShaderResourceType::STORAGE_IMAGE)
	        {
	            dynamic_cast<const GL_Texture*>(wsr.imageInfo.texture.get())->BindForWrite(wsr.dstBinding,wsr.imageInfo.mip,wsr.imageInfo.layer);
	        }
	        else if(type == ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER)
	        {
	            dynamic_cast<const GL_Texture*>(wsr.imageInfo.texture.get())->Bind(wsr.imageInfo.mip,wsr.imageInfo.layer);
	        }
	        else if(type == ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER)
	        {
	            ((const GL_UniformBuffer*)(wsr.bufferInfo.buffer))->Submit();
				GLCheckErrorsWithTitle("BindShaderResources: 4");
	        }
	        else if(type == ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER)
	        {
	            ((GL_ShaderStorageBuffer*)(wsr.bufferInfo.buffer))->Access();
				GLCheckErrorsWithTitle("BindShaderResources: 5");
	        }
	        else
	        {
	            continue;
            }
        }
    }
}