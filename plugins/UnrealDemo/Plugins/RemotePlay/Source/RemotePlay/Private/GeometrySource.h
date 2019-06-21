#pragma once
#include <libavstream/geometry/mesh_interface.hpp>

/*! The Geometry Source keeps all the geometry ready for streaming, and returns geometry
	data in glTF-style when asked for.

	It handles geometry for multiple clients, so each client will only want a subset.
*/
class GeometrySource : public avs::GeometrySourceBackendInterface
{
public:
	GeometrySource();
	~GeometrySource();
	avs::uid AddMesh(UStaticMesh *StaticMesh);
	avs::uid AddStreamableActor(class UStreamableGeometryComponent *StreamableGeometryComponent);

	void Tick();

	// Inherited via GeometrySourceBackendInterface
	virtual size_t getNodeCount() const override;
	virtual avs::uid getNodeUid(size_t index) const override;

	virtual size_t getMeshCount() const override;
	virtual avs::uid getMeshUid(size_t index) const override;
	virtual size_t getMeshPrimitiveArrayCount(avs::uid mesh_uid) const override;
	virtual bool getMeshPrimitiveArray(avs::uid mesh_uid, size_t array_index, avs::PrimitiveArray & primitiveArray) const override;
	virtual bool getAccessor(avs::uid accessor_uid, avs::Accessor & accessor) const override;
	virtual bool getBufferView(avs::uid buffer_view_uid, avs::BufferView & bufferView) const override;
	virtual bool getBuffer(avs::uid buffer_uid, avs::GeometryBuffer & buffer) const override;
protected:
	struct Mesh;
	TArray<UStreamableGeometryComponent*> ToAdd;
	struct GeometryInstance
	{
		class UStreamableGeometryComponent* Geometry;
		//unsigned long long SentFrame;
	};
	TMap<avs::uid, TSharedPtr<Mesh>> Meshes;
	TMap<avs::uid, TSharedPtr<GeometryInstance> > GeometryInstances;
	void PrepareMesh(Mesh &m);
	void SendMesh(Mesh &m);
	bool InitMesh(Mesh *mesh, class FStaticMeshLODResources &lod);
};