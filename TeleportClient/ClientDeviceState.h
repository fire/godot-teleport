#pragma once
#include <libavstream/common.hpp>
#include "basic_linear_algebra.h"
#include <Input.h>

namespace teleport
{
	namespace client
	{
		//!The generic state of the client hardware device e.g. headset, controllers etc.
		class ClientDeviceState
		{
		public:
			ClientDeviceState();

			scr::mat4 transformToLocalOrigin; // Because we're using OVR's rendering, we must position the actors relative to the oculus origin.
			float eyeHeight=0.5f;
			float stickYaw=0.0f;
			avs::Pose controllerRelativePoses[2];	// in local space.

			avs::Pose headPose;				// in game absolute space.
			avs::Pose relativeHeadPose;		// in local space
			avs::Pose originPose;			// in game absolute space.
			avs::Pose controllerPoses[2];	// in game absolute space.
			teleport::client::ControllerState controllerStates[2];

			void TransformPose(avs::Pose &p);
			void SetHeadPose(avs::vec3 pos,scr::quat q);
			void SetControllerPose(int index,avs::vec3 pos,scr::quat q);
			void SetControllerState(int index, const teleport::client::ControllerState& st);
		};
	}
}