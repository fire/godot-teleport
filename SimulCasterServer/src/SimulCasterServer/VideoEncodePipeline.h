#pragma once

#include <memory>
#include "CasterContext.h"
#include "CasterSettings.h"

// Forward declare so classes that include don't have to know about them
namespace avs
{
	class Pipeline;
	class Encoder;
	class Surface;
}

namespace SCServer
{
	struct VideoEncodeParams
	{
		int32_t encodeWidth = 0;
		int32_t encodeHeight = 0;
		GraphicsDeviceType deviceType;
		void* deviceHandle = nullptr;
		void* inputSurfaceResource = nullptr;
	};

	class VideoEncodePipeline
	{
	public:
		VideoEncodePipeline() = default;
		~VideoEncodePipeline() = default;

		Result initialize(const CasterSettings& settings, const VideoEncodeParams& vidEncodeParams, avs::Node* output);
		Result process(avs::Transform& cameraTransform, bool forceIDR = false);
		Result release();

	private:
		std::unique_ptr<avs::Pipeline> pipeline;
		std::unique_ptr<avs::Surface> inputSurface;
		std::unique_ptr<avs::Encoder> encoder;
	};
}