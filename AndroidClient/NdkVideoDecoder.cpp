#include "NdkVideoDecoder.h"
#include <media/NdkMediaFormat.h>
#include <android/hardware_buffer_jni.h>
#include <iostream>
#include "Platform/Vulkan/Texture.h"
#include <TeleportCore/ErrorHandling.h>
#include <Platform/Vulkan/RenderPlatform.h>
#include <android/log.h>
#include <sys/prctl.h>
#include <fmt/core.h>
#include "Platform/Vulkan/EffectPass.h"

#define DISABLE_VULKAN_EXTERNAL_TEXTURE 0

#define VERBOSE_LOGGING 1
#if VERBOSE_LOGGING
#define verbose_log_print __android_log_print
#else
#define verbose_log_print(...)
#endif

void SetThreadName(const char* threadName)
{
	prctl(PR_SET_NAME, threadName, 0, 0, 0);
}

#define AMEDIA_CHECK(r)\
{\
	media_status_t res = r;\
	if(res != AMEDIA_OK)\
	{\
		TELEPORT_CERR << "NdkVideoDecoder - Failed" << std::endl;\
		return;\
	}\
}

static bool memory_type_from_properties(vk::PhysicalDevice* gpu, uint32_t typeBits, vk::MemoryPropertyFlags requirements_mask, uint32_t* typeIndex)
{
	vk::PhysicalDeviceMemoryProperties memory_properties;
	gpu->getMemoryProperties(&memory_properties);
	// Search memtypes to find first index with those properties
	for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
	{
		if ((typeBits & 1) == 1)
		{
			// Type is available, does it match user properties?
			if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask)
			{
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
	// No memory types matched, return failure
	return false;
}
static int VulkanFormatToHardwareBufferFormat(VkFormat v)
{
	switch (v)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
		return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_R8G8B8_UNORM:
		return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
	case VK_FORMAT_R5G6B5_UNORM_PACK16:
		return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
	case VK_FORMAT_D16_UNORM:
		return AHARDWAREBUFFER_FORMAT_D16_UNORM;
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return AHARDWAREBUFFER_FORMAT_D24_UNORM;
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_D32_SFLOAT:
		return AHARDWAREBUFFER_FORMAT_D32_FLOAT;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT;
	case VK_FORMAT_S8_UINT:
		return AHARDWAREBUFFER_FORMAT_S8_UINT;
	default:
		TELEPORT_BREAK_ONCE("");
		return 0;
	}
}

// TODO: this should be a member variabls:
std::mutex buffers_mutex;
std::atomic<bool> stopProcessBuffersThread = false;

NdkVideoDecoder::NdkVideoDecoder(VideoDecoderBackend* d, avs::VideoCodec codecType)
{
	videoDecoder = d;
	this->codecType = codecType;
	decoder = AMediaCodec_createDecoderByType(GetCodecMimeType());
}

NdkVideoDecoder::~NdkVideoDecoder()
{
}

void NdkVideoDecoder::Initialize(platform::crossplatform::RenderPlatform* p, platform::crossplatform::Texture* texture)
{
	if (decoderConfigured)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "VideoDecoder: Cannot initialize: already configured" << std::endl;
		return;
	}
	renderPlatform = (platform::vulkan::RenderPlatform*)p;
	targetTexture = (platform::vulkan::Texture*)texture;
	AMediaFormat* format = AMediaFormat_new();
	//decoderParams.maxDecodePictureBufferCount
	AMEDIA_CHECK(AImageReader_newWithUsage(targetTexture->width, targetTexture->length, AIMAGE_FORMAT_YUV_420_888, AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, videoDecoderParams.maxDecodePictureBufferCount, &imageReader));
	// nativeWindow is managed by the ImageReader.
	ANativeWindow* nativeWindow = nullptr;
	media_status_t status;
	AMEDIA_CHECK(AImageReader_getWindow(imageReader, &nativeWindow));
	//= AMediaFormat_createVideoFormat(getCodecMimeType, frameWidth, frameHeight)
	// Guessing the following is equivalent:
	AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, GetCodecMimeType());
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_WIDTH, targetTexture->width);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_HEIGHT, targetTexture->length);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, targetTexture->length);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, targetTexture->width);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, videoDecoderParams.bitRate);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, videoDecoderParams.frameRate);
	//int OUTPUT_VIDEO_COLOR_FORMAT =
	//        MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface;
	//AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21); // #21 COLOR_FormatYUV420SemiPlanar (NV12) 

	uint32_t flags = 0;
	//surface.setOnFrameAvailableListener(this)
	media_status_t res = AMediaCodec_configure(decoder, format, nativeWindow, nullptr, flags);
	if (res != AMEDIA_OK)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "Failed" << std::endl;
		return;
	}
	AMediaCodecOnAsyncNotifyCallback callback;
	callback.onAsyncInputAvailable = onAsyncInputAvailable;
	callback.onAsyncOutputAvailable = onAsyncOutputAvailable;
	callback.onAsyncFormatChanged = onAsyncFormatChanged;
	callback.onAsyncError = onAsyncError;
	res = AMediaCodec_setAsyncNotifyCallback(
		decoder,
		callback,
		this);
	if (res != AMEDIA_OK)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "Failed" << std::endl;
		return;
	}
	/*AMEDIA_CHECK( AMediaCodec_setOnFrameRenderedCallback(
					  mDecoder,
					  AMediaCodecOnFrameRendered callback,
					  void *userdata
					);*/
					//mDecoder.configure(format, Surface(surface), null, 0)
	AMEDIA_CHECK(AMediaCodec_start(decoder));
	int format_color;
	AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &format_color);
	TELEPORT_CERR << "NdkVideoDecoder AMEDIAFORMAT_KEY_COLOR_FORMAT - " << format_color << std::endl;
	auto format2 = AMediaCodec_getOutputFormat(decoder);
	AMediaFormat_getInt32(format2, AMEDIAFORMAT_KEY_COLOR_FORMAT, &format_color);
	TELEPORT_CERR << "NdkVideoDecoder AMEDIAFORMAT_KEY_COLOR_FORMAT - " << format_color << std::endl;

	int32_t format1 = 0;
	AMEDIA_CHECK(AImageReader_getFormat(imageReader, &format1));
	int32_t maxImages = 0;
	AMEDIA_CHECK(AImageReader_getMaxImages(imageReader, &maxImages));
	TELEPORT_CERR << "NdkVideoDecoder maxImages - " << maxImages << std::endl;
	reflectedTextures.resize(maxImages);
	for (int i = 0; i < maxImages; i++)
	{
		reflectedTextures[i].sourceTexture = (platform::vulkan::Texture*)renderPlatform->CreateTexture(fmt::format("video source {0}", i).c_str());
	}
	static AImageReader_ImageListener imageListener;
	imageListener.context = this;
	imageListener.onImageAvailable = onAsyncImageAvailable;
	AMEDIA_CHECK(AImageReader_setImageListener(imageReader, &imageListener));
	stopProcessBuffersThread = false;
	processBuffersThread = new std::thread(&NdkVideoDecoder::processBuffersOnThread, this);
	decoderConfigured = true;
}

