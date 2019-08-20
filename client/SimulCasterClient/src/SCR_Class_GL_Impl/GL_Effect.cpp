// (C) Copyright 2018-2019 Simul Software Ltd
#include "GL_Effect.h"

using namespace scc;
using namespace scr;
using namespace OVR;

void GL_Effect::Create(EffectCreateInfo* pEffectCreateInfo)
{
    m_CI = *pEffectCreateInfo;
}
void GL_Effect::CreatePass(EffectPassCreateInfo* pEffectCreateInfo)
{
    m_EffectPasses[pEffectCreateInfo->effectPassName] = *pEffectCreateInfo;
}

void GL_Effect::LinkShaders(const char* effectPassName)
{
    Shader* vertex = nullptr;
    Shader* fragment = nullptr;

    ShaderSystem::Pipeline& pipeline = m_EffectPasses.at(effectPassName).pipeline;
    if(pipeline.m_Type == ShaderSystem::PipelineType::PIPELINE_TYPE_GRAPHICS)
    {
        for(size_t i = 0; i < pipeline.m_ShaderCount; i++)
        {
            if(pipeline.m_Shaders->GetShaderCreateInfo().stage == Shader::Stage ::SHADER_STAGE_VERTEX)
                vertex = &(pipeline.m_Shaders[i]);
            if(pipeline.m_Shaders->GetShaderCreateInfo().stage == Shader::Stage ::SHADER_STAGE_FRAGMENT)
                fragment = &(pipeline.m_Shaders[i]);
            else
                continue;
        }
    }
    else
    {
        SCR_CERR("Current OpenGL ES 3.0 implementation does not support compute shaders.");
        return;
    }

    assert(vertex != nullptr && fragment != nullptr);

    m_Program = GlProgram::Build(vertex->GetShaderCreateInfo().sourceCode, fragment->GetShaderCreateInfo().sourceCode, nullptr, 0);
}

void GL_Effect::Bind(const char* effectPassName) const
{
    /*EffectPassCreateInfo& epci = m_EffectPasses[effectPassName];


    //!Viewport and Scissor State!
    glEnable(GL_DEPTH_TEST | GL_SCISSOR_TEST);
    glViewport((GLint)epci.viewportAndScissor.x, (GLint)epci.viewportAndScissor.y, (GLint)epci.viewportAndScissor.width, (GLint)epci.viewportAndScissor.height);
    glDepthRangef(epci.viewportAndScissor.minDepth, epci.viewportAndScissor.maxDepth);
    glScissor(epci.viewportAndScissor.offsetX, epci.viewportAndScissor.offsetY, epci.viewportAndScissor.extentX, epci.viewportAndScissor.extentY);

    //!Rasterization State!
    glEnable(GL_CULL_FACE);
    glFrontFace(epci.rasterizationState.frontFace == FrontFace::COUNTER_CLOCKWISE ? GL_CCW : GL_CW);
    glCullFace(ToGLCullMode(epci.rasterizationState.cullMode));

    if(epci.rasterizationState.rasterizerDiscardEnable)
        glEnable(GL_RASTERIZER_DISCARD);
    else
        glDisable(GL_RASTERIZER_DISCARD);

    //TODO: Not supported in current OpenGL ES 3.0 loader!
    //if(epci.rasterizationState.depthClampEnable);
    //    glEnable(GL_DEPTH_CLAMP);
    //else
    //    glDisable(GL_DEPTH_CLAMP);

    //!Multisample Sample State!
    //TODO: Not supported in current OpenGL ES 3.0 loader! GL_EXT_multisample_compatibility?

    //!Depth Stenciling State!
    if(epci.depthStencilingState.depthTestEnable)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);

    if(epci.depthStencilingState.depthWriteEnable)
        glDepthMask(GL_TRUE);
    else
        glDepthMask(GL_FALSE);

    glDepthFunc(ToGLCompareOp(epci.depthStencilingState.depthCompareOp));

    //TODO: Not supported in current OpenGL ES 3.0 loader! GL_EXT_depth_bounds_test
    //if(epci.depthStencilingState.depthBoundTestEnable) {
    //    glEnable(DEPTH_BOUNDS_TEST_EXT)
    //    glDepthBoundEXT(epci.depthStencilingState.minDepthBounds, epci.depthStencilingState.maxDepthBounds);
    //}
    //else
    //   glDisable(DEPTH_BOUNDS_TEST_EXT)

    if(epci.depthStencilingState.stencilTestEnable)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);

    glStencilOpSeparate(GL_FRONT,
            ToGLStencilCompareOp(epci.depthStencilingState.frontCompareOp.stencilFailOp),
            ToGLStencilCompareOp(epci.depthStencilingState.frontCompareOp.stencilPassDepthFailOp),
            ToGLStencilCompareOp(epci.depthStencilingState.frontCompareOp.passOp));

    glStencilOpSeparate(GL_BACK,
            ToGLStencilCompareOp(epci.depthStencilingState.backCompareOp.stencilFailOp),
            ToGLStencilCompareOp(epci.depthStencilingState.backCompareOp.stencilPassDepthFailOp),
            ToGLStencilCompareOp(epci.depthStencilingState.backCompareOp.passOp));

    //!Colour Blending State!
    if(epci.colourBlendingState.blendEnable)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);

    glBlendEquationSeparate(ToGLBlendOp(epci.colourBlendingState.colorBlendOp), ToGLBlendOp(epci.colourBlendingState.alphaBlendOp));

    glBlendFuncSeparate(
            ToGLBlendFactor(epci.colourBlendingState.srcColorBlendFactor),
            ToGLBlendFactor(epci.colourBlendingState.dstColorBlendFactor),
            ToGLBlendFactor(epci.colourBlendingState.srcAlphaBlendFactor),
            ToGLBlendFactor(epci.colourBlendingState.dstAlphaBlendFactor));*/
}
void GL_Effect::Unbind(const char* effectPassName) const
{
    //NULL
}

