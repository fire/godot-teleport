#pragma once

#include "libavstream/common.hpp"
#include "ClientRender/NodeManager.h"

class OVRNodeManager : public clientrender::NodeManager
{
public:
	virtual ~OVRNodeManager() = default;

	virtual std::shared_ptr<clientrender::Node> CreateNode(avs::uid id, const avs::Node &avsNode) override;

	//Changes PBR effect used on nodes/surfaces to the effect pass with the passed name.
	//Also changes GlobalGraphicsResource::effectPassName.
	void ChangeEffectPass(const char* effectPassName);
};
