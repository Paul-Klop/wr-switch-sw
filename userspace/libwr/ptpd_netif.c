// Wrapper functions for network/timestamping/adjustment operations

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <fcntl.h>
#include <errno.h>

#include <asm/socket.h>

#include <libwr/ptpd_netif.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/hal_client.h>
#include <libwr/util.h>
#include <libwr/wrs-msg.h>
#include <net/ethernet.h>

#ifdef NETIF_VERBOSE
#define netif_dbg(...) printf(__VA_ARGS__)
#else
#define netif_dbg(...)
#endif

#define ETHER_MTU 1518
#define DMTD_UPDATE_INTERVAL 500

struct scm_timestamping {
	struct timespec systime;
	struct timespec hwtimetrans;
	struct timespec hwtimeraw;
};

struct etherpacket {
	struct ethhdr ether;
	char data[ETH_DATA_LEN];
};

static inline int tmo_init(struct wr_tmo * tmo, uint32_t milliseconds)
{
	tmo->start_tics = get_monotonic_us();
	tmo->timeout = (uint64_t) milliseconds *1000ULL;
	return 0;
}

static inline int tmo_restart(struct wr_tmo * tmo)
{
	tmo->start_tics = get_monotonic_us();
	return 0;
}

static inline int tmo_expired(struct wr_tmo * tmo)
{
	return (get_monotonic_us() - tmo->start_tics > tmo->timeout);
}

// cheks if x is inside range <min, max>
static inline int inside_range(int min, int max, int x)
{
	if (min < max)
		return (x >= min && x <= max);
	else
		return (x <= max || x >= min);
}

/* For debugging/testing purposes */
int ptpd_netif_get_dmtd_phase(int32_t *phase, struct hal_port_state *p)
{
	int phase_val_valid_local;

	if (!p)
		return -1;

	phase_val_valid_local = p->phase_val_valid;
	if (phase)
		*phase = phase_val_valid_local;
	return phase_val_valid_local;
}

static void update_dmtd(struct wr_socket *s, struct hal_port_state *p)
{

	if (!p)
		return;

	if (tmo_expired(&s->dmtd_update_tmo)) {
		// FIXME: ccheck if phase value is ready
		s->dmtd_phase = p->phase_val;
		s->dmtd_phase_valid = p->phase_val_valid;

		tmo_restart(&s->dmtd_update_tmo);
	}
}

void ptpd_netif_linearize_rx_timestamp(struct wr_tstamp *ts, int32_t dmtd_phase,
				       int cntr_ahead, int transition_point,
				       int clock_period)
{
	int trip_lo, trip_hi;
	int phase;

	// "phase" transition: DMTD output value (in picoseconds)
	// at which the transition of rising edge
	// TS counter will appear
	ts->raw_phase = dmtd_phase;

	phase = clock_period - 1 - dmtd_phase;

	// calculate the range within which falling edge timestamp is stable
	// (no possible transitions)
	trip_lo = transition_point - clock_period / 4;
	if (trip_lo < 0)
		trip_lo += clock_period;

	trip_hi = transition_point + clock_period / 4;
	if (trip_hi >= clock_period)
		trip_hi -= clock_period;

	if (inside_range(trip_lo, trip_hi, phase)) {
		// We are within +- 25% range of transition area of
		// rising counter. Take the falling edge counter value as the
		// "reliable" one. cntr_ahead will be 1 when the rising edge
		//counter is 1 tick ahead of the falling edge counter

		ts->nsec -= cntr_ahead ? (clock_period / 1000) : 0;

		// check if the phase is before the counter transition value
		// and eventually increase the counter by 1 to simulate a
		// timestamp transition exactly at s->phase_transition
		//DMTD phase value
		if (inside_range(trip_lo, transition_point, phase))
			ts->nsec += clock_period / 1000;

	}

	ts->phase = phase - transition_point - 1;
	if (ts->phase < 0)
		ts->phase += clock_period;
	ts->phase = clock_period - 1 - ts->phase;
}

#define HAL_CONNECT_RETRIES 1000
#define HAL_CONNECT_TIMEOUT 2000000	/* us */

int ptpd_netif_init()
{
	if (halexp_client_try_connect(HAL_CONNECT_RETRIES, HAL_CONNECT_TIMEOUT)
	    < 0)
		return -1;

	return 0;
}

struct wr_socket *ptpd_netif_create_socket(int sock_type, int flags,
				      struct wr_sockaddr *bind_addr,
				      struct hal_port_state *port)
{
	struct wr_socket *s;
	struct sockaddr_ll sll;
	struct ifreq f;

	int fd;

	//    fprintf(stderr,"CreateSocket!\n");

	if (sock_type != PTPD_SOCK_RAW_ETHERNET)
		return NULL;

	if (!port)
		return NULL;

	fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

	if (fd < 0) {
		perror("socket()");
		return NULL;
	}

