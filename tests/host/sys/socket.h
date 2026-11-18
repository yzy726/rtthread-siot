#ifndef TEST_SOCKET_H
#define TEST_SOCKET_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>
typedef int socklen_t;
static inline void host_set_errno(void)
{
    int error = WSAGetLastError();
    errno = error == WSAEWOULDBLOCK ? EWOULDBLOCK : error == WSAEINPROGRESS ? EINPROGRESS : EIO;
}
static inline int host_connect(int fd, const struct sockaddr *address, int length)
{
    int n = connect((SOCKET)fd, address, length);
    if (n < 0) host_set_errno();
    return n;
}
static inline int host_send(int fd, const void *data, size_t size, int flags)
{
    int n = send((SOCKET)fd, (const char *)data, (int)size, flags);
    if (n < 0) host_set_errno();
    return n;
}
static inline int host_recv(int fd, void *data, size_t size, int flags)
{
    int n = recv((SOCKET)fd, (char *)data, (int)size, flags);
    if (n < 0) host_set_errno();
    return n;
}
static inline int host_getsockopt(int fd, int level, int option, void *data, int *size)
{ return getsockopt((SOCKET)fd, level, option, (char *)data, size); }
#define connect host_connect
#define send host_send
#define recv host_recv
#define getsockopt host_getsockopt
#else
#include_next <sys/socket.h>
#include <unistd.h>
#define closesocket close
#define ioctlsocket ioctl
#endif
#endif
