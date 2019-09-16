// (C) Copyright 2018 Simul.co

#pragma once

#include <string>

#include <OVR_Input.h>
#include <enet/enet.h>
#include <common_p.hpp>

#include "Input.h"


extern void ClientLog( const char * fileTag, int lineno, const char * fmt, ... );

#define LOG( ... ) 	//ClientLog( __FILE__, __LINE__, __VA_ARGS__ )
#define WARN( ... ) //ClientLog( __FILE__, __LINE__, __VA_ARGS__ )
#define FAIL( ... ) //{ClientLog( __FILE__, __LINE__, __VA_ARGS__ );exit(0);}

typedef unsigned int uint;
class ResourceCreator;

class SessionCommandInterface
{
public:
    virtual void OnVideoStreamChanged(const avs::SetupCommand &) = 0;
    virtual void OnVideoStreamClosed() = 0;

    virtual bool OnActorEnteredBounds(avs::uid actor_uid) = 0;
    virtual bool OnActorLeftBounds(avs::uid actor_uid) = 0;
};

class SessionClient
{
public:
    SessionClient(SessionCommandInterface* commandInterface, ResourceCreator& resourceCreator);
    ~SessionClient();

    bool Discover(uint16_t discoveryPort, ENetAddress& remote);
    bool Connect(const char* remoteIP, uint16_t remotePort, uint timeout);
    bool Connect(const ENetAddress& remote, uint timeout);
    void Disconnect(uint timeout);

    void Frame(const OVR::ovrFrameInput& vrFrame, const ControllerState& controllerState);

    bool IsConnected() const;
    std::string GetServerIP() const;

private:
    void DispatchEvent(const ENetEvent& event);
    void ParseCommandPacket(ENetPacket* packet);
    void ParseTextCommand(const char *txt_utf8);

    void SendHeadPose(const ovrRigidBodyPosef& pose);
    void SendInput(const ControllerState& controllerState);
    void SendResourceRequests();
    //Tell server we are ready to receive geometry payloads.
    void SendHandshake();

    uint32_t mClientID = 0;
    int mServiceDiscoverySocket = 0;

    SessionCommandInterface* const mCommandInterface;
    ResourceCreator& mResourceCreator;
    ENetHost* mClientHost = nullptr;
    ENetPeer* mServerPeer = nullptr;
    ENetAddress mServerEndpoint;

    ControllerState mPrevControllerState = {};

    //bool isReadyToReceivePayloads = false;
    std::vector<avs::uid> mResourceRequests; //Requests the session client has discovered need to be made; currently only for actors.
};

