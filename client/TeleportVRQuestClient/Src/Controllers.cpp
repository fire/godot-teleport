//
// Created by Roderick on 05/05/2020.
//

#include "Controllers.h"

#include <TeleportClient/Log.h>

Controllers::Controllers()
{
	mControllerIDs[0]=mControllerIDs[1]=0;
}

Controllers::~Controllers()
{

}

void Controllers::SetCycleShaderModeDelegate(TriggerDelegate delegate)
{
	CycleShaderMode = delegate;
}

void Controllers::SetCycleOSDDelegate(TriggerDelegate delegate)
{
	CycleOSD = delegate;
}
void Controllers::SetCycleOSDSelectionDelegate(TriggerDelegate delegate)
{
	CycleOSDSelection = delegate;
}

void Controllers::SetToggleMenuDelegate(TriggerDelegate delegate)
{
	ToggleMenu = delegate;
}

void Controllers::SetDebugOutputDelegate(TriggerDelegate delegate)
{
	WriteDebugOutput = delegate;
}

void Controllers::SetToggleWebcamDelegate(TriggerDelegate delegate)
{
	ToggleWebcam = delegate;
}

void Controllers::SetSetStickOffsetDelegate(Float2Delegate delegate)
{
	SetStickOffset = delegate;
}

void Controllers::ClearDelegates()
{
	CycleShaderMode = nullptr;
	CycleOSD = nullptr;
	CycleOSDSelection= nullptr;
	SetStickOffset = nullptr;
	WriteDebugOutput=nullptr;
	ToggleWebcam = nullptr;
}

bool Controllers::InitializeController(ovrMobile *ovrmobile,int idx)
{
	ovrInputCapabilityHeader inputCapsHeader;
	ovrControllerCapabilities caps;
	if(idx==1)
	{
		caps=ovrControllerCapabilities::ovrControllerCaps_RightHand;
	}
	if(idx==0)
	{
		caps=ovrControllerCapabilities::ovrControllerCaps_LeftHand;
	}

	for(uint32_t i = 0;i<2; ++i)
	{
		if(vrapi_EnumerateInputDevices(ovrmobile, i, &inputCapsHeader) >= 0)
		if(inputCapsHeader.Type == ovrControllerType_TrackedRemote&&(int)inputCapsHeader.DeviceID != -1)
		{
			ovrInputTrackedRemoteCapabilities trackedInputCaps;
			trackedInputCaps.Header = inputCapsHeader;
			vrapi_GetInputDeviceCapabilities(ovrmobile, &trackedInputCaps.Header);
			if((trackedInputCaps.ControllerCapabilities&caps)!=caps)
				continue;
			TELEPORT_CLIENT_LOG("Found controller (ID: %d)", inputCapsHeader.DeviceID);
			TELEPORT_CLIENT_LOG("Controller Capabilities: %ud", trackedInputCaps.ControllerCapabilities);
			TELEPORT_CLIENT_LOG("Button Capabilities: %ud", trackedInputCaps.ButtonCapabilities);
			TELEPORT_CLIENT_LOG("Trackpad range: %ud, %ud", trackedInputCaps.TrackpadMaxX, trackedInputCaps.TrackpadMaxX);
			TELEPORT_CLIENT_LOG("Trackpad range: %ud, %ud", trackedInputCaps.TrackpadMaxX, trackedInputCaps.TrackpadMaxX);
			mTrackpadDim.x = trackedInputCaps.TrackpadMaxX;
			mTrackpadDim.y = trackedInputCaps.TrackpadMaxY;
			mControllerIDs[idx] = inputCapsHeader.DeviceID;
		}
	}

	return (mControllerIDs[idx]>0);
}