void NdkVideoDecoder::Shutdown()
{
	stopProcessBuffersThread = true;
	if (processBuffersThread)
	{
		while (!processBuffersThread->joinable())
		{
		}
		processBuffersThread->join();
		delete processBuffersThread;
		processBuffersThread = nullptr;
	}
	if (!decoderConfigured)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "VideoDecoder: Cannot shutdown: not configured" << std::endl;
		return;
	}
	AImageReader_delete(imageReader);
	imageReader = nullptr;
	AMediaCodec_flush(decoder);
	//mDecoder.flush()
	AMediaCodec_stop(decoder);
	//mDecoder.stop()
	AMediaCodec_delete(decoder);
	decoder = nullptr;
	decoderConfigured = false;
	displayRequests = 0;
}

bool NdkVideoDecoder::Decode(std::vector<uint8_t>& buffer, avs::VideoPayloadType payloadType, bool lastPayload)
{
	if (!decoderConfigured)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "VideoDecoder: Cannot decode buffer: not configured" << std::endl;
		return false;
	}

	int payloadFlags = 0;
	switch (payloadType)
	{
	case avs::VideoPayloadType::VPS:payloadFlags = AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG; break;
	case avs::VideoPayloadType::PPS:payloadFlags = AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG; break;
	case avs::VideoPayloadType::SPS:payloadFlags = AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG; break;
	case avs::VideoPayloadType::ALE:payloadFlags = AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG; break;
	default:break;
	}
	std::vector<uint8_t> startCodes;
	switch (payloadFlags)
	{
	case AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG:
		startCodes = { 0, 0, 0, 1 };
		break;
	default:
		startCodes = { 0, 0, 1 };
		break;
	}

	if (!lastPayload)
	{
		// Signifies partial frame data. For all VCLs in a frame besides the last one. Needed for H264.
		if (payloadFlags == 0)
		{
			payloadFlags = AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME;
		}
	}

	std::vector<uint8_t> inputBuffer = startCodes;
	inputBuffer.resize(inputBuffer.size() + buffer.size());
	memcpy(inputBuffer.data() + startCodes.size(), buffer.data(), buffer.size());

	// get(dest,offset,length)
	// copies length bytes from this buffer into the given array,
	// starting at the current position of this buffer and at the given offset in the array.
	// The position of this buffer is then incremented by length.
	// buffer.get(inputBuffer, startCodes.size, buffer.remaining());
	// memcpy(inputBuffer.data(),buffer.data()+startCodes.size(),buffer.size()-startCodes.size());
	int32_t bufferId = QueueInputBuffer(inputBuffer, payloadFlags, lastPayload);
	if (lastPayload && bufferId >= 0)
	{
		++displayRequests;
		return true;
	}

	return false;
}

