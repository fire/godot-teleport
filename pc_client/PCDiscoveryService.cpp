#include "PCDiscoveryService.h"

#include <random>

#include "crossplatform/Log.h"

#pragma pack(push, 1) 
struct ServiceDiscoveryResponse
{
	uint32_t clientID;
	uint16_t remotePort;
};
#pragma pack(pop)

PCDiscoveryService::PCDiscoveryService()
{
	std::random_device rd;  //Will be used to obtain a seed for the random number engine
	std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
	std::uniform_int_distribution<> dis(1);

	clientID = static_cast<uint32_t>(dis(gen));
}

PCDiscoveryService::~PCDiscoveryService()
{
	if(serviceDiscoverySocket)
	{
		enet_socket_destroy(serviceDiscoverySocket);
		serviceDiscoverySocket = 0;
	}
}

int PCDiscoveryService::CreateDiscoverySocket(uint16_t discoveryPort)
{
	int sock = enet_socket_create(ENetSocketType::ENET_SOCKET_TYPE_DATAGRAM);// PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock <= 0)
	{
		FAIL("Failed to create service discovery UDP socket");
		return 0;
	}

	int flagEnable = 1;
	enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);
	enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
	// We don't want to block, just check for packets.
	enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

	// Here we BIND the socket to the local address that we want to be identified with.
	// e.g. our OWN local IP.
	ENetAddress bindAddress = { ENET_HOST_ANY, discoveryPort };
	enet_address_set_host(&(bindAddress), "127.0.0.1");
	if (enet_socket_bind(sock, &bindAddress) != 0)
	{
		FAIL("Failed to bind to service discovery UDP socket");
		enet_socket_destroy(sock);
		sock = 0;
		return 0;
	}
	return sock;
}

uint32_t PCDiscoveryService::Discover(uint16_t discoveryPort, ENetAddress& remote)
{
	bool serverDiscovered = false;

	ENetAddress broadcastAddress = {ENET_HOST_BROADCAST, discoveryPort};

	if(!serviceDiscoverySocket)
	{
		serviceDiscoverySocket=CreateDiscoverySocket(discoveryPort);
	}
	ENetBuffer buffer = {sizeof(clientID) ,(void*)&clientID};
	ServiceDiscoveryResponse response = {};
	ENetAddress  responseAddress = {0xffffffff, 0};
	ENetBuffer responseBuffer = {sizeof(response),&response};
	// Send our client id to the server on the discovery port. Once every 1000 frames.
	static int frame=1000;
	frame--;
	if(!frame)
	{
		enet_socket_send(serviceDiscoverySocket, &broadcastAddress, &buffer, 1);
		frame=1000;
	}

	static size_t bytesRecv;
	do
	{
		// This will change responseAddress from 0xffffffff into the address of the server
		bytesRecv = enet_socket_receive(serviceDiscoverySocket, &responseAddress, &responseBuffer, 1);

		if(bytesRecv == sizeof(response) && clientID == response.clientID)
		{
			remote.host = responseAddress.host;
			remote.port = response.remotePort;
			serverDiscovered = true;
		}
	}
	while(bytesRecv > 0 && !serverDiscovered);


	if(serverDiscovered)
	{
		char remoteIP[20];
		enet_address_get_host_ip(&remote, remoteIP, sizeof(remoteIP));
		LOG("Discovered session server: %s:%d", remoteIP, remote.port);

		enet_socket_destroy(serviceDiscoverySocket);
		serviceDiscoverySocket = 0;
		return clientID;
	}
	return 0;
}