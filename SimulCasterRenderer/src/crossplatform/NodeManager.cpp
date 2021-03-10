#include "NodeManager.h"

namespace scr
{
	std::shared_ptr<Node> NodeManager::CreateNode(avs::uid id, const std::string& name) const
	{
		return std::make_shared<Node>(id, name);
	}

	void NodeManager::AddNode(std::shared_ptr<Node> node, const avs::DataNode& nodeData)
	{
		//Remove any node already using the ID.
		RemoveNode(node->id);

		if(nodeData.data_subtype == avs::NodeDataSubtype::None)
		{
			rootNodes.push_back(node);
		}
		nodeLookup[node->id] = node;

		//Update movement based on movement data that was received before the node was complete.
		auto movementIt = earlyMovements.find(node->id);
		if(movementIt != earlyMovements.end()) node->SetLastMovement(movementIt->second);

		//Link new node to parent.
		LinkToParentNode(node->id);

		//Link node's children to this node.
		for(avs::uid childID : node->GetChildrenIDs())
		{
			parentLookup[childID] = node->id;
			LinkToParentNode(childID);
		}

		switch(nodeData.data_subtype)
		{
		case avs::NodeDataSubtype::None:
			break;
		case avs::NodeDataSubtype::Body:
			SetBody(node);
			break;
		case avs::NodeDataSubtype::LeftHand:
			SetLeftHand(node);
			break;
		case avs::NodeDataSubtype::RightHand:
			SetRightHand(node);
			break;
		default:
			SCR_CERR << "Unrecognised node data sub-type: " << static_cast<int>(nodeData.data_subtype) << "!\n";
			break;
		}
	}

	void NodeManager::RemoveNode(std::shared_ptr<Node> node)
	{
		//Remove node from parent's child list.
		std::shared_ptr<Node> parent = node->GetParent().lock();
		if(parent)
		{
			parent->RemoveChild(node);
		}
		//Remove from root nodes, if the node had no parent.
		else
		{
			rootNodes.erase(std::find(rootNodes.begin(), rootNodes.end(), node));
		}

		//Attach children to world root.
		std::vector<std::weak_ptr<Node>> children = node->GetChildren();
		for (std::weak_ptr<Node> childPtr : children)
		{
			std::shared_ptr<Node> child = childPtr.lock();
			if (child)
			{
				rootNodes.push_back(child);

				//Remove parent
				child->SetParent(nullptr);
				parentLookup.erase(child->id);
			}
		}

		//Remove from node lookup table.
		nodeLookup.erase(node->id);
	}

