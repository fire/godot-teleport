#pragma once

#include <ctime>
#include <unordered_map>
#include <vector>

#include "basisu_comp.h"
#include "libavstream/geometry/mesh_interface.hpp"

#include "ExtractedTypes.h"
struct InteropTextCanvas;

namespace teleport
{
	namespace core
	{
		struct TextCanvas;
	}
	namespace server
	{
		//! Singleton for storing geometry data and managing the geometry file cache.
		class GeometryStore
		{
		public:
			GeometryStore();
			~GeometryStore();

			static GeometryStore& GetInstance();
			bool willDelayTextureCompression = true; //Causes textures to wait for compression in StoreTexture, rather than calling compress them during the function call, when true.

			//Checks and sets the global cache path for the project. Returns true if path is valid.
			bool SetCachePath(const char* path);
			void verify();
			bool saveToDisk() const;
			//Load from disk.
			//Parameters are used to return the meta data of the resources that were loaded back-in, so they can be confirmed.
			void loadFromDisk(size_t& meshAmount, LoadedResource*& loadedMeshes, size_t& textureAmount, LoadedResource*& loadedTextures, size_t& materialAmount, LoadedResource*& loadedMaterials);

			void clear(bool freeMeshBuffers);

			void setCompressionLevels(uint8_t compressionStrength, uint8_t compressionQuality);

			const char* getNodeName(avs::uid nodeID) const;

			std::vector<avs::uid> getNodeIDs() const;
			avs::Node* getNode(avs::uid nodeID);
			const avs::Node* getNode(avs::uid nodeID) const;
			const std::map<avs::uid, avs::Node>& getNodes() const;

			avs::Skin* getSkin(avs::uid skinID, avs::AxesStandard standard);
			const avs::Skin* getSkin(avs::uid skinID, avs::AxesStandard standard) const;

			avs::Animation* getAnimation(avs::uid id, avs::AxesStandard standard);
			const avs::Animation* getAnimation(avs::uid id, avs::AxesStandard standard) const;

			std::vector<avs::uid> getMeshIDs() const;

			const ExtractedMesh* getExtractedMesh(avs::uid meshID, avs::AxesStandard standard) const;

			const avs::CompressedMesh* getCompressedMesh(avs::uid meshID, avs::AxesStandard standard) const;
			virtual avs::Mesh* getMesh(avs::uid meshID, avs::AxesStandard standard);
			virtual const avs::Mesh* getMesh(avs::uid meshID, avs::AxesStandard standard) const;

			virtual std::vector<avs::uid> getTextureIDs() const;
			virtual avs::Texture* getTexture(avs::uid textureID);
			virtual const avs::Texture* getTexture(avs::uid textureID) const;

			virtual std::vector<avs::uid> getMaterialIDs() const;
			virtual avs::Material* getMaterial(avs::uid materialID);
			virtual const avs::Material* getMaterial(avs::uid materialID) const;

			virtual std::vector<avs::uid> getShadowMapIDs() const;
			virtual avs::Texture* getShadowMap(avs::uid shadowID);
			virtual const avs::Texture* getShadowMap(avs::uid shadowID) const;

			const core::TextCanvas* getTextCanvas(avs::uid u) const;
			const core::FontAtlas* getFontAtlas(avs::uid u) const;

			//Returns a list of all light nodes that need to be streamed to the client.
			const std::map<avs::uid, avs::LightNodeResources>& getLightNodes() const;

			//Returns whether there is a node stored with the passed id.
			bool hasNode(avs::uid id) const;
			//Returns whether there is a mesh stored with the passed id.
			bool hasMesh(avs::uid id) const;
			//Returns whether there is a material stored with the passed id.
			bool hasMaterial(avs::uid id) const;
			//Returns whether there is a texture stored with the passed id.
			bool hasTexture(avs::uid id) const;
			//Returns whether there is a shadow map stored with the passed id.
			bool hasShadowMap(avs::uid id) const;

