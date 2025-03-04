// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include "GL_Sampler.h"
#include <ClientRender/Texture.h>
#include <ClientRender/Sampler.h>
#include <Render/GlTexture.h>

namespace scc
{
	class GL_Texture final : public clientrender::Texture
	{
	private:
		OVRFW::GlTexture m_Texture;

	public:
		GL_Texture(const clientrender::RenderPlatform*const r)
			:clientrender::Texture(r) {}

		void Create(const TextureCreateInfo& pTextureCreateInfo) override;
		void Destroy() override;

		void Bind(uint32_t mip,uint32_t layer) const override;
		void BindForWrite(uint32_t slot,uint32_t mip,uint32_t layer) const override;
		void Unbind() const override;

		void GenerateMips() override;
		void UseSampler(const std::shared_ptr<clientrender::Sampler>& sampler) override;
		bool ResourceInUse(int timeout) override {return true;}

		inline OVRFW::GlTexture& GetGlTexture() { return m_Texture;}

		void SetExternalGlTexture(GLuint tex_id);
private:
		GLenum TypeToGLTarget(Type type) const;
		GLenum ToBaseGLFormat(Format format) const;
		GLenum ToGLFormat(Format format) const;
		GLenum ToGLCompressedFormat(CompressionFormat format, uint32_t bytesPerPixel) const;
};
}