bool NdkVideoDecoder::Display()
{
	if (!decoderConfigured)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "VideoDecoder: Cannot display output: not configured" << std::endl;
		return false;
	}
	while (displayRequests > 0)
	{
		if (ReleaseOutputBuffer(displayRequests == 1) > -2)
			displayRequests--;
	}
	return true;
}

void NdkVideoDecoder::CopyVideoTexture(platform::crossplatform::GraphicsDeviceContext& deviceContext)
{
	if (!decoderConfigured)
	{
		return;
	}
	std::unique_lock<std::mutex> lock(_mutex);
	// start freeing images after 12 frames.
	for (int i = 0; i < texturesToFree.size(); i++)
	{
		FreeTexture(texturesToFree[i]);
	}
	texturesToFree.clear();
	if (!renderPlatform)
		return;
#if 1
	if (nextImageIndex < 0)
		return;
	ReflectedTexture& reflectedTexture = reflectedTextures[nextImageIndex];
	if (!reflectedTexture.nextImage)
		return;
	nextImageIndex = -1;
	platform::crossplatform::TextureCreate textureCreate;
	textureCreate.w = targetTexture->width;
	textureCreate.l = targetTexture->length;
	textureCreate.d = 1;
	textureCreate.arraysize = 1;
	textureCreate.mips = 1;
	textureCreate.f = platform::crossplatform::PixelFormat::UNDEFINED;//targetTexture->pixelFormat;
	textureCreate.numOfSamples = 1;
	textureCreate.make_rt = false;
	textureCreate.setDepthStencil = false;
	textureCreate.need_srv = true;
	textureCreate.cubemap = false;
	textureCreate.external_texture = (void*)reflectedTexture.videoSourceVkImage;
	textureCreate.forceInit = true;

	reflectedTexture.sourceTexture->InitFromExternalTexture(renderPlatform, &textureCreate);
	if (!reflectedTexture.sourceTexture->IsValid())
		return;
	reflectedTexture.sourceTexture->AssumeLayout(vk::ImageLayout::eUndefined);
#endif
	// Can't use CopyTexture because we can't use transfer_src.
	//renderPlatform->CopyTexture(deviceContext,targetTexture,sourceTexture);
	auto* effect = renderPlatform->copyEffect;
	auto* effectPass = (platform::vulkan::EffectPass*)effect->GetTechniqueByName("copy_2d_from_video")->GetPass(0);
	effectPass->SetVideoSource(true);
	targetTexture->activateRenderTarget(deviceContext);
	auto srcResource = effect->GetShaderResource("SourceTex2");
	auto dstResource = effect->GetShaderResource("DestTex2");
	renderPlatform->SetTexture(deviceContext, srcResource, reflectedTexture.sourceTexture);
	effect->SetUnorderedAccessView(deviceContext, dstResource, targetTexture);
	renderPlatform->ApplyPass(deviceContext, effectPass);
	int w = (targetTexture->width + 7) / 8;
	int l = (targetTexture->length + 7) / 8;
	renderPlatform->DispatchCompute(deviceContext, w, l, 1);
	renderPlatform->UnapplyPass(deviceContext);
	targetTexture->deactivateRenderTarget(deviceContext);
}