	fcntl(fd, F_SETFL, O_NONBLOCK);

	// Put the controller in promiscious mode, so it receives everything
	strcpy(f.ifr_name, bind_addr->if_name);
	if (ioctl(fd, SIOCGIFFLAGS, &f) < 0) {
		perror("ioctl()");
		return NULL;
	}
	f.ifr_flags |= IFF_PROMISC;
	if (ioctl(fd, SIOCSIFFLAGS, &f) < 0) {
		perror("ioctl()");
		return NULL;
	}
	// Find the inteface index
	strcpy(f.ifr_name, bind_addr->if_name);
	ioctl(fd, SIOCGIFINDEX, &f);

	sll.sll_ifindex = f.ifr_ifindex;
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = htons(bind_addr->ethertype);
	sll.sll_halen = ETH_ALEN;

	memcpy(sll.sll_addr, bind_addr->mac, ETH_ALEN);

	if (bind(fd, (struct sockaddr *)&sll, sizeof(struct sockaddr_ll)) < 0) {
		close(fd);
		perror("bind()");
		return NULL;
	}
	// timestamping stuff:

	int so_timestamping_flags = SOF_TIMESTAMPING_TX_HARDWARE |
	    SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;

	struct ifreq ifr;
	struct hwtstamp_config hwconfig;

	/* Since version v3.2-rc1 (commit 850a545bd) linux kernel check whether
	 * field "flags" of struct hwtstamp_config is empty, otherwise ioctl
	 * for SIOCSHWTSTAMP returns -EINVAL */
	memset(&ifr, 0, sizeof(struct ifreq));
	memset(&hwconfig, 0, sizeof(struct hwtstamp_config));

	strncpy(ifr.ifr_name, bind_addr->if_name, sizeof(ifr.ifr_name));

	hwconfig.tx_type = HWTSTAMP_TX_ON;
	hwconfig.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;

	ifr.ifr_data = (char *)&hwconfig;

	if (ioctl(fd, SIOCSHWTSTAMP, &ifr) < 0) {
		perror("SIOCSHWTSTAMP");
		return NULL;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &so_timestamping_flags,
		       sizeof(int)) < 0) {
		perror("setsockopt(SO_TIMESTAMPING)");
		return NULL;
	}

	s = calloc(sizeof(struct wr_socket), 1);
	if (!s)
		return NULL;

	s->if_index = f.ifr_ifindex;

	// get interface MAC address
	if (ioctl(fd, SIOCGIFHWADDR, &f) < 0) {
		perror("ioctl()");
		return NULL;
	}

	memcpy(s->local_mac, f.ifr_hwaddr.sa_data, ETH_ALEN);
	memcpy(&s->bind_addr, bind_addr, sizeof(s->bind_addr));

	s->fd = fd;

	// store the linearization parameters
	s->clock_period = port->clock_period;
	s->phase_transition = port->t2_phase_transition;
	s->dmtd_phase_valid = 0;
	s->dmtd_phase = port->phase_val;

	tmo_init(&s->dmtd_update_tmo, DMTD_UPDATE_INTERVAL);

	return s;
}

int ptpd_netif_close_socket(struct wr_socket *s)
{
	if (!s)
		return 0;

	close(s->fd);
	free(s);
	return 0;
}

static void poll_tx_timestamp(struct wr_socket *sock,
			      struct wr_tstamp *tx_timestamp);

int ptpd_netif_sendto(struct wr_socket *s, struct wr_sockaddr *to, void *data,
		      size_t data_length, struct wr_tstamp *tx_ts)
{
	struct etherpacket pkt;
	struct sockaddr_ll sll;
	int rval;

	if (s->bind_addr.family != PTPD_SOCK_RAW_ETHERNET)
		return -ENOTSUP;

	if (data_length > ETHER_MTU - 8)
		return -EINVAL;

	memset(&pkt, 0, sizeof(struct etherpacket));

	memcpy(pkt.ether.h_dest, to->mac, ETH_ALEN);
	memcpy(pkt.ether.h_source, s->local_mac, ETH_ALEN);
	pkt.ether.h_proto = htons(to->ethertype);

	memcpy(pkt.data, data, data_length);

	size_t len = data_length + sizeof(struct ethhdr);

	if (len < 60)		/* pad to the minimum allowed packet size */
		len = 60;

	memset(&sll, 0, sizeof(struct sockaddr_ll));

	sll.sll_ifindex = s->if_index;
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = htons(to->ethertype);
	sll.sll_halen = 6;

	rval = sendto(s->fd, &pkt, len, 0, (struct sockaddr *)&sll,
		      sizeof(struct sockaddr_ll));

	poll_tx_timestamp(s, tx_ts);

	return rval;
}

#if 0
static void hdump(uint8_t * buf, int size)
{
	int i;
	netif_dbg("Dump: ");
	for (i = 0; i < size; i++)
		netif_dbg("%02x ", buf[i]);
	netif_dbg("\n");
}
#endif

