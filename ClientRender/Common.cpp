#include "Common.h"
#include <stdarg.h>
// Implement snprintf for MS compilers
 #if defined(_MSC_VER) && _MSC_VER < 1900

#define snprintf c99_snprintf
#define vsnprintf c99_vsnprintf
 
inline int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap)
{
    int count = -1;

    if (size != 0)
        count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
    if (count == -1)
        count = _vscprintf(format, ap);

    return count;
}

inline int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
{
    int count;
    va_list ap;

    va_start(ap, format);
    count = c99_vsnprintf(outBuf, size, format, ap);
    va_end(ap);

    return count;
}

#endif
#if !defined(__ANDROID__)
void log_print(const char* source, const char* format, ...)
{
	va_list ap;
	char fmt[1000];
    sprintf_s(fmt, "%s %s", source, format);
	char outp[1000];
	va_start(ap, format);
	vsnprintf(outp,1000,fmt, ap);
	va_end(ap);
    // Now actually output the darn thing.
    std::cout<<outp<<std::endl;
}
#endif