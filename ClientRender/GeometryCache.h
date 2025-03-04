// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#include <libavstream/geometry/mesh_interface.hpp>
#include <libavstream/mesh.hpp>

#include "NodeManager.h"
#include "ResourceManager.h"
#include "TeleportCore/FontAtlas.h"

namespace clientrender
{
	class Animation;
	class Material;
	class Light;
}

namespace clientrender
{
	typedef unsigned long long geometry_cache_uid;
	
	struct IncompleteMaterial : IncompleteResource
	{
		IncompleteMaterial(avs::uid id, avs::GeometryPayloadType type)
			:IncompleteResource(id, type)
		{}

		clientrender::Material::MaterialCreateInfo materialInfo;
		std::set<avs::uid> missingTextureUids; //<ID of the texture, slot the texture should be placed into>.
	};
	struct UntranscodedTexture
	{
		avs::uid													texture_uid			= 0;
		std::vector<uint8_t>										data				= {};										//The raw data of the basis file.
		std::shared_ptr<clientrender::Texture::TextureCreateInfo>	textureCI			= nullptr;									//Creation information on texture being transcoded.
		std::string													name				= {};										//For debugging which texture failed.
		avs::TextureCompression										compressionFormat	= avs::TextureCompression::UNCOMPRESSED;
		float														valueScale			= 0.0f;										// scale on transcode.

		UntranscodedTexture(avs::uid uid, const void* ptr, size_t size, const std::shared_ptr<clientrender::Texture::TextureCreateInfo>& textureCreateInfo,
			const std::string& name, avs::TextureCompression compressionFormat, float valueScale)
			: texture_uid(uid), data(size), textureCI(textureCreateInfo), name(name), compressionFormat(compressionFormat), valueScale(valueScale)
		{
			memcpy(data.data(), ptr, size);
		}
	};

	//! A container for geometry sent from servers and cached locally.
	//! There is one instance of GeometryCache for each connected server, and a local GeometryCache for the client's own objects.
	class GeometryCache : public avs::GeometryCacheBackendInterface
	{
		geometry_cache_uid next_geometry_cache_uid=1;
		std::map<uint64_t,geometry_cache_uid> uid_mapping;
	public:
		GeometryCache(NodeManager *);

		~GeometryCache();
		/// Generate a new uid unique in this cache (not globally unique).
		geometry_cache_uid GenerateUid()
		{
			geometry_cache_uid u=next_geometry_cache_uid;
			next_geometry_cache_uid++;
			return u;
		}
		/// Generate a new uid unique in this cache and store a mapping from the given input number.
		geometry_cache_uid GenerateUid(uint64_t from)
		{
			geometry_cache_uid u=next_geometry_cache_uid;
			uid_mapping[from]=u;
			next_geometry_cache_uid++;
			return u;
		}
		/// Get the locally unique id that the specified number maps to.
		geometry_cache_uid GetCorrespondingUid(uint64_t from)
		{
			return uid_mapping[from];
		}
		//Clear any resources that have not been used longer than their expiry time.
		//	timeElapsed_s : Delta time in seconds.
		void Update(float timeElapsed_s)
		{
			mNodeManager->Update(timeElapsed_s);
			mIndexBufferManager.Update(timeElapsed_s);
			mMaterialManager.Update(timeElapsed_s);
			mTextureManager.Update(timeElapsed_s);
			mVertexBufferManager.Update(timeElapsed_s);
			mMeshManager.Update(timeElapsed_s);
			mSkinManager.Update(timeElapsed_s);
			//mLightManager.Update(timeElapsed_s);
			mBoneManager.Update(timeElapsed_s);
			mAnimationManager.Update(timeElapsed_s);
			mTextCanvasManager.Update(timeElapsed_s);
			mFontAtlasManager.Update(timeElapsed_s);
		}
		void setCacheFolder(const std::string &f);
		void SaveNodeTree(const std::shared_ptr<clientrender::Node>& n) const;

