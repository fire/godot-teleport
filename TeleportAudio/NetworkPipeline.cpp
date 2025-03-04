#include "NetworkPipeline.h"

#include <algorithm>
#include <iostream>

#include "AudioCommon.h"

namespace
{
	constexpr int networkPipelineSocketBufferSize = 16 * 1024 * 1024; // 16MiB
}

namespace sca
{
	NetworkPipeline::NetworkPipeline()
		: prevProcResult(avs::Result::OK)
	{
	}

	NetworkPipeline::~NetworkPipeline()
	{
		release();
	}

	void NetworkPipeline::initialise(const NetworkSettings& inNetworkSettings, avs::Queue* audioQueue)
	{
		avs::NetworkSinkParams SinkParams = {};
		SinkParams.socketBufferSize = networkPipelineSocketBufferSize;
		SinkParams.throttleToRateKpS =  1; // Unused
		SinkParams.socketBufferSize = inNetworkSettings.clientBufferSize;
		SinkParams.requiredLatencyMs = inNetworkSettings.requiredLatencyMs;
		SinkParams.connectionTimeout = inNetworkSettings.connectionTimeout;

		pipeline.reset(new avs::Pipeline);
		networkSink.reset(new avs::NetworkSink);

		if(audioQueue)
		{
			audioPipes.resize(1);
			for (std::unique_ptr<AudioPipe>& pipe : audioPipes)
			{
				pipe = std::make_unique<AudioPipe>();
			}
			audioPipes[0]->sourceQueue = audioQueue;
		}

		std::vector<avs::NetworkSinkStream> streams;

		for (size_t i = 0; i < audioPipes.size(); ++i)
		{
			avs::NetworkSinkStream stream;
			stream.parserType = avs::StreamParserType::Audio;
			stream.useParser = false;
			stream.isDataLimitPerFrame = false;
			stream.counter = 0;
			stream.chunkSize = 2048;
			stream.id = 100 + (uint32_t)i;
			stream.dataType = avs::NetworkDataType::Audio;
			streams.emplace_back(std::move(stream));
		}

		if (!networkSink->configure(std::move(streams), nullptr, inNetworkSettings.localPort, inNetworkSettings.remoteIP, inNetworkSettings.remotePort, SinkParams))
		{
			SCA_CERR << "Failed to configure network sink!" << std::endl;
			return;
		}

		for (size_t i = 0; i < audioPipes.size(); ++i)
		{
			auto& pipe = audioPipes[i];
			if (!avs::PipelineNode::link(*pipe->sourceQueue, *networkSink))
			{
				SCA_CERR << "Failed to configure network pipeline for audio!" << std::endl;
				return;
			}
			pipeline->add(pipe->sourceQueue);
		}

		pipeline->add(networkSink.get());

		prevProcResult = avs::Result::OK;
	}

	void NetworkPipeline::release()
	{
		pipeline.reset();
		if (networkSink) networkSink->deconfigure();
		networkSink.reset();
		audioPipes.clear();
	}

	bool NetworkPipeline::process()
	{
		assert(pipeline);
		assert(networkSink);

		const avs::Result result = pipeline->process();
		// Prevent spamming of errors from NetworkSink. This happens when there is a connection issue.
		if (!result && result != avs::Result::IO_Empty)
		{
			if (result != prevProcResult)
			{
				SCA_CERR << "Network pipeline processing encountered an error!" << std::endl;
				prevProcResult = result;
			}
			return false;
		}
		prevProcResult = result;

		return true;
	}

	avs::Pipeline* NetworkPipeline::getAvsPipeline() const
	{
		return pipeline.get();
	}

	avs::Result NetworkPipeline::getCounters(avs::NetworkSinkCounters& counters) const
	{
		if (networkSink)
		{
			counters = networkSink->getCounters();
		}
		else
		{
			SCA_CERR << "Can't return counters because network sink is null." << std::endl;
			return avs::Result::Node_Null;
		}
		return avs::Result::OK;
	}
}