#define _DECL_STRINGS_

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "diag.h"
#include "nonce.h"
#include "os.h"
#include "util.h"

char           *
string_for_addr(sa_family_t af, void *addr, char *addr_str, size_t addr_str_sz)
{
	switch (af) {
	case AF_UNSPEC:
		strlcpy(addr_str, "<none>", addr_str_sz);
		break;
	case AF_INET:
	case AF_INET6:
		inet_ntop(af, addr, addr_str, addr_str_sz);
		break;
	default:
		snprintf(addr_str, addr_str_sz, "<invalid, (af %u)>", af);

	}

	return addr_str;
}

void
log_msg(struct vpn_state *vpn, int priority, const char *msg,...)
{
	va_list		ap;
	time_t		now;
	struct tm	now_tm;
	char		timestamp_str[32];
	char		msg_str   [1024];

	va_start(ap, msg);
	if (vpn->foreground) {
		if (priority <= vpn->log_upto) {
			time(&now);
			localtime_r(&now, &now_tm);
			strftime(timestamp_str, sizeof(timestamp_str), "%c", &now_tm);
			vsnprintf(msg_str, sizeof(msg_str), msg, ap);
			printf("%s %s\n", timestamp_str, msg_str);
		}
	} else {
		vsyslog(priority, msg, ap);
	}
	va_end(ap);
}

char           *
time_str(time_t time, char *time_str, size_t len)
{
	time_t		h      , m, s;

	m = time / 60;
	s = time % 60;
	h = m / 60;
	m = m % 60;
	snprintf(time_str, len, "%ld:%02ld:%02ld", h, m, s);

	return time_str;
}

void
log_invalid_msg_for_state(struct vpn_state *vpn, message_type msg_type)
{
	log_msg(vpn, LOG_ERR, "%s: received unexpected %s message",
		VPN_STATE_STR(vpn->state), MSG_TYPE_STR(msg_type));
}

void
log_nonce(struct vpn_state *vpn, char *prefix, nonce_type type, unsigned char *nonce)
{
	char		nonce_str [(crypto_box_NONCEBYTES * 2) + 1] = {'\0'};
	char           *nonce_filename;

	nonce_filename = (type == LOCAL) ? vpn->local_nonce_filename
	    : vpn->remote_nonce_filename;

	log_msg(vpn, LOG_NOTICE, "%s: %s (%s) %s)", VPN_ROLE_STR(vpn->role), prefix,
		nonce_filename,
		sodium_bin2hex(nonce_str, sizeof(nonce_str), nonce, crypto_box_NONCEBYTES));
}

void
log_retransmit(struct vpn_state *vpn, message_type msg_type)
{
	log_msg(vpn, LOG_NOTICE, "%s: retransmitting %s", VPN_STATE_STR(vpn->state),
		MSG_TYPE_STR(msg_type));
}

void
log_skip_retransmit(struct vpn_state *vpn, uintptr_t timer_id)
{
	log_msg(vpn, LOG_INFO, "%s: not renewing %s", VPN_STATE_STR(vpn->state),
		TIMER_TYPE_STR(timer_id));
}

