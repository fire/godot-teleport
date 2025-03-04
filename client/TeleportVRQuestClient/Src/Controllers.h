#ifndef CLIENT_CONTROLLERS_H
#define CLIENT_CONTROLLERS_H

#include <functional>

#include <VrApi_Input.h>

#include <TeleportCore/Input.h>
#include "libavstream/common_input.h"

typedef std::function<void()> TriggerDelegate;
typedef std::function<void(float,float)> Float2Delegate;

class Controllers
{
public:
	static constexpr int CONTROLLER_AMOUNT = 2;

	Controllers();
	~Controllers();

	void SetCycleShaderModeDelegate(TriggerDelegate d);
	void SetCycleOSDDelegate(TriggerDelegate d);
	void SetCycleOSDSelectionDelegate(TriggerDelegate d);
	void SetToggleMenuDelegate(TriggerDelegate d);
	void SetDebugOutputDelegate(TriggerDelegate d);
	void SetToggleWebcamDelegate(TriggerDelegate d);
	void SetSetStickOffsetDelegate(Float2Delegate d);
	void ClearDelegates();

	bool InitializeController(ovrMobile *pMobile,int idx);
	void Update(ovrMobile *ovrmobile);

	ovrDeviceID mControllerIDs[CONTROLLER_AMOUNT];

	ovrVector2f mTrackpadDim;
private:

	TriggerDelegate CycleShaderMode;
	TriggerDelegate CycleOSD;
	TriggerDelegate CycleOSDSelection;
	TriggerDelegate WriteDebugOutput;
	TriggerDelegate ToggleWebcam;
	TriggerDelegate ToggleMenu;
	Float2Delegate SetStickOffset;

	uint32_t nextEventID = 0;
};


#endif //CLIENT_CONTROLLERS_H
