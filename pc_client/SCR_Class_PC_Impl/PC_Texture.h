// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include "PC_Sampler.h"
#include <api/Texture.h>
#include <api/Sampler.h>

namespace scr
{
	class PC_Texture final : public Texture
	{
	private:

	public:
		PC_Texture() {}

		void Create(Slot slot, Type type, Format format, SampleCountBit sampleCount, uint32_t width, uint32_t height, uint32_t depth, uint32_t bitsPerPixel, const uint8_t* data) override;
		void Destroy() override;

		void Bind() const override;
		void Unbind() const override;

		void GenerateMips() override;
		void UseSampler(const Sampler* sampler) override;
		bool ResourceInUse(int timeout) override {return true;}

	private:
	};
}