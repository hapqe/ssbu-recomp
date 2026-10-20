/* winsock2.h / ws2tcpip.h / mstcpip.h for the Android port: Winsock on BSD sockets. */
#ifndef ANDROID_COMPAT_WINSOCK_H
#define ANDROID_COMPAT_WINSOCK_H
#include "windows.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
typedef int SOCKET;
typedef struct sockaddr SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;
typedef struct sockaddr_storage SOCKADDR_STORAGE;
typedef struct in_addr IN_ADDR;
typedef struct addrinfo ADDRINFOA, *PADDRINFOA;
typedef struct pollfd WSAPOLLFD;
typedef struct linger LINGER;
typedef int socklen_t_compat;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#define closesocket close
#define WSAPoll poll
#define SO_NOSIGPIPE_COMPAT 0
#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0x1022                 /* not on Linux: setsockopt fails with ENOPROTOOPT, which callers ignore */
#endif
#define SIO_UDP_CONNRESET 0x9800000C
#define SIO_KEEPALIVE_VALS 0x98000004
typedef struct WSAData { WORD wVersion, wHighVersion; char szDescription[257], szSystemStatus[129]; unsigned short iMaxSockets, iMaxUdpDg; char *lpVendorInfo; } WSADATA, *LPWSADATA;
int WSAStartup(WORD v, LPWSADATA d);
int WSACleanup(void);
int WSAGetLastError(void);
void WSASetLastError(int e);
int WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD inn, LPVOID out, DWORD outn, LPDWORD got, LPOVERLAPPED ov, void *cb);
int ioctlsocket(SOCKET s, long cmd, u_long *arg);
#define InetPtonA inet_pton
#define InetNtopA inet_ntop
/* Winsock's SO_RCVTIMEO / SO_SNDTIMEO take a DWORD of milliseconds; Linux takes a struct timeval */
int compat_setsockopt(int s, int level, int name, const void *v, socklen_t n);
int compat_getsockopt(int s, int level, int name, void *v, socklen_t *n);
#define setsockopt compat_setsockopt
#define getsockopt compat_getsockopt
/* Winsock error codes as errno values */
#define WSAEINTR EINTR
#define WSAEBADF EBADF
#define WSAEACCES EACCES
#define WSAEFAULT EFAULT
#define WSAEINVAL EINVAL
#define WSAEMFILE EMFILE
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define WSAEALREADY EALREADY
#define WSAENOTSOCK ENOTSOCK
#define WSAEDESTADDRREQ EDESTADDRREQ
#define WSAEMSGSIZE EMSGSIZE
#define WSAEPROTOTYPE EPROTOTYPE
#define WSAENOPROTOOPT ENOPROTOOPT
#define WSAEPROTONOSUPPORT EPROTONOSUPPORT
#define WSAESOCKTNOSUPPORT ESOCKTNOSUPPORT
#define WSAEOPNOTSUPP EOPNOTSUPP
#define WSAEPFNOSUPPORT EPFNOSUPPORT
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#define WSAEADDRINUSE EADDRINUSE
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#define WSAENETDOWN ENETDOWN
#define WSAENETUNREACH ENETUNREACH
#define WSAENETRESET ENETRESET
#define WSAECONNABORTED ECONNABORTED
#define WSAECONNRESET ECONNRESET
#define WSAENOBUFS ENOBUFS
#define WSAEISCONN EISCONN
#define WSAENOTCONN ENOTCONN
#define WSAESHUTDOWN ESHUTDOWN
#define WSAETIMEDOUT ETIMEDOUT
#define WSAECONNREFUSED ECONNREFUSED
#define WSAEHOSTDOWN EHOSTDOWN
#define WSAEHOSTUNREACH EHOSTUNREACH
/* getaddrinfo results: Winsock returns WSA codes, bionic EAI_* values */
#define WSATRY_AGAIN EAI_AGAIN
#define WSANO_RECOVERY EAI_FAIL
#define WSA_NOT_ENOUGH_MEMORY EAI_MEMORY
#define WSAHOST_NOT_FOUND EAI_NONAME
#define WSATYPE_NOT_FOUND EAI_SERVICE
#define WSANO_DATA EAI_NODATA
#endif
