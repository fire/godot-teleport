#include "DefaultHTTPService.h"

#include "SimulCasterServer/ErrorHandling.h"
#include "SimulCasterServer/ClientData.h"
#include "SimulCasterServer/CasterSettings.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

namespace teleport
{
	DefaultHTTPService::DefaultHTTPService()
	{

	}

	DefaultHTTPService::~DefaultHTTPService()
	{
		shutdown();
	}

	bool DefaultHTTPService::initialize(std::string mountDirectory, std::string certPath, std::string privateKeyPath, int32_t port)
	{
		if (mountDirectory == "")
		{
			TELEPORT_CERR << "Path of directory to mount cannot be empty!" << std::endl;
			return false;
		}

		if (certPath == "")
		{
			TELEPORT_CERR << "Path to SSL certificate cannot be empty!" << std::endl;
			return false;
		}

		if (privateKeyPath == "")
		{
			TELEPORT_CERR << "Path to private key cannot be empty!" << std::endl;
			return false;
		}

		if (port == 0)
		{
			TELEPORT_CERR << "HTTP port cannot be 0!" << std::endl;
			return false;
		}

		mServer.reset(new httplib::SSLServer(certPath.c_str(), privateKeyPath.c_str()));

		// Mount / to provided directory
		auto ret = mServer->set_mount_point("/", mountDirectory);
		if (!ret) 
		{
			TELEPORT_CERR << "Provided mount directory does not exist!" << std::endl;
			return false;
		}

		// TODO: Pass these extra mappings in as parameters.
		mServer->set_file_extension_and_mimetype_mapping("texture", "image/texture");
		mServer->set_file_extension_and_mimetype_mapping("mesh", "image/mesh");
		mServer->set_file_extension_and_mimetype_mapping("material", "image/material");
		mServer->set_file_extension_and_mimetype_mapping("basis", "image/basis");

		mServer->bind_to_port("0.0.0.0", port);

		if (!mThread.joinable())
		{
			mThread = std::thread(&httplib::Server::listen_after_bind, mServer.get());
		}

		return true;
	}

	void DefaultHTTPService::shutdown()
	{
		if (!mServer)
			return;

		mServer->stop();
		if (mThread.joinable())
		{
			mThread.join();
		}
		mServer.reset();
	}

	void DefaultHTTPService::tick()
	{

	}
}
