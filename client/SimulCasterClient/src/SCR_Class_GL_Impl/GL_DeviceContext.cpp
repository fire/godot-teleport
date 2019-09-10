// (C) Copyright 2018-2019 Simul Software Ltd
#include "GL_DeviceContext.h"

using namespace scc;
using namespace scr;

void GL_DeviceContext::Create(DeviceContextCreateInfo* pDeviceContextCreateInfo)
{
    m_CI = *pDeviceContextCreateInfo;
}

void GL_DeviceContext::Draw(InputCommand* pInputCommand)
{
    //Set up for DescriptorSet binding
    std::vector<ShaderResource> descriptorSets;
    Effect* effect = nullptr;

    //Default Init
    dynamic_cast<GL_FrameBuffer *>(pInputCommand->pFBs)[0].BeginFrame();
    pInputCommand->pCamera->UpdateCameraUBO();
    descriptorSets.push_back(pInputCommand->pCamera->GetDescriptorSet());

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
            dynamic_cast<const GL_VertexBuffer*>(ic_mmt->pMesh->GetMeshCreateInfo().vb.get())->Bind();

            const GL_IndexBuffer* gl_IndexBuffer = dynamic_cast<const GL_IndexBuffer*>(ic_mmt->pMesh->GetMeshCreateInfo().ib.get());
            gl_IndexBuffer->Bind();
            m_IndexCount = static_cast<GLsizei>(gl_IndexBuffer->GetIndexBufferCreateInfo().indexCount);
            size_t stride = gl_IndexBuffer->GetIndexBufferCreateInfo().stride;
            m_Type = stride == 4 ? GL_UNSIGNED_INT : stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

            //Material
            const char* effectPassName = "";
            descriptorSets.push_back(ic_mmt->pMaterial->GetShaderResource());
            effect = ic_mmt->pMaterial->GetMaterialCreateInfo().effect;
            dynamic_cast<const GL_Effect*>(effect)->Bind(effectPassName);
            m_Topology = dynamic_cast<const GL_Effect*>(effect)->ToGLTopology(effect->GetEffectPassCreateInfo(effectPassName).topology);

            //Transform
            descriptorSets.push_back(ic_mmt->pTransform->GetDescriptorSet());
            ic_mmt->pTransform->UpdateModelUBO();

            break;
        }
        case INPUT_COMMAND_COMPUTE:
        {
            SCR_COUT("Invalid Input Command");
        }
    }
    BindShaderResources(descriptorSets, effect);
    glDrawElements(m_Topology, m_IndexCount, m_Type, nullptr);
}
void GL_DeviceContext::DispatchCompute(InputCommand* pInputCommand)
{
    const InputCommand_Compute& ic_c = *(dynamic_cast<InputCommand_Compute*>(pInputCommand));

    BindShaderResources(ic_c.m_ShaderResources, ic_c.m_pComputeEffect.get());
    const uvec3& size = ic_c.m_WorkGroupSize;
    glDispatchCompute(size.x, size.y, size.z);
    glMemoryBarrier(GL_UNIFORM_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void GL_DeviceContext::BeginFrame()
{

}
void GL_DeviceContext::EndFrame()
{

}
void GL_DeviceContext::BindShaderResources(const std::vector<ShaderResource>& shaderResources, Effect* pEffect)
{
    //TODO: Move to OpenGL ES 3.2 for explicit in-shader UniformBlockBinding with the 'binding = X' layout qualifier!

    if(!pEffect)
        return; //SCR_CERR_BREAK("Invalid effect. Can not bind descriptor sets!", -1);

    //Set Uniforms for textures and UBs!
    GLuint& program = dynamic_cast<GL_Effect*>(pEffect)->GetGlPlatform().Program;
    glUseProgram(program);
    for(auto& sr : shaderResources)
    {
        for(auto& wsr : sr.GetWriteShaderResources())
        {
            ShaderResourceLayout::ShaderResourceType type = wsr.shaderResourceType;
            if(type == ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER)
            {
                GLint location = glGetUniformLocation(program, wsr.shaderResourceName);
                glUniform1i(location, wsr.dstBinding);
            }
            else if(type == ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER)
            {
                GLuint location = glGetUniformBlockIndex(program, wsr.shaderResourceName);
                glUniformBlockBinding(program, location, wsr.dstBinding);
            }
            else if(type == ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER)
            {
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
        for(auto& wsr : sr.GetWriteShaderResources())
        {
            ShaderResourceLayout::ShaderResourceType type = wsr.shaderResourceType;
            if(type == ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER)
            {
                dynamic_cast<const GL_Texture*>(wsr.imageInfo.texture.get())->Bind();
            }
            else if(type == ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER)
            {
                ((const GL_UniformBuffer*)(wsr.bufferInfo.buffer))->Submit();
            }
            else if(type == ShaderResourceLayout::ShaderResourceType::STORAGE_BUFFER)
            {
                ((GL_ShaderStorageBuffer*)(wsr.bufferInfo.buffer))->Access();
            }
            else
            {
                continue;
            }
        }
    }
}