#pragma once

#if defined(PLATFORM_ANDROID)
extern void RedirectStdCoutCerr();
#endif

enum class ClientLogPriority
{
	UNKNOWN,
	DEFAULT,
	VERBOSE,
	LOG_DEBUG,
	INFO,
	WARNING,
	LOG_ERROR,
	FATAL,
	SILENT
};

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

extern void ClientLog(const char* fileTag, int lineno, ClientLogPriority prio, const char* fmt, ...);
#define TELEPORT_CLIENT_LOG( ... ) 	ClientLog( __FILENAME__, __LINE__,ClientLogPriority::INFO, __VA_ARGS__ )
#define TELEPORT_CLIENT_WARN( ... ) ClientLog( __FILENAME__, __LINE__, ClientLogPriority::WARNING,__VA_ARGS__ )
#define TELEPORT_CLIENT_FAIL( ... ) {ClientLog( __FILENAME__, __LINE__, ClientLogPriority::LOG_ERROR,__VA_ARGS__ );exit(0);}


#define TELEPORT_CERR_BREAK(msg, errCode) std::cerr << __FILE__ << "(" << __LINE__ << "): " << msg << std::endl; throw(errCode);
#define TELEPORT_COUT_BREAK(msg, errCode) std::cout << __FILE__ << "(" << __LINE__ << "): " << msg << std::endl; throw(errCode);
#define TELEPORT_CERR std::cerr << __FILE__ << "(" << __LINE__ << "): error: " 

#define TELEPORT_COUT std::cout << __FILE__ << "(" << __LINE__ << "): " 