#include "Config.h"
#include "Platform/Core/SimpleIni.h"
#include "Platform/Core/FileLoader.h"
#include "TeleportCore/ErrorHandling.h"
#include "Platform/External/magic_enum/include/magic_enum.hpp"
#include <fmt/core.h>
#include <sstream>
using namespace teleport;
using namespace client;
using std::string;
using namespace std::string_literals;

void Config::LoadConfigFromIniFile()
{
	CSimpleIniA ini;
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	if(!fileLoader)
		return;
	string str=fileLoader->LoadAsString("assets/client.ini");
	SI_Error rc = ini.LoadData(str);
	if(rc == SI_OK)
	{
		string server_ip = ini.GetValue("", "SERVER_IP", TELEPORT_SERVER_IP);
		string ip_list;
		ip_list = ini.GetValue("", "SERVER_IP", "");

		size_t pos = 0;
		string token;
		do
		{
			pos = ip_list.find(",");
			string ip = ip_list.substr(0, pos);
			recent_server_urls.push_back(ip);
			ip_list.erase(0, pos + 1);
		} while (pos != string::npos);

		enable_vr = ini.GetLongValue("", "ENABLE_VR", enable_vr);
		dev_mode = ini.GetLongValue("", "DEV_MODE", dev_mode);
		log_filename = ini.GetValue("", "LOG_FILE", "TeleportClient.log");

		render_local_offline = ini.GetLongValue("", "RENDER_LOCAL_OFFLINE", render_local_offline);

	}
	else
	{
		TELEPORT_CERR<<"Create client.ini in pc_client directory to specify settings."<<std::endl;
	}
}

const std::vector<Bookmark> &Config::GetBookmarks() const
{
	return bookmarks;
}

void Config::AddBookmark(const Bookmark &b)
{
	bookmarks.push_back(b);
	SaveBookmarks();
}

void Config::LoadBookmarks()
{
	string str;
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	if(fileLoader)
	{
		void *ptr=nullptr;
		unsigned bytelen=0;
		std::string filename=GetStoragePath()+"config/bookmarks.txt"s;
		fileLoader->AcquireFileContents(ptr,bytelen,filename.c_str(),true);
		if(ptr)
			str=(char*)ptr;
		fileLoader->ReleaseFileContents(ptr);
	}
	if(str.length())
	{
		std::istringstream f(str);
		string line;    
		while (std::getline(f, line))
		{
			size_t split=line.find_first_of(' ');
			Bookmark b={line.substr(0,split),line.substr(split+1,line.length()-split-1)};
			AddBookmark(b);
		}
	}
	else
	{
		bookmarks.push_back({"192.168.3.40","192.168.3.40"});
		bookmarks.push_back({"test.teleportvr.io","test.teleportvr.io"});
		SaveBookmarks();
	}
}

void Config::SaveBookmarks()
{
//std::ofstream
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	{
		string str;
		for(const auto &b:bookmarks)
		{
			str+=fmt::format("{0} {1}\r\n",b.url,b.title);
		}
		std::string filename=GetStoragePath()+"config/bookmarks.txt"s;
		fileLoader->Save(str.data(),(unsigned int)str.length(),filename.c_str(),true);
	}
}

void Config::LoadOptions()
{
	CSimpleIniA ini;
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	if(!fileLoader)
		return;
	std::string filename=GetStoragePath()+"config/options.txt"s;
	string str=fileLoader->LoadAsString(filename.c_str());
	if(!str.length())
		return;	
	SI_Error rc = ini.LoadData(str);
	if(rc == SI_OK)
	{
		std::string s=ini.GetValue("", "LobbyView","");
		auto l=magic_enum::enum_cast<LobbyView>(s);
		if(l.has_value())
			options.lobbyView = l.value(); 
	}
}

void Config::SaveOptions()
{
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	{
		string str;
		str+=fmt::format("LobbyView={0}",magic_enum::enum_name(options.lobbyView));
		std::string filename=GetStoragePath()+"config/options.txt"s;
		fileLoader->Save(str.data(),(unsigned int)str.length(),filename.c_str(),true);
		LoadOptions();
	}
}

void Config::StoreRecentURL(const char *r)
{
	string s=r;
	if(s.length()==0)
		return;
	// If it's already in the recent list, move it to the front:
	for(int i=0;i<recent_server_urls.size();i++)
	{
		if(recent_server_urls[i]==s)
		{
			recent_server_urls.erase(recent_server_urls.begin()+i);
			i--;
		}
	}
	recent_server_urls.insert(recent_server_urls.begin(),s);
	
	auto *fileLoader=platform::core::FileLoader::GetFileLoader();
	//save recent:
	{
		string str;
		for(const auto &i:recent_server_urls)
		{
			str+=fmt::format("{0}\r\n",i);
		}
		std::string filename=GetStoragePath()+"config/recent_servers.txt";
		fileLoader->Save(str.data(),(unsigned int)str.length(),filename.c_str(),true);
	}
}

void Config::SetStorageFolder(const char *f)
{
	storageFolder=f;
}
const std::string &Config::GetStoragePath() const
{
	if(!storageFolder.length())
		return storageFolder;
	static std::string str;
	str=storageFolder+"/"s;
	return str;
}

const std::string &Config::GetStorageFolder() const
{
	return storageFolder;
}