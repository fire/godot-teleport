#pragma once

#include <cstdint>
#include <iostream>

#include "common.hpp"
#include "common_maths.h"

namespace avs
{
	#pragma pack(push)
	#pragma pack(1)
	enum class RemotePlaySessionChannel : unsigned char //enet_uint8
	{
		RPCH_Handshake = 0,
		RPCH_Control = 1,
		RPCH_DisplayInfo = 2,
		RPCH_HeadPose = 3,
		RPCH_ResourceRequest = 4,
		RPCH_KeyframeRequest = 5,
		RPCH_ClientMessage = 6,
		RPCH_Origin = 7,
		RPCH_NumChannels
	};

	enum class ControlModel : uint32_t
	{
		NONE,
		CLIENT_ORIGIN_SERVER_GRAVITY,
		SERVER_ORIGIN_CLIENT_LOCAL
	};

	enum class NodeStatus : uint8_t
	{
		Unknown = 0,
		Drawn,
		WantToRelease,
		Released
	};

	enum class CommandPayloadType : uint8_t
	{
		Invalid,
		Shutdown,
		Setup,
		NodeBounds,
		AcknowledgeHandshake,
		SetPosition,
		UpdateNodeMovement,
		UpdateNodeAnimation,
		ReconfigureVideo
	};

	enum class ClientMessagePayloadType : uint8_t
	{
		Invalid,
		NodeStatus,
		ReceivedResources,
		ControllerPoses,
		OriginPose
	};

	struct ServiceDiscoveryResponse
	{
		uint64_t clientID;
		uint16_t remotePort;
	};

	struct Handshake
	{
		DisplayInfo startDisplayInfo = DisplayInfo();
		float MetresPerUnit = 1.0f;
		float FOV = 90.0f;
		uint32_t udpBufferSize = 0;			// In kilobytes.
		uint32_t maxBandwidthKpS = 0;			// In kilobytes per second
		AxesStandard axesStandard = AxesStandard::NotInitialized;
		uint8_t framerate = 0;				// In hertz
		bool usingHands = false; //Whether to send the hand nodes to the client.
		bool isVR = true;
		uint64_t resourceCount = 0; //Amount of resources the client has, and are appended to the handshake.
		uint32_t maxLightsSupported = 0;
		uint32_t clientStreamingPort = 0; // the local port on the client to receive the stream.
	};

	struct InputState
	{
		uint32_t controllerId = 0;
		uint32_t buttonsDown = 0;		// arbitrary bitfield.
		float trackpadAxisX = 0.0f;
		float trackpadAxisY = 0.0f;
		float joystickAxisX = 0.0f;
		float joystickAxisY = 0.0f;

		uint32_t binaryEventAmount = 0;
		uint32_t analogueEventAmount = 0;
		uint32_t motionEventAmount = 0;
	};

	//Contains information to update the transform of a node.
	struct MovementUpdate
	{
		int64_t timestamp = 0;
		bool isGlobal = true;

		uid nodeID = 0;
		vec3 position;
		vec4 rotation;
		vec3 scale;

		vec3 velocity;
		vec3 angularVelocityAxis;
		float angularVelocityAngle = 0.0f;
	};

	//TODO: Use instead of MovementUpdate for bandwidth.
	struct NodeUpdatePosition
	{
		int64_t timestamp = 0;
		bool isGlobal = true;

		uid nodeID = 0;
		vec3 position;
		vec3 velocity;
	};

	//TODO: Use instead of MovementUpdate for bandwidth.
	struct NodeUpdateRotation
	{
		int64_t timestamp = 0;
		bool isGlobal = true;

		uid nodeID = 0;
		vec4 rotation;
		vec3 angularVelocityAxis;
		float angularVelocityAngle = 0.0f;
	};

	//TODO: Use instead of MovementUpdate for bandwidth.
	struct NodeUpdateScale
	{
		int64_t timestamp = 0;
		bool isGlobal = true;

		uid nodeID = 0;
		vec3 scale;
		vec3 velocity;
	};

	struct NodeUpdateAnimation
	{
		int64_t timestamp = 0; //When the animation change was detected.

		uid nodeID = 0; //ID of the node the animation is playing on.
		uid animationID = 0; //ID of the animation that is now playing.
	};

	struct Command
	{
		CommandPayloadType commandPayloadType;

		Command(CommandPayloadType t) : commandPayloadType(t) {}

		//Returns byte size of command.
		virtual size_t getCommandSize() const = 0;
	};

	struct AcknowledgeHandshakeCommand : public Command
	{
		AcknowledgeHandshakeCommand() : Command(CommandPayloadType::AcknowledgeHandshake) {}
		AcknowledgeHandshakeCommand(size_t visibleNodeAmount) : Command(CommandPayloadType::AcknowledgeHandshake), visibleNodeAmount(visibleNodeAmount) {}
		
		virtual size_t getCommandSize() const override
		{
			return sizeof(AcknowledgeHandshakeCommand);
		}

		size_t visibleNodeAmount = 0; //Amount of visible node IDs appended to the command payload.
	};

	struct SetPositionCommand : public Command
	{
		SetPositionCommand() : Command(CommandPayloadType::SetPosition) {}

		virtual size_t getCommandSize() const override
		{
			return sizeof(SetPositionCommand);
		}

		vec3 origin_pos;
		vec4 orientation;
		uint64_t valid_counter = 0;
		uint8_t set_relative_pos = 0;
		vec3 relative_pos;
	};

