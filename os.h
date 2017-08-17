#ifndef _VPND_OS_H_
#define _VPND_OS_H_

#include "vpnd.h"

#define SYS_IP_FORWARDING "net.inet.ip.forwarding"
#define SYS_IP6_FORWARDING "net.inet6.ip6.forwarding"

bool		read_nonce(struct vpn_state *vpn, nonce_type type);
void		write_nonce(struct vpn_state *vpn, nonce_type type);
bool		get_sysctl_bool(struct vpn_state *vpn, char *name);
void		set_sysctl_bool(struct vpn_state *vpn, char *name, bool value);
void		get_cur_monotonic(struct timespec *tp);
void		spawn_subprocess(struct vpn_state *vpn, char *cmd);
void		add_timer (struct vpn_state *vpn, timer_type ttype, intptr_t timeout_interval);
bool		run       (struct vpn_state *vpn);
#if defined(__NetBSD__) || defined(__MacOSX__)
long long	strtonum(const char *nptr, long long minval, long long maxval, const char **errstr);
#endif
#endif				/* !_VPND_OS_H_ */