void
log_stats(struct vpn_state *vpn)
{
	struct timespec	now;
	time_t		cur_inactive_secs, cur_sess_active_secs;
	char		cur_inactive_str[32], cur_sess_active_str[32];
	char		tx_nonce_str[(crypto_box_NONCEBYTES * 2) + 1] = {'\0'};
	char		rx_nonce_str[(crypto_box_NONCEBYTES * 2) + 1] = {'\0'};
	char		host_addr_str[INET6_ADDRSTRLEN];
	char		remote_net_str[INET6_ADDRSTRLEN];
	char		resolv_addr_str[INET6_ADDRSTRLEN];

	get_cur_monotonic(&now);
	cur_inactive_secs = vpn->inactive_secs;
	cur_sess_active_secs = vpn->sess_active_secs;

	if (vpn->state == ACTIVE_MASTER || vpn->state == ACTIVE_SLAVE) {
		cur_sess_active_secs += (now.tv_sec - vpn->sess_start_ts.tv_sec);
	} else {
		cur_inactive_secs += (now.tv_sec - vpn->sess_end_ts.tv_sec);
	}

	log_msg(vpn, LOG_NOTICE, "--- vpnd statistics ---\n"
		"version: %s\n"
		"%s is %s:\n"
		"sessions: %" PRIu32 ", keys used: %" PRIu32 " (max age %" PRIu32 " sec.)\n"
		"current key: %s\n"
		"time inactive/active: %s/%s\n"
		"data bytes rx/tx: %" PRIu32 "/%" PRIu32 "\n"
		"packets rx/tx: %" PRIu32 "/%" PRIu32 "\n"
		"late rx packets: %" PRIu32 "\n"
		"late rx packets (cur key): %" PRIu32 "\n"
		"bad nonces: %" PRIu32 "\n"
		"nonces since reset: %" PRIu32 "\n"
		"decrypt failures: %" PRIu32 "\n"
		"retransmits (pi/kss/ksa/kr): %" PRIu32
		"/%" PRIu32 "/%" PRIu32 "/%" PRIu32 "\n"
		"last peer message: %" PRIu32 " sec. ago\n",
		VPND_VERSION,
		VPN_ROLE_STR(vpn->role),
		VPN_STATE_STR(vpn->state),
		vpn->sess_starts, vpn->keys_used, vpn->max_key_age_secs,
		vpn->shared_key_is_ephemeral ? "ephemeral" : "pre-shared",
		time_str(cur_inactive_secs, cur_inactive_str,
			 sizeof(cur_inactive_str)),
		time_str(cur_sess_active_secs, cur_sess_active_str,
			 sizeof(cur_sess_active_str)),
		vpn->rx_data_bytes, vpn->tx_data_bytes,
		vpn->rx_packets, vpn->tx_packets,
	   vpn->rx_late_packets, cur_key_late_packets(vpn), vpn->bad_nonces,
		vpn->nonce_incr_count, vpn->decrypt_failures,
	      vpn->peer_init_retransmits, vpn->key_switch_start_retransmits,
		vpn->key_switch_ack_retransmits,
		vpn->key_ready_retransmits,
		(now.tv_sec - vpn->peer_last_heartbeat_ts.tv_sec));

	log_msg(vpn, LOG_NOTICE, "--- vpnd nonces ---\n"
		"TX nonce: %s\n"
		"RX nonce: %s\n",
	      sodium_bin2hex(tx_nonce_str, sizeof(tx_nonce_str), vpn->nonce,
			     crypto_box_NONCEBYTES),
	sodium_bin2hex(rx_nonce_str, sizeof(rx_nonce_str), vpn->remote_nonce,
		       crypto_box_NONCEBYTES));

	switch (vpn->role) {
	case HOST:
		log_msg(vpn, LOG_NOTICE, "--- vpnd peerinfo ---\n"
			"RX peerinfo:\n"
			"  host: %s/%d\n"
			"  remote network: %s/%d\n"
			"  resolver: %s\n"
			"  domain: %s",
			string_for_addr(vpn->rx_peer_info.host_addr_family,
				&vpn->rx_peer_info.host_addr, host_addr_str,
		  sizeof(host_addr_str)), vpn->rx_peer_info.host_prefix_len,
		   string_for_addr(vpn->rx_peer_info.remote_net_addr_family,
			      &vpn->rx_peer_info.remote_net, remote_net_str,
				   sizeof(remote_net_str)), vpn->rx_peer_info.remote_net_prefix_len,
			string_for_addr(vpn->rx_peer_info.resolv_addr_family,
			    &vpn->rx_peer_info.resolv_addr, resolv_addr_str,
					sizeof(resolv_addr_str)),
			strlen(vpn->rx_peer_info.resolv_domain) > 0
			? vpn->rx_peer_info.resolv_domain : "<none>");
		break;
	case HOST_GW:
		log_msg(vpn, LOG_NOTICE, "--- nonces ---\n"
			"\nTX peerinfo:\n"
			"  host: %s/%d\n"
			"  local network: %s/%d\n"
			"  resolver: %s\n"
			"  domain: %s",
			string_for_addr(vpn->tx_peer_info.host_addr_family,
				&vpn->tx_peer_info.host_addr, host_addr_str,
		  sizeof(host_addr_str)), vpn->tx_peer_info.host_prefix_len,
		   string_for_addr(vpn->tx_peer_info.remote_net_addr_family,
			      &vpn->tx_peer_info.remote_net, remote_net_str,
				   sizeof(remote_net_str)), vpn->tx_peer_info.remote_net_prefix_len,
			string_for_addr(vpn->tx_peer_info.resolv_addr_family,
			    &vpn->tx_peer_info.resolv_addr, resolv_addr_str,
					sizeof(resolv_addr_str)),
			strlen(vpn->tx_peer_info.resolv_domain) > 0
			? vpn->tx_peer_info.resolv_domain : "<none>");
		break;
	default:
		break;
	}

}

void
tx_graphite_stats(struct vpn_state *vpn, int client_fd)
{
	time_t		n;
	long long	now;
	char		stats_buf [2048];

	time(&n);
	now = (long long)n;
	snprintf(stats_buf, sizeof(stats_buf),
	    "%s.vpnd.keys %" PRIu32 " %lld\n"
	    "%s.vpnd.sessions %" PRIu32 " %lld\n"
	    "%s.vpnd.rx.data_bytes %" PRIu32 " %lld\n"
	    "%s.vpnd.tx.data_bytes %" PRIu32 " %lld\n"
	    "%s.vpnd.rx.packets %" PRIu32 " %lld\n"
	    "%s.vpnd.tx.packets %" PRIu32 " %lld\n"
	    "%s.vpnd.rx.late %" PRIu32 " %lld\n"
	    "%s.vpnd.rx._cur_key_late %" PRIu32 " %lld\n"
	    "%s.vpnd.bad_nonces %" PRIu32 " %lld\n"
	    "%s.vpnd.decrypt_failures %" PRIu32 " %lld\n"
	    "%s.vpnd.peer_info_retransmits %" PRIu32 " %lld\n"
	    "%s.vpnd.key_switch_start_retransmits %" PRIu32 " %lld\n"
	    "%s.vpnd.key_ack_retransmits %" PRIu32 " %lld\n"
	    "%s.vpnd.key_ready_retransmits %" PRIu32 " %lld\n",
	    vpn->stats_prefix, vpn->keys_used, now,
	    vpn->stats_prefix, vpn->sess_starts, now,
	    vpn->stats_prefix, vpn->rx_data_bytes, now,
	    vpn->stats_prefix, vpn->tx_data_bytes, now,
	    vpn->stats_prefix, vpn->rx_packets, now,
	    vpn->stats_prefix, vpn->tx_packets, now,
	    vpn->stats_prefix, vpn->rx_late_packets, now,
	    vpn->stats_prefix, cur_key_late_packets(vpn), now,
	    vpn->stats_prefix, vpn->bad_nonces, now,
	    vpn->stats_prefix, vpn->decrypt_failures, now,
	    vpn->stats_prefix, vpn->peer_init_retransmits, now,
	    vpn->stats_prefix, vpn->key_switch_start_retransmits, now,
	    vpn->stats_prefix, vpn->key_switch_ack_retransmits, now,
	    vpn->stats_prefix, vpn->key_ready_retransmits, now);
	write(client_fd, stats_buf, strlen(stats_buf));

}
