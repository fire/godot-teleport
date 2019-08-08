// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include <api/FrameBuffer.h>
#include <api/Texture.h>
#include <EyeBuffers.h>

namespace scr
{
	//Implementation of FrameBuffer wrapping over ovrEyeBuffers
	class GL_FrameBuffer final : public FrameBuffer
	{
	private:
	    int eyeNum = 0;
        OVR::ovrEyeBufferParms m_EyeBuffersParms;
	    std::unique_ptr<OVR::ovrEyeBuffers> m_EyeBuffers;

	public:
		void Create(Texture::Format format, Texture::SampleCount sampleCount, uint32_t width, uint32_t height) override;
		void Destroy() override;

		void Bind() const override;
		void Unbind() const override;

		void Resolve() override;
		void UpdateFrameBufferSize(uint32_t width, uint32_t height) override;
		void Clear(float colour_r, float colour_g, float colour_b, float colour_a, float depth, uint32_t stencil) override;
	};
}