	struct SetupCommand : public Command
	{
		SetupCommand() : Command(CommandPayloadType::Setup) {}

		virtual size_t getCommandSize() const override
		{
			return sizeof(SetupCommand);
		}

		int32_t server_streaming_port = 0;
		uint32_t debug_stream = 0;
		uint32_t do_checksums = 0;
		uint32_t debug_network_packets = 0;
		int32_t requiredLatencyMs = 0;
		uint32_t idle_connection_timeout = 5000;
		uid	server_id = 0;
		ControlModel control_model = ControlModel::NONE;
		VideoConfig video_config;
		vec3 bodyOffsetFromHead;
		AxesStandard axesStandard = AxesStandard::NotInitialized;
		uint8_t audio_input_enabled = 0;
	};

	struct ReconfigureVideoCommand : public Command
	{
		ReconfigureVideoCommand() : Command(CommandPayloadType::ReconfigureVideo) {}

		virtual size_t getCommandSize() const override
		{
			return sizeof(ReconfigureVideoCommand);
		}

		VideoConfig video_config;
	};

	struct ShutdownCommand : public Command
	{
		ShutdownCommand() : Command(CommandPayloadType::Shutdown) {}

		virtual size_t getCommandSize() const override
		{
			return sizeof(ShutdownCommand);
		}
	};

	struct NodeBoundsCommand : public Command
	{
		size_t nodesShowAmount;
		size_t nodesHideAmount;

		NodeBoundsCommand()
			:NodeBoundsCommand(0, 0)
		{}

		NodeBoundsCommand(size_t nodesShowAmount, size_t nodesHideAmount)
			:Command(CommandPayloadType::NodeBounds), nodesShowAmount(nodesShowAmount), nodesHideAmount(nodesHideAmount)
		{}

		virtual size_t getCommandSize() const override
		{
			return sizeof(NodeBoundsCommand);
		}
	};

	struct UpdateNodeMovementCommand : public Command
	{
		size_t updatesAmount;

		UpdateNodeMovementCommand()
			:UpdateNodeMovementCommand(0)
		{}

		UpdateNodeMovementCommand(size_t updatesAmount)
			:Command(CommandPayloadType::UpdateNodeMovement), updatesAmount(updatesAmount)
		{}

		virtual size_t getCommandSize() const override
		{
			return sizeof(UpdateNodeMovementCommand);
		}
	};

	struct UpdateNodeAnimationCommand : public Command
	{
		avs::NodeUpdateAnimation animationUpdate;

		UpdateNodeAnimationCommand()
			:UpdateNodeAnimationCommand(avs::NodeUpdateAnimation{})
		{}

		UpdateNodeAnimationCommand(const avs::NodeUpdateAnimation& update)
			:Command(CommandPayloadType::UpdateNodeAnimation), animationUpdate(update)
		{}

		virtual size_t getCommandSize() const override
		{
			return sizeof(UpdateNodeAnimationCommand);
		}
	};

	struct ClientMessage
	{
		ClientMessagePayloadType clientMessagePayloadType;

		ClientMessage(ClientMessagePayloadType t) : clientMessagePayloadType(t) {}

		//Returns byte size of message.
		virtual size_t getMessageSize() const = 0;
	};

	//Message info struct containing how many nodes have changed to what state; sent alongside two list of node UIDs.
	struct NodeStatusMessage : public ClientMessage
	{
		size_t nodesDrawnAmount;
		size_t nodesWantToReleaseAmount;

		NodeStatusMessage()
			:NodeStatusMessage(0, 0)
		{}

		NodeStatusMessage(size_t nodesDrawnAmount, size_t nodesWantToReleaseAmount)
			:ClientMessage(ClientMessagePayloadType::NodeStatus),
			nodesDrawnAmount(nodesDrawnAmount),
			nodesWantToReleaseAmount(nodesWantToReleaseAmount)
		{}

		virtual size_t getMessageSize() const override
		{
			return sizeof(NodeStatusMessage);
		}
	};

	//Message info struct containing how many resources were received; sent alongside a list of UIDs.
	struct ReceivedResourcesMessage : public ClientMessage
	{
		size_t receivedResourcesAmount;

		ReceivedResourcesMessage()
			:ReceivedResourcesMessage(0)
		{}

		ReceivedResourcesMessage(size_t receivedResourcesAmount)
			:ClientMessage(ClientMessagePayloadType::ReceivedResources), receivedResourcesAmount(receivedResourcesAmount)
		{}

		virtual size_t getMessageSize() const override
		{
			return sizeof(ReceivedResourcesMessage);
		}
	};

	//Message info struct containing how many resources were received; sent alongside a list of UIDs.
	struct ControllerPosesMessage : public ClientMessage
	{
		Pose headPose;
		Pose controllerPoses[2];

		ControllerPosesMessage()
			:ClientMessage(ClientMessagePayloadType::ControllerPoses)
		{}

		virtual size_t getMessageSize() const override
		{
			return sizeof(ControllerPosesMessage);
		}
	};

	struct OriginPoseMessage : public ClientMessage
	{
		uint64_t counter = 0;
		Pose originPose;

		OriginPoseMessage() :ClientMessage(ClientMessagePayloadType::OriginPose) {}

		virtual size_t getMessageSize() const override
		{
			return sizeof(OriginPoseMessage);
		}
	};
#pragma pack(pop)
} //namespace avs
