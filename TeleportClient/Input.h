// (C) Copyright 2018-2021 Simul Software Ltd

#pragma once

#include <map>
#include <stdint.h>
#include <vector>

#include "libavstream/common_input.h"

namespace teleport
{
	namespace client
	{
		struct ControllerState
		{
			uint32_t mButtons = 0;
			uint32_t mReleased = 0;
			bool  mTrackpadStatus = false;
			float mTrackpadX = 0.0f;
			float mTrackpadY = 0.0f;
			float mJoystickAxisX = 0.0f;
			float mJoystickAxisY = 0.0f;

			//We are using hard-set values as the Android compiler didn't like reading from referenced memory in a dictionary; every other frame it would evaluate to zero.
			float triggerBack = 0.0f;
			float triggerGrip = 0.0f;

			//These are split for simplicity, and we can't marshal polymorphic types to the managed C# code.
			std::vector<avs::InputEventBinary> binaryEvents;
			std::vector<avs::InputEventAnalogue> analogueEvents;
			std::vector<avs::InputEventMotion> motionEvents;

			void clear();
			void addBinaryEvent(uint32_t eventID, avs::InputId inputID, bool activated);
			void addAnalogueEvent(uint32_t eventID, avs::InputId inputID, float strength);
			void addMotionEvent(uint32_t eventID, avs::InputId inputID, avs::vec2 motion);
		};
	}
}