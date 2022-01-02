#include "ClientDeviceState.h"
#include "basic_linear_algebra.h"

using namespace teleport;
using namespace client;

ClientDeviceState::ClientDeviceState()
{
}

void ClientDeviceState::TransformPose(avs::Pose &p)
{
	scr::quat stickRotateQ = originPose.orientation;// (stickYaw, avs::vec3(0, 1.0f, 0));
	scr::quat localQ=*((scr::quat*)(&p.orientation));
	scr::quat globalQ=(stickRotateQ*localQ);
	p.orientation=globalQ;

	avs::vec3 relp=p.position;
	scr::quat localP=*((scr::quat*)(&(relp)));
	localP.s=0;
	scr::quat globalP=(stickRotateQ*localP)*(stickRotateQ.Conjugate());
	p.position=avs::vec3(globalP.i,globalP.j,globalP.k);
}

void ClientDeviceState::SetHeadPose(avs::vec3 pos,scr::quat q)
{
	headPose.orientation=*((const avs::vec4 *)(&q));
	headPose.position=pos;
	relativeHeadPose = headPose;
	TransformPose(headPose);
	headPose.position+=originPose.position;
}

void ClientDeviceState::SetControllerPose(int index,avs::vec3 pos,scr::quat q)
{
	controllerPoses[index].position = pos;
	controllerPoses[index].orientation = *((const avs::vec4 *)(&q));
	controllerRelativePoses[index]=controllerPoses[index];
	TransformPose(controllerPoses[index]);
	controllerPoses[index].position+=originPose.position;
}

void ClientDeviceState::SetControllerState(int index, const teleport::client::ControllerState& st)
{
	controllerStates[index]=st;
}