GLenum GL_Effect::ToGLTopology(TopologyType topology) const
{
    switch (topology)
    {
        case TopologyType::POINT_LIST:     return GL_POINTS;
        case TopologyType::LINE_LIST:      return GL_LINES;
        case TopologyType::LINE_STRIP:     return GL_LINE_STRIP;
        case TopologyType::TRIANGLE_LIST:  return GL_TRIANGLES;
        case TopologyType::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        case TopologyType::TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
    }
};

GLenum GL_Effect::ToGLCullMode(CullMode cullMode) const
{
    switch (cullMode)
    {
    case CullMode::NONE:               return GL_BACK;
    case CullMode::FRONT_BIT:          return GL_FRONT;
    case CullMode::BACK_BIT:           return GL_BACK;
    case CullMode::FRONT_AND_BACK:     return GL_FRONT_AND_BACK;
    }
};

GLenum GL_Effect::ToGLCompareOp(CompareOp op) const
{
    switch(op)
    {
        case CompareOp::NEVER:            return GL_NEVER;
        case CompareOp::LESS:             return GL_LESS;
        case CompareOp::EQUAL:            return GL_EQUAL;
        case CompareOp::LESS_OR_EQUAL:    return GL_LEQUAL;
        case CompareOp::GREATER:          return GL_GREATER;
        case CompareOp::NOT_EQUAL:        return GL_NOTEQUAL;
        case CompareOp::GREATER_OR_EQUAL: return GL_GEQUAL;
        case CompareOp::ALWAYS:           return GL_ALWAYS;
    }
};

GLenum GL_Effect::ToGLStencilCompareOp(StencilCompareOp op) const
{
    switch (op) {
        case StencilCompareOp::KEEP:                    return GL_KEEP;
        case StencilCompareOp::ZERO:                    return GL_ZERO;
        case StencilCompareOp::REPLACE:                 return GL_REPLACE;
        case StencilCompareOp::INCREMENT_AND_CLAMP:     return GL_INCR;
        case StencilCompareOp::DECREMENT_AND_CLAMP:     return GL_DECR;
        case StencilCompareOp::INVERT:                  return GL_INVERT;
        case StencilCompareOp::INCREMENT_AND_WRAP:      return GL_INCR_WRAP;
        case StencilCompareOp::DECREMENT_AND_WRAP:      return GL_DECR_WRAP;
    }
};

GLenum GL_Effect::ToGLBlendFactor(BlendFactor factor) const
{
    switch(factor)
    {
        case BlendFactor::ZERO:                return GL_ZERO;
        case BlendFactor::ONE:                 return GL_ONE;
        case BlendFactor::SRC_COLOR:           return GL_SRC_COLOR;
        case BlendFactor::ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DST_COLOR:           return GL_DST_COLOR;
        case BlendFactor::ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SRC_ALPHA:           return GL_SRC_ALPHA;
        case BlendFactor::ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DST_ALPHA:           return GL_DST_ALPHA;
        case BlendFactor::ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    }
};
GLenum GL_Effect::ToGLBlendOp(BlendOp op) const
{
    switch (op) {
        case BlendOp::ADD:                  return GL_FUNC_ADD;
        case BlendOp::SUBTRACT:             return GL_FUNC_SUBTRACT;
        case BlendOp::REVERSE_SUBTRACT:     return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::MIN:                  return GL_MIN;
        case BlendOp::MAX:                  return GL_MAX;
    }
};