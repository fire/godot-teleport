#pragma once
#include <string>
#include <srt.h>
namespace avs
{
	extern sockaddr_in CreateAddrInet(const std::string& name, unsigned short port);
	extern void CHECK_SRT_ERROR(int err);
}