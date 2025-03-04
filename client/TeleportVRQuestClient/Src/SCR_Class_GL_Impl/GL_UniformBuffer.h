// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include <ClientRender/UniformBuffer.h>
#include <Render/GlBuffer.h>

namespace scc
{
	class GL_UniformBuffer final : public clientrender::UniformBuffer
	{
	private:
		OVRFW::GlBuffer m_UBO;

	public:
		GL_UniformBuffer(const clientrender::RenderPlatform*const r)
			:clientrender::UniformBuffer(r) {}

		//Binding Locations for UBOs
		//Camera = 0;
		//Model = 1;
		//Light = 2;
		void Create(UniformBufferCreateInfo* pUniformBufferCreateInfo) override;
		void Destroy() override;

		void Submit() const override;
		void Update() const override;

		bool ResourceInUse(int timeout) override {return true;}

		void Bind() const override;
		void Unbind() const override;

		inline OVRFW::GlBuffer& GetGlBuffer() { return m_UBO; }
	};
}