/* Waits for the transmission timestamp and stores it in tx_timestamp (if not null). */
static void poll_tx_timestamp(struct wr_socket *s, struct wr_tstamp *tx_timestamp)
{
	char data[16384];

	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_ll from_addr;
	struct {
		struct cmsghdr cm;
		char control[1024];
	} control;
	struct cmsghdr *cmsg;
	int res;
	uint32_t rtag;

	struct sock_extended_err *serr = NULL;
	struct scm_timestamping *sts = NULL;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = data;
	entry.iov_len = sizeof(data);
	msg.msg_name = (caddr_t) & from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	res = recvmsg(s->fd, &msg, MSG_ERRQUEUE);	//|MSG_DONTWAIT);

	if (tx_timestamp)
		tx_timestamp->correct = 0;

	if (res <= 0)
		return;

	memcpy(&rtag, data + res - 4, 4);

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {

		void *dp = CMSG_DATA(cmsg);

		if (cmsg->cmsg_level == SOL_PACKET
		    && cmsg->cmsg_type == PACKET_TX_TIMESTAMP)
			serr = (struct sock_extended_err *)dp;

		if (cmsg->cmsg_level == SOL_SOCKET
		    && cmsg->cmsg_type == SO_TIMESTAMPING)
			sts = (struct scm_timestamping *)dp;

		//fprintf(stderr, "Serr %x sts %x\n", serr, sts);

		if (serr && sts && tx_timestamp) {
			tx_timestamp->correct = 1;
			tx_timestamp->phase = 0;
			tx_timestamp->nsec = sts->hwtimeraw.tv_nsec;
			tx_timestamp->sec =
			    (uint64_t) sts->hwtimeraw.tv_sec & 0x7fffffff;
		}
	}
}

int ptpd_netif_recvfrom(struct wr_socket *s, struct wr_sockaddr *from, void *data,
			size_t data_length, struct wr_tstamp *rx_timestamp,
			struct hal_port_state *port)
{
	struct etherpacket pkt;
	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_ll from_addr;
	struct {
		struct cmsghdr cm;
		char control[1024];
	} control;
	struct cmsghdr *cmsg;
	struct scm_timestamping *sts = NULL;

	size_t len = data_length + sizeof(struct ethhdr);

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = &pkt;
	entry.iov_len = len;
	msg.msg_name = (caddr_t) & from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	int ret = recvmsg(s->fd, &msg, MSG_DONTWAIT);

	if (ret < 0 && errno == EAGAIN)
		return 0;	// would be blocking
	if (ret == -EAGAIN)
		return 0;

	if (ret <= 0)
		return ret;

	memcpy(data, pkt.data, ret - sizeof(struct ethhdr));

	from->ethertype = ntohs(pkt.ether.h_proto);
	memcpy(from->mac, pkt.ether.h_source, ETH_ALEN);

	if (rx_timestamp)
		rx_timestamp->correct = 0;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {

		void *dp = CMSG_DATA(cmsg);

		if (cmsg->cmsg_level == SOL_SOCKET
		    && cmsg->cmsg_type == SO_TIMESTAMPING)
			sts = (struct scm_timestamping *)dp;

	}

	if (sts && rx_timestamp) {
		int cntr_ahead = sts->hwtimeraw.tv_sec & 0x80000000 ? 1 : 0;
		rx_timestamp->nsec = sts->hwtimeraw.tv_nsec;
		rx_timestamp->sec =
		    (uint64_t) sts->hwtimeraw.tv_sec & 0x7fffffff;

		rx_timestamp->raw_nsec = sts->hwtimeraw.tv_nsec;
		rx_timestamp->raw_ahead = cntr_ahead;

		update_dmtd(s, port);
		if (s->dmtd_phase_valid) {
			ptpd_netif_linearize_rx_timestamp(rx_timestamp,
							  s->dmtd_phase,
							  cntr_ahead,
							  s->phase_transition,
							  s->clock_period);
			rx_timestamp->correct = 1;
		}
	}

	return ret - sizeof(struct ethhdr);
}

int ptpd_netif_adjust_counters(int64_t adjust_sec, int32_t adjust_nsec)
{
	hexp_pps_params_t p;
	int cmd;

	if (!adjust_nsec && !adjust_sec)
		return 0;

	if (adjust_sec && adjust_nsec) {
		pr_error(" FATAL : trying to adjust both the SEC and the NS "
			 "counters simultaneously.\n");
		exit(-1);
	}

	if (adjust_sec) {
		cmd = HEXP_PPSG_CMD_ADJUST_SEC;
		p.adjust_sec = adjust_sec;
	} else {
		cmd = HEXP_PPSG_CMD_ADJUST_NSEC;
		p.adjust_nsec = adjust_nsec;
	}

	if (!halexp_pps_cmd(cmd, &p))
		return 0;

	return -1;
}
