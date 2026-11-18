#ifdef _WIN32
#include <winsock2.h>

#else
#include_next <sys/ioctl.h>
#endif
