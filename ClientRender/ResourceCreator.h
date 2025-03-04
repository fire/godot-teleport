// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#include "transcoder/basisu_transcoder.h"

#include <libavstream/geometry/mesh_interface.hpp>
#include <libavstream/mesh.hpp>

#include "API.h"
#include "Platform/Shaders/SL/CppSl.sl"
#include "Light.h"
#include "MemoryUtil.h"
#include "NodeManager.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "GeometryCache.h"
#include "FontAtlas.h"
#include "TextCanvas.h"

namespace clientrender
{
	class Animation;
	class Material;
}

namespace clientrender
{
	/*! A class to receive geometry stream instructions and create meshes. It will then manage them for rendering and destroy them when done.*/
	class ResourceCreator final : public avs::GeometryTargetBackendInterface
	{
	public:

		std::shared_ptr<clientrender::Material> placeholderMaterial;

		ResourceCreator();
		~ResourceCreator();
	
		void Initialize(platform::crossplatform::RenderPlatform *r, clientrender::VertexBufferLayout::PackingStyle packingStyle);
		/// Full reset, when the server has changed.
		void Clear();

		//Updates any processes that need to happen on a regular basis; should be called at least once per second.
		//	deltaTime : Milliseconds that has passed since the last call to Update();
		void Update(float deltaTime);

		void SetGeometryCache(clientrender::GeometryCache * c)
		{
			geometryCache = c;
		}

		// Inherited via GeometryTargetBackendInterface
		avs::Result CreateMesh(avs::MeshCreate& meshCreate) override;

		void CreateTexture(avs::uid id, const avs::Texture& texture) override;
		void CreateMaterial(avs::uid id, const avs::Material& material) override;
		void CreateNode(avs::uid id, avs::Node& node) override;
		void CreateFontAtlas(avs::uid id,teleport::core::FontAtlas &fontAtlas);
		void CreateTextCanvas(clientrender::TextCanvasCreateInfo &textCanvasCreateInfo);

		void CreateSkin(avs::uid id, avs::Skin& skin) override;
		void CreateAnimation(avs::uid id, avs::Animation& animation) override;

		std::shared_ptr<clientrender::Texture> m_DummyWhite;
		std::shared_ptr<clientrender::Texture> m_DummyNormal;
		std::shared_ptr<clientrender::Texture> m_DummyCombined;
		std::shared_ptr<clientrender::Texture> m_DummyBlack;
		std::shared_ptr<clientrender::Texture> m_DummyGreen;

	private:
	
		void CreateMeshNode(avs::uid id, avs::Node& node);
		void CreateLight(avs::uid id, avs::Node& node);
		void CreateBone(avs::uid id, avs::Node& node);

		void CompleteMesh(avs::uid id, const clientrender::Mesh::MeshCreateInfo& meshInfo);
		void CompleteSkin(avs::uid id, std::shared_ptr<IncompleteSkin> completeSkin);
		void CompleteTexture(avs::uid id, const clientrender::Texture::TextureCreateInfo& textureInfo);
		void CompleteMaterial(avs::uid id, const clientrender::Material::MaterialCreateInfo& materialInfo);
		void CompleteNode(avs::uid id, std::shared_ptr<clientrender::Node> node);
		void CompleteBone(avs::uid id, std::shared_ptr<clientrender::Bone> bone);
		void CompleteAnimation(avs::uid id, std::shared_ptr<clientrender::Animation> animation);

		//Add texture to material being created.
		//	accessor : Data on texture that was received from server.
		//	colourFactor : Vector factor to multiply texture with to adjust strength.
		//	dummyTexture : Texture to use if there is no texture ID assigned.
		//	incompleteMaterial : IncompleteMaterial we are attempting to add the texture to.
		//	materialParameter : Parameter we are modifying.
		void AddTextureToMaterial(const avs::TextureAccessor& accessor,
								  const vec4& colourFactor,
								  const std::shared_ptr<clientrender::Texture>& dummyTexture,
								  std::shared_ptr<IncompleteMaterial> incompleteMaterial,
								  clientrender::Material::MaterialParameter& materialParameter);

		void BasisThread_TranscodeTextures();

		platform::crossplatform::RenderPlatform* renderPlatform = nullptr;
		clientrender::VertexBufferLayout::PackingStyle m_PackingStyle = clientrender::VertexBufferLayout::PackingStyle::GROUPED;

		//basist::etc1_global_selector_codebook basis_codeBook;
	#ifdef _MSC_VER
		basist::transcoder_texture_format basis_transcoder_textureFormat =basist::transcoder_texture_format::cTFBC3;
	#else
		basist::transcoder_texture_format basis_transcoder_textureFormat =basist::transcoder_texture_format::cTFETC2;
	#endif

		std::vector<UntranscodedTexture> texturesToTranscode;
		std::mutex mutex_texturesToTranscode;
		std::atomic_bool shouldBeTranscoding = true;	//Whether the basis thread should be running, and transcoding textures. Settings this to false causes the thread to end.
		std::thread basisThread;						//Thread where we transcode basis files to mip data.
	
		const uint32_t whiteBGRA = 0xFFFFFFFF;
	
		const uint32_t normalRGBA = 0xFFFF7F7F;
		const uint32_t combinedBGRA = 0xFFFFFFFF;
		const uint32_t blackBGRA = 0x0;
		const uint32_t greenBGRA = 0xFF337733;
	
		clientrender::GeometryCache* geometryCache = nullptr;
	};


}