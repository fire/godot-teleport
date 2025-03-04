// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include <ClientRender/VertexBuffer.h>
#include <ClientRender/VertexBufferLayout.h>
#include <Render/GlGeometry.h>

namespace scc
{
	class GL_VertexBuffer final : public clientrender::VertexBuffer
	{
	private:
		GLuint m_VertexID;
		GLuint m_VertexArrayID = 0;

	public:
        GL_VertexBuffer(const clientrender::RenderPlatform*const r)
        	:clientrender::VertexBuffer(r) {}

		void Create(VertexBufferCreateInfo* pVertexBufferCreateInfo) override;
		void Destroy() override;

		void Bind() const override;
		void Unbind() const override;

		bool ResourceInUse(int timeout) override {return true;}

		inline const GLuint& GetVertexID() const { return m_VertexID; }
		inline const GLuint& GetVertexArrayID() const { return m_VertexArrayID; }
		inline const VertexBufferCreateInfo& GetVertexBufferCreateInfo() const {return m_CI;}

	public:
		//Assume an interleaved VBO;
		void CreateVAO(GLuint indexBufferID);
	};
}