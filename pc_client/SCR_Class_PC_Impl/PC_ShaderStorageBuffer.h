// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

#include <ClientRender/ShaderStorageBuffer.h>

namespace pc_client
{
class PC_ShaderStorageBuffer final : public clientrender::ShaderStorageBuffer
	{
	private:

	public:
		PC_ShaderStorageBuffer(const clientrender::RenderPlatform* r) :clientrender::ShaderStorageBuffer(r) {}

		void Create(ShaderStorageBufferCreateInfo * pUniformBuffer) override {}
		void Update(size_t size, const void* data, uint32_t offset = 0) override {}
		void* Map() override;
		void Unmap() override;
		void Destroy() override {}

		bool ResourceInUse(int timeout) override { return true; }

		void Bind() const override {}
		void Unbind() const override {}

		void Access() override {}
};
}