void Controllers::Update(ovrMobile *ovrmobile)
{
	// Query controller input state.
	for(int i = 0; i < 2; i++)
	{
		if(mControllerIDs[i] != 0)
		{
			teleport::client::Input controllerState = {};
			teleport::client::ControllerState &lastControllerState = mLastControllerStates[i];
			ovrInputStateTrackedRemote ovrState;
			ovrState.Header.ControllerType = ovrControllerType_TrackedRemote;
			if(vrapi_GetCurrentInputState(ovrmobile, mControllerIDs[i], &ovrState.Header) >= 0)
			{
#if 0
				controllerState.mButtons = ovrState.Buttons;

				controllerState.mTrackpadStatus = ovrState.TrackpadStatus > 0;
				controllerState.mTrackpadX = ovrState.TrackpadPosition.x / mTrackpadDim.x;
				controllerState.mTrackpadY = ovrState.TrackpadPosition.y / mTrackpadDim.y;
				controllerState.mJoystickAxisX = ovrState.Joystick.x;
				controllerState.mJoystickAxisY = ovrState.Joystick.y;
#endif
				if(ovrState.TrackpadStatus)
				{
					float dx = ovrState.TrackpadPosition.x / mTrackpadDim.x - 0.5f;
					float dy = ovrState.TrackpadPosition.y / mTrackpadDim.y - 0.5f;
					SetStickOffset(dx, dy);
				}

				//uint32_t pressed = controllerState.mButtons & ~lastControllerState.mButtons;
				//controllerState.mReleased = ~controllerState.mButtons & lastControllerState.mButtons;

				//Detect when a button press or button release event occurs, and store the event in controllerState.
			/*	AddButtonPressEvent(pressed, controllerState.mReleased, controllerState, ovrButton::ovrButton_A, avs::InputId::BUTTON01);
				AddButtonPressEvent(pressed, controllerState.mReleased, controllerState, ovrButton::ovrButton_X, avs::InputId::BUTTON01);
				AddButtonPressEvent(pressed, controllerState.mReleased, controllerState, ovrButton::ovrButton_B, avs::InputId::BUTTON02);
				AddButtonPressEvent(pressed, controllerState.mReleased, controllerState, ovrButton::ovrButton_Y, avs::InputId::BUTTON02);
				AddButtonPressEvent(pressed, controllerState.mReleased, controllerState, ovrButton::ovrButton_Joystick, avs::InputId::BUTTON_STICK);
*/
				if(lastControllerState.triggerBack != ovrState.IndexTrigger)
				{
					controllerState.addAnalogueEvent(avs::InputId::TRIGGER_BACK, ovrState.IndexTrigger);
				}
				//controllerState.triggerBack = ovrState.IndexTrigger;

				if(lastControllerState.triggerGrip != ovrState.GripTrigger)
				{
					controllerState.addAnalogueEvent(avs::InputId::TRIGGER_GRIP, ovrState.GripTrigger);
				}
				//controllerState.triggerGrip = ovrState.GripTrigger;

				if((controllerState.mReleased & ovrButton::ovrButton_Enter) != 0)
				{
					ToggleMenu();
				}
				if((controllerState.mReleased & ovrButton::ovrButton_A) != 0)
				{
					CycleOSD();
				}
				if((controllerState.mReleased & ovrButton::ovrButton_B) != 0)
				{
					CycleOSDSelection();
				}
				if((controllerState.mReleased & ovrButton::ovrButton_X) != 0)
				{
					WriteDebugOutput();

					// All buttons seem to be taken up so putting it here for now
					ToggleWebcam();
				}
				if( (controllerState.mReleased & ovrButton::ovrButton_Y) != 0)
				{
					CycleShaderMode();
				}

				mLastControllerStates[i] = controllerState;
			}
		}
	}
}

void Controllers::AddButtonPressEvent(uint32_t pressedButtons, uint32_t releasedButtons, teleport::client::ControllerState& controllerState, ovrButton buttonID, avs::InputId inputID)
{
	if((pressedButtons & buttonID) != 0)
	{
		controllerState.addBinaryEvent(inputID, true);
	}
	else if((releasedButtons & buttonID) != 0)
	{
		controllerState.addBinaryEvent(inputID, false);
	}
}
