#include "ClientApp.h"

using namespace teleport;
using namespace client;

ClientApp::ClientApp()
{
}

ClientApp::~ClientApp()
{
}

void ClientApp::Initialize()
{
	auto &config=Config::GetInstance();
	config.LoadConfigFromIniFile();
	config.LoadBookmarks();
	config.LoadOptions();
}
