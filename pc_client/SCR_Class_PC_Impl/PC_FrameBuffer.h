// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include <api/FrameBuffer.h>
#include <api/Texture.h>

namespace pc_client
{
	//Implementation of FrameBuffer wrapping over ovrEyeBuffers
	class PC_FrameBuffer final : public scr::FrameBuffer
	{
	private:

	public:
		PC_FrameBuffer(scr::RenderPlatform *r):scr::FrameBuffer(r) {}

		// Inherited via FrameBuffer
		void Destroy() override;

		void Bind() const override;
		void Unbind() const override;

		void Resolve() override;
		void UpdateFrameBufferSize(uint32_t width, uint32_t height) override;

		void Create(FrameBufferCreateInfo * pFrameBufferCreateInfo) override;
		void SetClear(ClearColous * pClearColours) override;
	};
}