		std::vector<uid> GetAllResourceIDs()
		{
			std::vector<uid> resourceIDs;

			const auto& m = mMaterialManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), m.begin(), m.end());
			const auto& t = mTextureManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), t.begin(), t.end());
			const auto& h = mMeshManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), h.begin(), h.end());
			const auto& s = mSkinManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), s.begin(), s.end());
			const auto& l = mLightManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), l.begin(), l.end());
			const auto& b = mBoneManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), b.begin(), b.end());
			const auto& a = mAnimationManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), a.begin(), a.end());
			
			const auto& c = mTextCanvasManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), c.begin(), c.end());
			const auto& f = mFontAtlasManager.GetAllIDs();
			resourceIDs.insert(resourceIDs.end(), f.begin(), f.end());
			return resourceIDs;

			/*
				//We will resend the nodes/objects to update the transform data, as changes in client position (and thus the new invisible nodes) aren't stored for the reconnect.
				mNodeManager;

				//These IDs aren't stored on the server currently, and thus are ignored.
				mIndexBufferManager.GetAllIDs();
				mShaderManager.GetAllIDs();
				mVertexBufferManager.GetAllIDs();
			*/
		}

		//Clear all resources.
		void ClearAll()
		{
			mNodeManager->Clear();

			mIndexBufferManager.Clear();
			mMaterialManager.Clear();
			mTextureManager.Clear();
			mVertexBufferManager.Clear();
			mMeshManager.Clear();
			mSkinManager.Clear();
			mLightManager.Clear();
			mBoneManager.Clear();
			mAnimationManager.Clear();
			mTextCanvasManager.Clear();
			mFontAtlasManager.Clear();
		}
		
		MissingResource& GetMissingResource(avs::uid id, avs::GeometryPayloadType resourceType);
		MissingResource* GetMissingResourceIfMissing(avs::uid id, avs::GeometryPayloadType resourceType);
		//Returns the resources the ResourceCreator needs, and clears the list.
		std::vector<avs::uid> GetResourceRequests() const override;
		void ClearResourceRequests() override;
		void ReceivedResource(avs::uid id);
		//Returns a list of resource IDs corresponding to the resources the client has received, and clears the list.
		std::vector<avs::uid> GetReceivedResources() const override;
		void ClearReceivedResources() override;
		//Returns the nodes that have been finished since the call, and clears the list.
		std::vector<avs::uid> GetCompletedNodes() const override;
		void ClearCompletedNodes() override;

		std::unique_ptr<clientrender::NodeManager>				mNodeManager;
		ResourceManager<avs::uid,clientrender::Material>		mMaterialManager;
		ResourceManager<avs::uid,clientrender::Texture>			mTextureManager;
		ResourceManager<avs::uid,clientrender::Mesh>			mMeshManager;
		ResourceManager<avs::uid,clientrender::Skin>			mSkinManager;
		ResourceManager<avs::uid,clientrender::Light>			mLightManager;
		ResourceManager<uint64_t,clientrender::Bone>			mBoneManager;
		ResourceManager<avs::uid,clientrender::Animation>		mAnimationManager;
		
		ResourceManager<avs::uid,clientrender::TextCanvas>		mTextCanvasManager;
		ResourceManager<avs::uid,clientrender::FontAtlas>			mFontAtlasManager;
		// Buffers used in meshes do not have server-unique id's, their id's are generated clientside.
		ResourceManager<geometry_cache_uid,clientrender::IndexBuffer>	mIndexBufferManager;
		ResourceManager<geometry_cache_uid,clientrender::VertexBuffer>	mVertexBufferManager;

		std::vector<avs::uid> m_CompletedNodes; //List of IDs of nodes that have been fully received, and have yet to be confirmed to the server.
		std::unordered_map<avs::uid, MissingResource> m_MissingResources; //<ID of Missing Resource, Missing Resource Info>

		const std::vector<avs::uid> &GetResourceRequests();
	protected:
		std::vector<avs::uid> m_ResourceRequests; //Resources the client will request from the server.
		std::vector<avs::uid> m_ReceivedResources; //Resources received.
		std::string cacheFolder;
	};
}