//Callbacks
void NdkVideoDecoder::onAsyncInputAvailable(AMediaCodec* codec, void* userdata, int32_t inputBufferId)
{
	NdkVideoDecoder* ndkVideoDecoder = (NdkVideoDecoder*)userdata;
	TELEPORT_COUT << "NdkVideoDecoder - " << "New input buffer: " << inputBufferId << std::endl;
	if (codec == ndkVideoDecoder->decoder)
		ndkVideoDecoder->nextInputBuffers.push_back({ inputBufferId,0,-1 });
}

void NdkVideoDecoder::onAsyncOutputAvailable(AMediaCodec* codec, void* userdata, int32_t outputBufferId, AMediaCodecBufferInfo* bufferInfo)
{
	NdkVideoDecoder* ndkVideoDecoder = (NdkVideoDecoder*)userdata;
	if (codec != ndkVideoDecoder->decoder)
		return;
	ndkVideoDecoder->outputBuffers.push_back({ outputBufferId,bufferInfo->offset,bufferInfo->size,bufferInfo->presentationTimeUs,bufferInfo->flags });
}

void NdkVideoDecoder::onAsyncImageAvailable(void* context, AImageReader* reader)
{
	NdkVideoDecoder* ndkVideoDecoder = (NdkVideoDecoder*)context;

#if !DISABLE_VULKAN_EXTERNAL_TEXTURE
	if (!reader)
#endif
		return;
	static int counter = 0;
	ndkVideoDecoder->reflectedTextureIndex++;
	if (ndkVideoDecoder->reflectedTextureIndex >= ndkVideoDecoder->reflectedTextures.size())
		ndkVideoDecoder->reflectedTextureIndex = 0;
	TELEPORT_CERR << "NdkVideoDecoder - onAsyncImageAvailable " << ndkVideoDecoder->reflectedTextureIndex << std::endl;
	ReflectedTexture& reflectedTexture = ndkVideoDecoder->reflectedTextures[ndkVideoDecoder->reflectedTextureIndex];

	int32_t format1 = 0;
	AMEDIA_CHECK(AImageReader_getFormat(ndkVideoDecoder->imageReader, &format1));
	int32_t maxImages = 0;
	AMEDIA_CHECK(AImageReader_getMaxImages(ndkVideoDecoder->imageReader, &maxImages));

	// Does this mean we can now do AImageReader_acquireNextImage?
	ndkVideoDecoder->acquireFenceFd = 0;
	auto res = AImageReader_acquireLatestImage(ndkVideoDecoder->imageReader, &reflectedTexture.nextImage);
	//auto res=AImageReader_acquireLatestImageAsync(imageReader, &nextImage,&acquireFenceFd) ;
	//
	if (res != AMEDIA_OK)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "AImageReader_acquireLatestImage Failed " << std::endl;
		ndkVideoDecoder->FreeTexture(ndkVideoDecoder->reflectedTextureIndex);
		ndkVideoDecoder->reflectedTextureIndex--;
		return;
	}
	std::unique_lock<std::mutex> lock(ndkVideoDecoder->_mutex);
	// start freeing images after 12 frames.
	int idx = (ndkVideoDecoder->reflectedTextureIndex + 4) % maxImages;
	ndkVideoDecoder->texturesToFree.push_back(idx);

	vk::Device* vulkanDevice = ndkVideoDecoder->renderPlatform->AsVulkanDevice();
	AHardwareBuffer* hardwareBuffer = nullptr;
	AMEDIA_CHECK(AImage_getHardwareBuffer(reflectedTexture.nextImage, &hardwareBuffer));
	auto vkd = ndkVideoDecoder->renderPlatform->AsVulkanDevice()->operator VkDevice();
	vk::AndroidHardwareBufferPropertiesANDROID		properties;
	vk::AndroidHardwareBufferFormatPropertiesANDROID  formatProperties;
	properties.pNext = &formatProperties;
	vk::Result vkResult = vulkanDevice->getAndroidHardwareBufferPropertiesANDROID(hardwareBuffer, &properties);
	AHardwareBuffer_Desc hardwareBufferDesc{};
	AHardwareBuffer_describe(hardwareBuffer, &hardwareBufferDesc);
	// have to actually CREATE a vkImage EVERY FRAME????

		// mTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkExternalFormatANDROID externalFormatAndroid{
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
		.pNext = nullptr,
		.externalFormat = formatProperties.externalFormat,
	};

	vk::ExternalMemoryImageCreateInfo externalMemoryImageCreateInfo;
	externalMemoryImageCreateInfo.pNext = &externalFormatAndroid;
	externalMemoryImageCreateInfo.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID;
	//		VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;


	vk::ImageCreateInfo imageCreateInfo = vk::ImageCreateInfo();
	imageCreateInfo.pNext = &externalMemoryImageCreateInfo;
	//imageCreateInfo.flags = vk::ImageCreateFlagBits::eu;
	imageCreateInfo.imageType = vk::ImageType::e2D;
	imageCreateInfo.format = vk::Format::eUndefined;//eG8B8R83Plane420UnormKHR eUndefined
	imageCreateInfo.extent = vk::Extent3D((uint32_t)ndkVideoDecoder->targetTexture->width, (uint32_t)ndkVideoDecoder->targetTexture->length, 1);
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = hardwareBufferDesc.layers;
	imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
	imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
	imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled;//|vk::ImageUsageFlagBits::eTransferSrc;//VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
	imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
	imageCreateInfo.queueFamilyIndexCount = 0;
	imageCreateInfo.pQueueFamilyIndices = nullptr;
	imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;
	// Not allowed with external format: imageCreateInfo.flags=vk::ImageCreateFlagBits::eMutableFormat;//VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT

	auto result = vulkanDevice->createImage(&imageCreateInfo, nullptr, &reflectedTexture.videoSourceVkImage);

	vk::PhysicalDeviceMemoryProperties memoryProperties;
	ndkVideoDecoder->renderPlatform->GetVulkanGPU()->getMemoryProperties(&memoryProperties);
	// Required for AHB images.
	vk::MemoryDedicatedAllocateInfo memoryDedicatedAllocateInfo = vk::MemoryDedicatedAllocateInfo().setImage(reflectedTexture.videoSourceVkImage);

	vk::ImportAndroidHardwareBufferInfoANDROID hardwareBufferInfo = vk::ImportAndroidHardwareBufferInfoANDROID()
		.setBuffer(hardwareBuffer)
		.setPNext(&memoryDedicatedAllocateInfo);

	vk::MemoryRequirements mem_reqs;

	//vulkanDevice->getImageMemoryRequirements(videoSourceVkImage, &mem_reqs);
	mem_reqs.size = properties.allocationSize;
	mem_reqs.memoryTypeBits = properties.memoryTypeBits;
	vk::MemoryAllocateInfo mem_alloc_info = vk::MemoryAllocateInfo()
		.setAllocationSize(properties.allocationSize)
		.setMemoryTypeIndex(memoryProperties.memoryTypes[0].heapIndex)
		.setMemoryTypeIndex(1 << (__builtin_ffs(properties.memoryTypeBits) - 1))
		.setPNext(&hardwareBufferInfo);
	// overwrites the above
	memory_type_from_properties(ndkVideoDecoder->renderPlatform->GetVulkanGPU(), mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal,
		&mem_alloc_info.memoryTypeIndex);
	SIMUL_VK_CHECK(vulkanDevice->allocateMemory(&mem_alloc_info, nullptr, &reflectedTexture.mMem));
	// Dedicated memory bindings require offset 0.
	// Returns void:
	vulkanDevice->bindImageMemory(reflectedTexture.videoSourceVkImage, reflectedTexture.mMem, /*offset*/ 0);

	TELEPORT_CERR << "NdkVideoDecoder - " << "AImageReader_acquireLatestImage Succeeded" << std::endl;

	ndkVideoDecoder->nextImageIndex = ndkVideoDecoder->reflectedTextureIndex;
}

