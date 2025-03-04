#include "GeometryCache.h"

using namespace clientrender;

GeometryCache::GeometryCache(clientrender::NodeManager *nm)
		: mNodeManager(nm),
		  mTextureManager(&clientrender::Texture::Destroy),
		  mIndexBufferManager(&clientrender::IndexBuffer::Destroy),
		  mVertexBufferManager(&clientrender::VertexBuffer::Destroy)
{
}

GeometryCache::~GeometryCache()
{

}

clientrender::MissingResource* GeometryCache::GetMissingResourceIfMissing(avs::uid id, avs::GeometryPayloadType resourceType)
{
	auto missingPair = m_MissingResources.find(id);
	if (missingPair == m_MissingResources.end())
	{
		return nullptr;
	}
	return &missingPair->second;
}

clientrender::MissingResource& GeometryCache::GetMissingResource(avs::uid id, avs::GeometryPayloadType resourceType)
{
	auto missingPair = m_MissingResources.find(id);
	if (missingPair == m_MissingResources.end())
	{
		missingPair = m_MissingResources.emplace(id, MissingResource(id, resourceType)).first;
		m_ResourceRequests.push_back(id);
	}
	if(resourceType!=missingPair->second.resourceType)
	{
		TELEPORT_CERR<<"Resource type mismatch"<<std::endl;
	}
	return missingPair->second;
}

std::vector<avs::uid> GeometryCache::GetResourceRequests() const
{
	std::vector<avs::uid> resourceRequests = m_ResourceRequests;
	//Remove duplicates.
	std::sort(resourceRequests.begin(), resourceRequests.end());
	resourceRequests.erase(std::unique(resourceRequests.begin(), resourceRequests.end()), resourceRequests.end());

	return resourceRequests;
}

void GeometryCache::ClearResourceRequests()
{
	m_ResourceRequests.clear();
}

void GeometryCache::ReceivedResource(avs::uid id)
{
	m_ReceivedResources.push_back(id);
	if(id==64)
	{
		TELEPORT_COUT<<"Received Resource "<<(long long)id<<std::endl;
	}
}

std::vector<avs::uid> GeometryCache::GetReceivedResources() const
{
	return m_ReceivedResources;
}

void GeometryCache::ClearReceivedResources()
{
	m_ReceivedResources.clear();
}

std::vector<avs::uid> GeometryCache::GetCompletedNodes() const
{
	return m_CompletedNodes;
}

void GeometryCache::ClearCompletedNodes()
{
	m_CompletedNodes.clear();
}
#include "Platform/Core/FileLoader.h"


void GeometryCache::setCacheFolder(const std::string &f)
{
	cacheFolder=f;
}

template<typename T> void put(std::vector<uint8_t> &buffer, T t)
{
	size_t sz=buffer.size();
	buffer.resize(sz+sizeof(T));
	T* bt=(T*)(buffer.data()+sz);
	*bt=t;
}

void SaveNodeTree(const std::weak_ptr<clientrender::Node>& n,std::vector<uint8_t> &buffer) 
{
	auto N=n.lock();
	put(buffer,N->GetChildren().size());
	auto T=N->GetLocalTransform();
	put(buffer,T.m_Rotation);
	put(buffer,T.m_Translation);
	put(buffer,T.m_Scale);
	for(size_t i=0;i<N->GetChildren().size();i++)
	{
		SaveNodeTree(N->GetChildren()[i],buffer);
	}
}

void GeometryCache::SaveNodeTree(const std::shared_ptr<clientrender::Node>& n) const
{
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	std::string filename=n->name+".node";
	std::string f=cacheFolder.length()?(cacheFolder+"/")+filename:filename;
	std::vector<uint8_t> buffer;
	::SaveNodeTree(n,buffer);
	fileLoader->Save((const void*)buffer.data(),(unsigned)buffer.size(),f.c_str(),false);
}