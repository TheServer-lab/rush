#ifndef RUSH_NET_H
#define RUSH_NET_H

#include <stddef.h>

/* One-time global network init/cleanup (WSAStartup on Windows, no-op
 * elsewhere). Call net_init() once at program start, net_cleanup()
 * once at exit. */
int net_init(void);
void net_cleanup(void);

/* Wall-clock time in fractional seconds, monotonic, portable. */
double time_now_seconds(void);

/* Resolve `host` and attempt a single TCP connection on `port`
 * (service name or numeric string, e.g. "443"). On return,
 * *elapsed_out is set to how long resolution+connect took, in
 * seconds. Returns 0 on a successful connect, -1 otherwise. */
int net_bounce(const char *host, const char *port, double *elapsed_out);

/* Resolve `host` to its first address and format it as a printable
 * string into ip_out (sized ip_sz). Returns 0 on success, -1 on
 * failure. Used by netch to show what address was actually reached. */
int net_resolve_ip(const char *host, char *ip_out, size_t ip_sz);

/* Installs a Ctrl+C (SIGINT) handler that sets g_interrupt instead of
 * killing the process, so long-running commands like an infinite
 * "loop" or "monitor" can stop gracefully. Call once at startup. */
void net_install_interrupt_handler(void);

#endif