void NdkVideoDecoder::onAsyncFormatChanged(AMediaCodec* codec, void* userdata, AMediaFormat* format)
{
	NdkVideoDecoder* ndkVideoDecoder = (NdkVideoDecoder*)userdata;

	int format_color;
	//auto outp_format = AMediaCodec_getOutputFormat(mDecoder);
	AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &format_color);
	TELEPORT_COUT << "NdkVideoDecoder - " << "onAsyncFormatChanged: " << format_color << std::endl;
}

void NdkVideoDecoder::onAsyncError(AMediaCodec* codec, void* userdata, media_status_t error, int32_t actionCode, const char* detail)
{
	NdkVideoDecoder* ndkVideoDecoder = (NdkVideoDecoder*)userdata;
	TELEPORT_CERR << "NdkVideoDecoder - " << "VideoDecoder: error: " << detail << std::endl;
}

//Others
void NdkVideoDecoder::FreeTexture(int index)
{
	if (index < 0 || index >= reflectedTextures.size())
		return;
	ReflectedTexture& reflectedTexture = reflectedTextures[index];
	if (reflectedTexture.nextImage)
	{
		//vk::Device *vulkanDevice=renderPlatform->AsVulkanDevice();
		renderPlatform->PushToReleaseManager(reflectedTexture.mMem);
		//reflectedTexture.sourceTexture->InvalidateDeviceObjects();
		AImage_delete(reflectedTexture.nextImage);
		reflectedTexture.nextImage = nullptr;
	}
}