			void storeNode(avs::uid id, avs::Node& newNode);
			void storeSkin(avs::uid id, avs::Skin& newSkin, avs::AxesStandard sourceStandard);
			void storeAnimation(avs::uid id, avs::Animation& animation, avs::AxesStandard sourceStandard);
			void storeMesh(avs::uid id, std::string guid, std::string path, std::time_t lastModified, avs::Mesh& newMesh, avs::AxesStandard standard, bool compress = false, bool verify = false);
			void storeMaterial(avs::uid id, std::string guid, std::string path, std::time_t lastModified, avs::Material& newMaterial);
			void storeTexture(avs::uid id, std::string guid, std::string path, std::time_t lastModified, avs::Texture& newTexture, std::string basisFileLocation, bool genMips, bool highQualityUASTC, bool forceOverwrite);
			avs::uid storeFont(std::string ttf_path_utf8, std::string relative_asset_path_utf8, std::time_t lastModified, int size = 32);
			avs::uid storeTextCanvas(std::string relative_asset_path, const InteropTextCanvas* interopTextCanvas);
			void storeShadowMap(avs::uid id, std::string guid, std::string path, std::time_t lastModified, avs::Texture& shadowMap);

			void removeNode(avs::uid id);

			void updateNodeTransform(avs::uid id, avs::Transform& newLTransform, avs::Transform& newGTransform);

			//Returns amount of textures waiting to be compressed.
			size_t getNumberOfTexturesWaitingForCompression() const;
			//Returns the texture that will be compressed next.
			const avs::Texture* getNextTextureToCompress() const;
			//Compresses the next texture to be compressed; does nothing if there are no more textures to compress.
			void compressNextTexture();

			/// Debug: check for clashing uid's: this should never return a non-empty set.
			std::set<avs::uid> GetClashingUids() const;
			/// Check for errors - these should be resolved before using this store in a server.
			bool CheckForErrors() const;
			//! Get or generate a uid. If the path already corresponds to an id, that will be returned. Otherwise a new one will be added.
			avs::uid GetOrGenerateUid(const std::string& path);
			//! Get the current session uid corresponding to the given resource/asset path.
			avs::uid PathToUid(std::string p) const;
			//! Get the resource/asset path corresponding to the current session uid.
			std::string UidToPath(avs::uid u) const;

		private:
			std::string cachePath;
			//Stores data on a texture that is to be compressed.
			struct PrecompressedTexture
			{
				std::string basisFilePath;

				std::vector<std::vector<uint8_t>> images;

				size_t numMips;
				bool genMips;	// if false, numMips tells how many are in the data already.
				bool highQualityUASTC;
				avs::TextureCompression textureCompression = avs::TextureCompression::UNCOMPRESSED;
			};

			uint8_t compressionStrength = 1;
			uint8_t compressionQuality = 1;

			// Mutable, non-resource assets.
			std::map<avs::uid, avs::Node> nodes;
			std::map<avs::uid, core::TextCanvas> textCanvases;

			// Static, resource assets.
			std::map<avs::AxesStandard, std::map<avs::uid, avs::Skin>> skins;
			std::map<avs::AxesStandard, std::map<avs::uid, avs::Animation>> animations;
			std::map<avs::AxesStandard, std::map<avs::uid, ExtractedMesh>> meshes;
			std::map<avs::uid, ExtractedMaterial> materials;
			std::map<avs::uid, ExtractedTexture> textures;
			std::map<avs::uid, ExtractedTexture> shadowMaps;
			std::map<avs::uid, ExtractedFontAtlas> fontAtlases;

			std::map<avs::uid, PrecompressedTexture> texturesToCompress; //Map of textures that need compressing. <ID of the texture; file path to store the basis file>

			std::map<avs::uid, avs::LightNodeResources> lightNodes; //List of ALL light nodes; prevents having to search for them every geometry tick.

			template<typename ExtractedResource>
			bool saveResourceBinary(const std::string file_name, const ExtractedResource& resource) const;
			template<typename ExtractedResource>
			bool loadResourceBinary(const std::string file_name, const std::string& path_root, ExtractedResource& esource);

			template<typename ExtractedResource>
			bool saveResource(const std::string file_name, const ExtractedResource& resource) const;
			template<typename ExtractedResource>
			avs::uid loadResource(const std::string file_name, const std::string& path_root, std::map<avs::uid, ExtractedResource>& resourceMap);

			template<typename ExtractedResource>
			bool saveResources(const std::string file_name, const std::map<avs::uid, ExtractedResource>& resourceMap) const;

			template<typename ExtractedResource>
			void loadResources(const std::string file_name, std::map<avs::uid, ExtractedResource>& resourceMap);


			std::map<avs::uid, std::string> uid_to_path;
			std::map<std::string, avs::uid> path_to_uid;
		};
	}
}