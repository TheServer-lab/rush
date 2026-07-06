#ifndef RUSH_NET_H
#define RUSH_NET_H

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

#endif