#pragma clang optimize off
int32_t NdkVideoDecoder::QueueInputBuffer(std::vector<uint8_t>& b, int flg, bool snd)
{
	//ByteBuffer inputBuffer = codec->getInputBuffer(inputBufferId);
	buffers_mutex.lock();
	dataBuffers.push_back({ b,flg,snd });
	buffers_mutex.unlock();
	return 0;
}

int NdkVideoDecoder::ReleaseOutputBuffer(bool render)
{
	buffers_mutex.lock();
	if (!outputBuffers.size())
	{
		buffers_mutex.unlock();
		return 0;
	}
	buffers_mutex.unlock();
	return 0;
}

const char* NdkVideoDecoder::GetCodecMimeType()
{
	switch (codecType)
	{
	case avs::VideoCodec::H264:
		return "video/avc";
	case avs::VideoCodec::HEVC:
		return "video/hevc";
	default:
		return "Invalid";
	};
}

//Processing thread
void NdkVideoDecoder::processBuffersOnThread()
{
	SetThreadName("processBuffersOnThread");
	while (!stopProcessBuffersThread)
	{
		buffers_mutex.lock();
		processInputBuffers();
		processOutputBuffers();
		processImages();
		buffers_mutex.unlock();
		std::this_thread::sleep_for(std::chrono::nanoseconds(10000));
	}
}

void NdkVideoDecoder::processInputBuffers()
{
	// Returns the index of an input buffer to be filled with valid data or -1 if no such buffer is currently available.
	//ssize_t inputBufferID=AMediaCodec_dequeueInputBuffer(codec,0);
	//val inputBufferID = mDecoder.dequeueInputBuffer(0) // microseconds
	if (!dataBuffers.size())
	{
		// Nothing to process.
		return;
	}
	if (!nextInputBuffers.size())
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "Out of buffers, queueing." << std::endl;
		return;
	}
	DataBuffer& dataBuffer = dataBuffers[0];
	bool send = dataBuffer.send;
	bool lastpacket = dataBuffer.send;
	send = false;
	InputBuffer& inputBuffer = nextInputBuffers[nextInputBufferIndex];


	size_t buffer_size = 0;
	size_t copiedSize = 0;
	uint8_t* targetBufferData = AMediaCodec_getInputBuffer(decoder, inputBuffer.inputBufferId, &buffer_size);
	if (!targetBufferData)
	{
		__android_log_print(ANDROID_LOG_INFO, "processInputBuffers", "AMediaCodec_getInputBuffer failed.");
		return;
	}
	if (!inputBuffer.offset)
		verbose_log_print(ANDROID_LOG_INFO, "processInputBuffers", "AMediaCodec_getInputBuffer %d got size %zu", inputBuffer.inputBufferId, buffer_size);
	copiedSize = std::min(buffer_size - inputBuffer.offset, dataBuffer.bytes.size());
	bool add_to_new = !targetBufferData || copiedSize < dataBuffer.bytes.size();
	// if buffer is valid, copy our data into it.
	if (targetBufferData && copiedSize == dataBuffer.bytes.size())
	{
		// if flags changes and data is already on the buffer, send this buffer and add to a new one.
		if (inputBuffer.flags != -1 && dataBuffer.flags != inputBuffer.flags)
		{
			send = true;
			add_to_new = true;
		}
		else
		{
			memcpy(targetBufferData + inputBuffer.offset, dataBuffer.bytes.data(), copiedSize);
			inputBuffer.offset += copiedSize;
			inputBuffer.flags = dataBuffer.flags;
			//verbose_log_print(ANDROID_LOG_INFO,"processInputBuffers","buffer %d at offset %d added: %zu bytes with flag %d",inputBuffer.inputBufferId,inputBuffer.offset,copiedSize,dataBuffer.flags);
			// over half the buffer filled then send
			if (inputBuffer.offset >= buffer_size / 2)
				send = true;
		}
	}
	else
	{
		__android_log_print(ANDROID_LOG_INFO, "processInputBuffers", "buffer %d full", inputBuffer.inputBufferId);
		send = true;
	}
	// offset now equals the accumulated size of the buffer.
	if (send && inputBuffer.offset > 0)
	{
		media_status_t res = AMediaCodec_queueInputBuffer(decoder, inputBuffer.inputBufferId, 0, inputBuffer.offset, 0, inputBuffer.flags);
		if (res != AMEDIA_OK)
		{
			__android_log_print(ANDROID_LOG_INFO, "processInputBuffers", "AMediaCodec_getInputBuffer failed.");
			return;
		}
		//if(lastpacket)
		//	__android_log_print(ANDROID_LOG_INFO,"processInputBuffers","Last Packet.");
		verbose_log_print(ANDROID_LOG_INFO, "processInputBuffers", "buffer: %d SENT %zu bytes with flag %d.", inputBuffer.inputBufferId, inputBuffer.offset, inputBuffer.flags);
		nextInputBuffers.erase(nextInputBuffers.begin() + nextInputBufferIndex);

		if (nextInputBufferIndex >= nextInputBuffers.size())
			nextInputBufferIndex = 0;
	}
	if (add_to_new)
	{
		InputBuffer& nextInputBuffer = nextInputBuffers[nextInputBufferIndex];
		uint8_t* targetBufferData2 = AMediaCodec_getInputBuffer(decoder, nextInputBuffer.inputBufferId, &buffer_size);
		if (targetBufferData2)
		{
			verbose_log_print(ANDROID_LOG_INFO, "processInputBuffers", "newbuffer %d added: %lu bytes with flag %d.", inputBuffer.inputBufferId, dataBuffer.bytes.size(), dataBuffer.flags);
			memcpy(targetBufferData2, dataBuffer.bytes.data(), dataBuffer.bytes.size());
			nextInputBuffer.offset += dataBuffer.bytes.size();
			nextInputBuffer.flags = dataBuffer.flags;
		}
		else
		{
			__android_log_print(ANDROID_LOG_INFO, "processInputBuffers", "AMediaCodec_getInputBuffer failed.");
			return;
		}
	}
	dataBuffers.erase(dataBuffers.begin());
}