	void NodeManager::RemoveNode(avs::uid nodeID)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if (nodeIt != nodeLookup.end())
		{
			RemoveNode(nodeIt->second);
		}
	}

	bool NodeManager::HasNode(avs::uid nodeID) const
	{
		return nodeLookup.find(nodeID) != nodeLookup.end();
	}

	std::shared_ptr<Node> NodeManager::GetNode(avs::uid nodeID)
	{
		return HasNode(nodeID) ? nodeLookup.at(nodeID) : nullptr;
	}

	size_t NodeManager::GetNodeAmount()
	{
		return nodeLookup.size();
	}

	const NodeManager::nodeList_t& NodeManager::GetRootNodes() const
	{
		return rootNodes;
	}

	void NodeManager::SetBody(std::shared_ptr<Node> node)
	{
		body = node;
	}

	bool NodeManager::SetBody(avs::uid nodeID)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if(nodeIt != nodeLookup.end())
		{
			SetBody(nodeIt->second);
			return true;
		}

		return false;
	}

	std::shared_ptr<Node> NodeManager::GetBody()
	{
		return body;
	}

	void NodeManager::SetLeftHand(std::shared_ptr<Node> node)
	{
		leftHand = node;
	}

	bool NodeManager::SetLeftHand(avs::uid nodeID)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if(nodeIt != nodeLookup.end())
		{
			SetLeftHand(nodeIt->second);
			return true;
		}

		return false;
	}

	std::shared_ptr<Node> NodeManager::GetLeftHand()
	{
		return leftHand;
	}

	void NodeManager::SetRightHand(std::shared_ptr<Node> node)
	{
		rightHand = node;
	}

	bool NodeManager::SetRightHand(avs::uid nodeID)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if(nodeIt != nodeLookup.end())
		{
			SetRightHand(nodeIt->second);
			return true;
		}

		return false;
	}

	std::shared_ptr<Node> NodeManager::GetRightHand()
	{
		return rightHand;
	}

	bool NodeManager::ShowNode(avs::uid nodeID)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if (nodeIt != nodeLookup.end())
		{
			nodeIt->second->SetVisible(true);
			return true;
		}

		return false;
	}

	bool NodeManager::HideNode(avs::uid nodeID)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if (nodeIt != nodeLookup.end())
		{
			nodeIt->second->SetVisible(false);
			return true;
		}

		return false;
	}

	void NodeManager::SetVisibleNodes(const std::vector<avs::uid> visibleNodes)
	{
		//Hide all nodes.
		for (auto it : nodeLookup)
		{
			it.second->SetVisible(false);
		}

		//Show visible nodes.
		for (avs::uid id : visibleNodes)
		{
			ShowNode(id);
		}
	}

	bool NodeManager::UpdateNodeTransform(avs::uid nodeID, const avs::vec3& translation, const quat& rotation, const avs::vec3& scale)
	{
		auto nodeIt = nodeLookup.find(nodeID);
		if (nodeIt != nodeLookup.end())
		{
			nodeIt->second->UpdateModelMatrix(translation, rotation, scale);
			return true;
		}

		return false;
	}

	void NodeManager::UpdateNodeMovement(std::vector<avs::MovementUpdate> updateList)
	{
		earlyMovements.clear();

		for (avs::MovementUpdate update : updateList)
		{
			auto nodeIt = nodeLookup.find(update.nodeID);
			if (nodeIt != nodeLookup.end())
			{
				nodeIt->second->SetLastMovement(update);
			}
			else
			{
				earlyMovements[update.nodeID] = update;
			}
		}
	}

	void NodeManager::Update(float deltaTime)
	{
		nodeList_t expiredNodes;
		for (auto node : rootNodes)
		{
			node->Update(deltaTime);

			if (node->GetTimeSinceLastVisible() >= nodeLifetime)
			{
				expiredNodes.push_back(node);
			}
		}

		//Delete nodes that have been invisible for too long.
		for (auto node : expiredNodes)
		{
			RemoveNode(node);
		}

		if(body) body->Update(deltaTime);
		if(leftHand) leftHand->Update(deltaTime);
		if(rightHand) rightHand->Update(deltaTime);
	}

	void NodeManager::Clear()
	{
		rootNodes.clear();
		nodeLookup.clear();

		body = nullptr;
		leftHand = nullptr;
		rightHand = nullptr;

		parentLookup.clear();
		earlyMovements.clear();
	}

	void NodeManager::ClearCareful(std::vector<uid>& excludeList, std::vector<uid>& outExistingNodes)
	{
		for (auto it = nodeLookup.begin(); it != nodeLookup.end();)
		{
			auto exclusionIt = std::find(excludeList.begin(), excludeList.end(), it->first);

			//Keep node in manager, if it is in the exclusion list.
			if (exclusionIt != excludeList.end())
			{
				excludeList.erase(exclusionIt);
				outExistingNodes.push_back(it->first);
				++it;
			}
			else
			{
				RemoveNode(it->second);
			}
		}
	}

	bool NodeManager::IsNodeVisible(avs::uid nodeID) const
	{
		return HasNode(nodeID) && nodeLookup.at(nodeID)->IsVisible();
	}

	void NodeManager::LinkToParentNode(avs::uid childID)
	{
		auto parentIt = parentLookup.find(childID);
		if (parentIt == parentLookup.end())
			return;

		std::shared_ptr<Node> parent = GetNode(parentIt->second);
		std::shared_ptr<Node> child = GetNode(childID);

		if (parent == nullptr || child == nullptr)
			return;

		child->SetParent(parent);
		parent->AddChild(child);
		auto lk= nodeLookup.find(childID);
		if(lk!=nodeLookup.end())
		{
			auto rn= std::find(rootNodes.begin(), rootNodes.end(), lk->second);
			if(rn!=rootNodes.end())
				rootNodes.erase(rn);
		}
	}
}
