#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET rush_socket_t;
  #define RUSH_INVALID_SOCKET INVALID_SOCKET
  #define RUSH_CLOSESOCKET closesocket
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <time.h>
  #include <signal.h>
  typedef int rush_socket_t;
  #define RUSH_INVALID_SOCKET (-1)
  #define RUSH_CLOSESOCKET close
#endif

#include "net.h"
#include "rush.h"

int net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

double time_now_seconds(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int have_freq = 0;
    LARGE_INTEGER counter;
    if (!have_freq) { QueryPerformanceFrequency(&freq); have_freq = 1; }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

int net_bounce(const char *host, const char *port, double *elapsed_out) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    double start = time_now_seconds();

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        *elapsed_out = time_now_seconds() - start;
        return -1;
    }

    int connected = 0;
    rush_socket_t sock = RUSH_INVALID_SOCKET;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == RUSH_INVALID_SOCKET) continue;
        if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
            connected = 1;
            break;
        }
        RUSH_CLOSESOCKET(sock);
        sock = RUSH_INVALID_SOCKET;
    }

    *elapsed_out = time_now_seconds() - start;
    freeaddrinfo(res);
    if (connected) RUSH_CLOSESOCKET(sock);
    return connected ? 0 : -1;
}

int net_resolve_ip(const char *host, char *ip_out, size_t ip_sz) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;

    void *addr;
    if (res->ai_family == AF_INET) {
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    } else if (res->ai_family == AF_INET6) {
        addr = &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
    } else {
        freeaddrinfo(res);
        return -1;
    }

    const char *rc = inet_ntop(res->ai_family, addr, ip_out, (socklen_t)ip_sz);
    freeaddrinfo(res);
    return rc ? 0 : -1;
}

#ifdef _WIN32
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_interrupt = 1;
        return TRUE; /* handled - don't let Windows kill the process */
    }
    return FALSE;
}
#else
static void sigint_handler(int signum) {
    (void)signum;
    g_interrupt = 1;
}
#endif

void net_install_interrupt_handler(void) {
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
#endif
}