void NdkVideoDecoder::processOutputBuffers()
{
	if (!outputBuffers.size())
		return;

	OutputBuffer& outputBuffer = outputBuffers[0];
	if ((outputBuffer.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0)
	{
		__android_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "video decoder: codec config buffer");
		AMediaCodec_releaseOutputBuffer(decoder, outputBuffer.outputBufferId, false);
		return;
	}
	//auto bufferFormat = AMediaCodec_getOutputFormat(codec,outputBufferId); // option A
	// bufferFormat is equivalent to mOutputFormat
	// outputBuffer is ready to be processed or rendered.
	verbose_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "Output available %d, size: %d", outputBuffer.outputBufferId, outputBuffer.size);

	bool render = outputBuffer.size != 0;
	if (AMediaCodec_releaseOutputBuffer(decoder, outputBuffer.outputBufferId, true) != AMEDIA_OK)
	{
		TELEPORT_CERR << "NdkVideoDecoder - " << "AMediaCodec_releaseOutputBuffer Failed" << std::endl;
	}
	outputBuffers.erase(outputBuffers.begin());
}

void NdkVideoDecoder::processImages()
{
	if (!acquireFenceFd)
		return;
	ReflectedTexture& reflectedTexture = reflectedTextures[reflectedTextureIndex];
	if (!reflectedTexture.nextImage)
		return;
	acquireFenceFd = 0;

	//nextImage
	__android_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "NdkVideoDecoder - Succeeded");
	//..AImage_getHardwareBuffer
	// mInputSurface.makeCurrent();
	__android_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "output surface: await new image");
	//mOutputSurface.awaitNewImage();
	// Edit the frame and send it to the encoder.
	__android_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "output surface: draw image");
	//mOutputSurface.drawImage();
	// mInputSurface.setPresentationTime(info.presentationTimeUs * 1000);
	__android_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "input surface: swap buffers");
	//mInputSurface.swapBuffers();
	__android_log_print(ANDROID_LOG_INFO, "NdkVideoDecoder", "video encoder: notified of new frame");
	//mInputSurface.releaseEGLContext();
}
