#include "AudioEncoder.h"

#include <algorithm>
#include <set>

#include "libavstream/common.hpp"

#include "CasterSettings.h"

#include "ErrorHandling.h"


namespace SCServer
{
	AudioEncoder::AudioEncoder(const CasterSettings* settings)
		:settings(settings)
	{}

	avs::Result AudioEncoder::initialize(const avs::AudioEncoderParams& params)
	{
		this->params = params;
		return avs::Result::OK;
	}

	avs::Result AudioEncoder::encode(uint32_t timestamp, uint8_t* captureData, size_t captureDataSize)
	{
		return avs::Result::OK;
	}

	avs::Result AudioEncoder::mapOutputBuffer(void *& bufferPtr, size_t & bufferSizeInBytes)
	{
		return avs::Result::OK;
	}

	avs::Result AudioEncoder::unmapOutputBuffer()
	{
		return avs::Result::OK;
	}

	avs::Result AudioEncoder::shutdown()
	{
		return avs::Result::OK;
	}
}
