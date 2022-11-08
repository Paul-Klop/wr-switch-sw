#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define RVLAN_STATUS_FILE "/tmp/rvlan-status"

#ifdef __arm__ /* real wr switch */
#  define IS_WRS 1
#  include <libwr/config.h>
#else /* PC simulation */
#  define IS_WRS 0
   /* empty ones, they are not called anyways */
   static inline char *libwr_cfg_get(char *name) {return NULL;}
   static inline int libwr_cfg_read_file(char *dotconfig) {return 0;}
   static inline int libwr_cfg_dump(FILE *output) {return 0;}
#endif
#define IS_PC (!IS_WRS)

/* This is a set of global variables, that convey program status */
char *prgname; /* argv[0], for my laziness */
int verbose;
int dryrun;
int rvlan_pmask = ~0;
int rvlan_change_pending; /* wrsw_vlans must be called globally */
char *rvlan_radius_secret;
unsigned long rvlan_server_fail_nr;
int rvlan_auth_vlan, rvlan_noauth_vlan, rvlan_obey_dotconfig;
int rvlan_gotsignal;

struct rvlan_radius_server; /* defined later */

struct rvlan_dev {
	char *name;			/* Allocated */
	int portnr;
	int ifindex;
	int up_now;			/* Current (detected) status 0-1 */
	char peer_mac[16];		/* Detected from traffic, as string */
	int poll_fd;			/* Sniffing or radclient output */
	int pid;			/* Running child */
	int fsm_state;			/* During sniff/auth */
	int dotconfig_vlan, chosen_vlan;
	unsigned char mac[ETH_ALEN];	/* Own mac address */
	char radbuffer[10240];
	int radbuffer_size;
	struct rvlan_radius_server *server;
	struct rvlan_dev *next;
};

enum fsm_state {
	RVLAN_GODOWN = 0,
	RVLAN_DOWN,
	RVLAN_JUSTUP,
	RVLAN_SNIFF,
	RVLAN_RADCLIENT,
	RVLAN_AUTH,
	RVLAN_CONFIG,
	RVLAN_CONFIGURED,
	RVLAN_WAIT,
};
char *fsm_names[] = {
	[RVLAN_GODOWN] = "godown",
	[RVLAN_DOWN] = "down",
	[RVLAN_JUSTUP] = "justup",
	[RVLAN_SNIFF] = "sniff",
	[RVLAN_RADCLIENT] = "radclient",
	[RVLAN_AUTH] = "auth",
	[RVLAN_CONFIG] = "config",
	[RVLAN_CONFIGURED] = "configured",
	[RVLAN_WAIT] = "wait",
};

struct rvlan_dev *devs;

/* And this otherlist is for mac addresses (binary) */
struct rvlan_binmac {
	unsigned char mac[ETH_ALEN];
	struct rvlan_binmac *next;
};
struct rvlan_binmac *macs;

/* Use the above */
int rvlan_mac_is_local(uint8_t *address)
{
	struct rvlan_binmac *mac;

	for (mac = macs; mac; mac = mac->next)
		if (!memcmp(address, mac->mac, ETH_ALEN))
			return 1;
	return 0;
}

/* Another list: all the servers */
struct rvlan_radius_server {
	char *addr;
	unsigned long last_failure;
	struct rvlan_radius_server *next;
};
struct rvlan_radius_server *servers;

/* Create a list of servers */
int rvlan_server_mklist(char *names)
{
	struct rvlan_radius_server *new, *last = NULL;
	char *comma;

	while (names[0]) {
		new = calloc(1, sizeof(*new));
		if (!new)
			return -1;
		comma = strchr(names, ',');
		if (comma)
			*comma = '\0';
		new->addr = strdup(names);
		if (comma)
			names = comma + 1;
		else
			names = "";
		/* save at the end of the list, to preserve order */
		if (last) {
			last->next = new;
			last = new;
		} else {
			servers = last = new;
		}
		if (verbose)
			printf("Radius server: \"%s\"\n", new->addr);
	}
	return 0;
}

/* Report a server failure */
void rvlan_server_failed(struct rvlan_dev *dev)
{
	if (dev->server)
		dev->server->last_failure = ++rvlan_server_fail_nr;
}

/* Return the best server */
struct rvlan_radius_server *rvlan_server_get(void)
{
	struct rvlan_radius_server *s, *best;

	for (s = best = servers; s; s = s->next)
		if (s->last_failure < best->last_failure)
			best = s;
	return best;
}


/* Helper for calling the wrs_vlan executable */
int rvlan_change_vlan(struct rvlan_dev *dev)
{
	char cmdstr[128];
	/*
	 *Call wrsw_vlans, But port sets are global so request an
	 * outer action with the change_pending flag.
	 */
	sprintf(cmdstr, "/wr/bin/wrs_vlans --port %i --pvid %i "
		" > /tmp/rvlan-cmd-stdout 2> /tmp/rvlan-cmd-stderr",
		dev->portnr, dev->chosen_vlan);

	if (verbose)
		printf("%srun %s\n", dryrun ? "would " : "", cmdstr);

	if (!dryrun && system(cmdstr)) {
		fprintf(stderr, "%s: can't set vlan %i for %i (%s)\n",
			prgname, dev->chosen_vlan, dev->portnr,
			dev->name);
		return -1;
	}
	rvlan_change_pending++;
	return 0;
}

/* Authentication of a port is a state machine */
int rvlan_fsm(struct rvlan_dev *dev, fd_set *rdset)
{
	int i, sock, pid, status, ret;
	struct packet_mreq req;
        struct ifreq ifr;
	struct sockaddr_ll addr;
	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	static unsigned char frame[512];
	int pipe0[2], pipe1[2];

	/* First of all, process up/down changes */
	if (!dev->up_now && dev->fsm_state != RVLAN_DOWN)
		dev->fsm_state = RVLAN_GODOWN;
	if (dev->up_now && dev->fsm_state == RVLAN_DOWN)
		dev->fsm_state = RVLAN_JUSTUP;

	switch(dev->fsm_state) {
	case RVLAN_DOWN:
		break;

	case RVLAN_JUSTUP:
		/* Turn into temporary auth vlan, but ignore errors */
		dev->chosen_vlan = rvlan_auth_vlan;
		rvlan_change_vlan(dev);
		/* prepare the sniffing socket */
		sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
		if (sock < 0) {
			fprintf(stderr, "%s: can't open raw socket on %s: %s\n",
				prgname, dev->name, strerror(errno));
			return -1;
		}
		memset(&req, 0, sizeof(req));
		req.mr_ifindex = dev->ifindex;
		req.mr_type = PACKET_MR_PROMISC;
		if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
			       &req, sizeof(req)) < 0) {
			fprintf(stderr, "%s: can't make %s promoscuous: %s\n",
				prgname, dev->name, strerror(errno));
			close(sock);
			return -1;
		}
		memset(&addr, 0, sizeof(addr));
		addr.sll_family = AF_PACKET;
		addr.sll_protocol = htons(ETH_P_ALL);
		addr.sll_pkttype = PACKET_MULTICAST; /* maybe redundant? */
		addr.sll_ifindex = dev->ifindex;
		if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			fprintf(stderr, "%s: bind(%s): %s\n",
				prgname, dev->name, strerror(errno));
			close(sock);
			return -1;
		}

		/* and save our own macaddress, to ignore it */
		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, dev->name);
		if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
			fprintf(stderr, "%s: gethwaddr(%s): %s\n",
				prgname, dev->name, strerror(errno));
			close(sock);
			return -1;
		}
		memcpy(dev->mac, &ifr.ifr_ifru.ifru_hwaddr.sa_data, ETH_ALEN);

		dev->poll_fd = sock;
		dev->fsm_state = RVLAN_SNIFF;
		break;

	case RVLAN_SNIFF:
		/* if readable, read it, possibly start auth */
		if (!FD_ISSET(dev->poll_fd, rdset)) {
			break;
		}

		ret = recvfrom(dev->poll_fd, frame, sizeof(frame), MSG_TRUNC,
			       (struct sockaddr *)&from, &fromlen);
		if (ret < 0) {
			fprintf(stderr, "%s: recvfrom(%s): %s\n",
				prgname, dev->name, strerror(errno));
			return -1;
		}
		if (ret < 14) {
			fprintf(stderr, "%s: recvfrom(%s): short frame: %ib\n",
				prgname, dev->name, ret);
			return -1;
		}
		if (0 && verbose) {
			printf("got %02x%02x%02x%02x%02x%02x ",
			       frame[6], frame[7], frame[8],
			       frame[9], frame[10], frame[11]);
			printf("own %02x%02x%02x%02x%02x%02x\n",
			       dev->mac[0], dev->mac[1], dev->mac[2],
			       dev->mac[3], dev->mac[4], dev->mac[5]);
		}
		if (rvlan_mac_is_local(frame + ETH_ALEN))
			break;

		/* success, it seems */
		sprintf(dev->peer_mac, "%02x%02x%02x%02x%02x%02x",
			frame[6], frame[7], frame[8],
			frame[9], frame[10], frame[11]);
		if (verbose)
			printf("recvfrom(%s): %04x-%s\n", dev->name,
			       frame[12] << 8 | frame[13], dev->peer_mac);
		close(dev->poll_fd);
		dev->poll_fd = -1;
		dev->fsm_state = RVLAN_RADCLIENT;
		/* and fall through */

	case RVLAN_RADCLIENT:
		dev->server = rvlan_server_get();
		if (verbose)
			printf("dev %s queries server %s\n", dev->name,
			       dev->server->addr);
		/*
		 * Now, fire the radclient process before changing state
		 */
		if (pipe(pipe0) < 0 || pipe(pipe1) < 0) {
			fprintf(stderr, "%s: fork(): %s\n",
				prgname, strerror(errno));
			close(pipe0[0]); close(pipe0[1]);
			return -1;
		}
		switch(pid = fork()) {
		case -1:
			fprintf(stderr, "%s: fork(): %s\n",
				prgname, strerror(errno));
			return -1;
		case 0: /* child */
			close(pipe0[1]); close(STDIN_FILENO);
			dup(pipe0[0]); close(pipe0[0]);
			close(pipe1[0]); close(STDOUT_FILENO);
			dup(pipe1[1]); close(pipe1[1]);
			close(STDERR_FILENO); dup(STDOUT_FILENO);
			/* radclient <IP> auth <SECRET> */
			execlp("radclient",
			       "radclient", dev->server->addr,
			       "auth", rvlan_radius_secret,
			       NULL);
			fprintf(stderr, "%s: exec(): %s\n",
				prgname, strerror(errno));
			exit(1);

		default:
			close(pipe0[0]); close(pipe1[1]);
			dev->pid = pid;
			break;
		}

		/* Parent: write the query */
		i = sprintf(dev->radbuffer,
			    "User-Name = \"%s\"\n"
			    "User-Password = \"%s\"\n"
			    "NAS-Port = %i\n",
			    dev->peer_mac, dev->peer_mac, dev->portnr);
		write(pipe0[1], dev->radbuffer, i);
		close(pipe0[1]);

		/* save a copy of stdin for diagnostics */
		if (1) {
			FILE *f;
			char fname[40];
			sprintf(fname, "/tmp/radclient-%s-in", dev->name);
			f = fopen(fname, "w");
			if (f) {
				fprintf(f, "%s", dev->radbuffer);
				fclose(f);
			}
		}

		/* and return when data is there to read */
		dev->poll_fd = pipe1[0];
		dev->radbuffer_size = 0;
		dev->fsm_state = RVLAN_AUTH;
		break;

	case RVLAN_AUTH:
		if (!FD_ISSET(dev->poll_fd, rdset)) {
			break;
		}
		/* If we get here, there is some data to read */
		i = read(dev->poll_fd, dev->radbuffer + dev->radbuffer_size,
			 sizeof(dev->radbuffer) - 1 - dev->radbuffer_size);
		if (i < 0) {
			fprintf(stderr, "%s: read(radclient): %s\n",
				prgname, strerror(errno));
			close(dev->poll_fd); dev->poll_fd = -1;
			kill(dev->pid, SIGTERM);
			dev->fsm_state = RVLAN_GODOWN;
			return -1;
		}
		if (i > 0) {
			dev->radbuffer_size += i;
			dev->radbuffer[dev->radbuffer_size] = '\0';
			if (verbose)
				printf("dev %s, got %i bytes so far\n",
				       dev->name, dev->radbuffer_size);
			break;
		}
		/* Got EOF: reap the zombie and see if was in error */
		close(dev->poll_fd);
		dev->poll_fd = -1;
		waitpid(dev->pid, &i, 0);
		if (verbose)
			printf("%s: reaped radclient: 0x%08x\n", dev->name, i);
		dev->pid = 0;

		/* save a copy for diagnostics */
		if (1) {
			FILE *f;
			char fname[40];
			sprintf(fname, "/tmp/radclient-%s-out", dev->name);
			f = fopen(fname, "w");
			if (f) {
				fprintf(f, "%s", dev->radbuffer);
				fclose(f);
			}
		}
		/* See if there was a server error */
		if (WEXITSTATUS(i) != 0
		    && !strncmp(dev->radbuffer, "radclient", 9)) {
			rvlan_server_failed(dev);
			if (verbose)
				printf("%s: server failed\n", dev->name);
			dev->fsm_state = RVLAN_RADCLIENT; /* and go back */
			break;
		}

		/* And parse the result */
		dev->chosen_vlan = rvlan_noauth_vlan;
		if (strstr(dev->radbuffer, "Framed-User")) {
			char *s;
			/* Tunnel-Private-Group-Id:0 = "2984" */
			s = strstr(dev->radbuffer, "Tunnel-Private-Group-Id");
			if (s)
				s = strchr(s, '"');
			if (s)
				sscanf(s+1, "%i", &dev->chosen_vlan);
			if (s && rvlan_obey_dotconfig)
				dev->chosen_vlan = dev->dotconfig_vlan;
		}
		if (verbose)
			printf("dev %s: vlan %i\n", dev->name,
			       dev->chosen_vlan);
		dev->fsm_state = RVLAN_CONFIG;
		break;
	case RVLAN_CONFIG:
		if (rvlan_change_vlan(dev) < 0)
			return -1;
		dev->fsm_state = RVLAN_CONFIGURED;
		break;

	case RVLAN_CONFIGURED:
		break;

	case RVLAN_GODOWN:
		if (dev->poll_fd >= 0)
			close(dev->poll_fd);
		dev->poll_fd = -1;
		dev->peer_mac[0] = '\0';
		dev->chosen_vlan = rvlan_auth_vlan;
		if (rvlan_change_vlan(dev) < 0)
			return -1;
		dev->fsm_state = RVLAN_DOWN;
		if (!dev->pid)
			break;
		/* but if we kill our child we must reap it */
		kill(dev->pid, SIGINT);
		dev->fsm_state = RVLAN_WAIT;
		/* fall through */

	case RVLAN_WAIT:
		pid = waitpid(dev->pid, &status, WNOHANG);
		if (!pid)
			break;
		if (pid < 0) {
			fprintf(stderr, "%s: wait(%i): %s\n", prgname,
				dev->pid, strerror(errno));
			return -1;
		}
		dev->pid = 0;
		dev->fsm_state = RVLAN_DOWN;
		break;
	}
	return 0;
}

/* This function checks all interfaces and saves a list of interesting ones */
static int rvlan_enumerate_devices(char *prefix)
{
	/* We might use socket ioctl calls, or sysfs. This is sysfs-based */
	char path[256];
	char *dirname = "/sys/class/net";
	int ret;
	DIR *d;
	FILE *f;
	struct dirent *ent;
	struct rvlan_dev *dev;
	struct rvlan_binmac *binmac;
	char *s, cfgname[40];
	int portnr, mode_access;

	d = opendir(dirname);
	if (!d) {
		fprintf(stderr, "%s: open(%s): %s\n", prgname, dirname,
			strerror(errno));
		return -1;
	}
	while ( (ent = readdir(d)) ) {
		if (ent->d_type != DT_LNK)
			continue; /* not a symlink --> not an interface */

		/* collect the mac address for all interfaces */
		binmac = calloc(1, sizeof(*binmac));
		if (binmac) {
			sprintf(path, "%s/%s/address", dirname, ent->d_name);
			f = fopen(path, "r");
			if (f) {
				ret = fscanf(f, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
					     binmac->mac+0, binmac->mac+1,
					     binmac->mac+2, binmac->mac+3,
					     binmac->mac+4, binmac->mac+5);
				/* discard all-0 -- e.g. lo interface */
				if (!binmac->mac[0] && !binmac->mac[1] &&
				    !binmac->mac[2] && !binmac->mac[3] &&
				    !binmac->mac[4] && !binmac->mac[5])
					ret = 0;
				if (ret == 6) {
					binmac->next = macs;
					macs = binmac;
				}
				fclose(f);
			}
			if (macs != binmac) /* not enqueued */
				free(binmac);
		}

		if (strncmp(ent->d_name, prefix, strlen(prefix)))
			continue; /* no match -> we do not care */

		if (IS_WRS) {
			/* Check configuration of this port */
			portnr = -1;
			sscanf(ent->d_name + strlen(prefix), "%d", &portnr);
			if (portnr < 0) {
				fprintf(stderr, "%s: \"%s\" has no number\n",
					prgname, ent->d_name);
				exit(1);
			}
			sprintf(cfgname, "VLANS_PORT%02d_MODE_ACCESS",
				portnr);
			mode_access = libwr_cfg_get(cfgname) != NULL;
			if (!mode_access && verbose)
				printf("Interface \"%s\": not access mode\n",
				       ent->d_name);
			if (!mode_access)
				continue; /* likely trunk */
			/* Note: port name is 1..18, but bits are 0..17 */
			if ((rvlan_pmask & (1 << (portnr - 1))) == 0) {
				if (verbose)
					printf("Interface \"%s\": "
					       "disabled by port mask\n",
					       ent->d_name);
				continue;
			}
		}

		dev = calloc(1, sizeof(*dev));
		dev->name = strdup(ent->d_name);
		if (macs) /* last allocated is ours */
			memcpy(dev->mac, macs->mac, ETH_ALEN);
		dev->portnr = portnr;
		dev->poll_fd = -1;
		if (IS_WRS) {
			sprintf(cfgname, "VLANS_PORT%02d_VID", portnr);
			s = libwr_cfg_get(cfgname);
			if (s)
				sscanf(s, "%i", &dev->dotconfig_vlan);
		}

		/* read ifindex from sysfs -- instead of ioctl(SIOCGIFINDEX) */
		sprintf(path, "%s/%s/ifindex", dirname, dev->name);
		f = fopen(path, "r");
		if (!f) {
			fprintf(stderr, "%s: open(%s): %s\n", prgname, path,
				strerror(errno));
			continue;
		}
		ret = fscanf(f, "%i", &dev->ifindex);
		fclose(f);
		if (ret != 1) {
			fprintf(stderr, "%s: %s: wrong data\n", prgname, path);
			continue;
		}

		dev->next = devs;
		devs = dev;
	}
	closedir(d);
	return 0;
}

/* This prepares polling using netlink, so we get notification on change */
int rvlan_prepare_poll(void)
{
	int sock;
	struct sockaddr_nl addr = {};

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0) {
		fprintf(stderr, "%s: socket(netlink): %s\n", prgname,
			strerror(errno));
		return -1;
	}

	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid ();
	addr.nl_groups = RTMGRP_LINK;

	if (bind (sock, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		fprintf(stderr, "%s: bind(netlink): %s\n", prgname,
			strerror(errno));
		return -1;
	}
	return sock;
}

/* Before looking at netlink, deal with what is already up */
int rvlan_initial_check(void)
{
	struct rvlan_dev *dev;
	char path[64], upbuf[16];
	FILE *f;
	int ret, up = 1;

	for (dev = devs; dev; dev = dev->next) {
		if (verbose)
			printf("Check %s: ", dev->name);
		sprintf(path, "/sys/class/net/%s/operstate", dev->name);
		f = fopen(path, "r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", prgname, path,
				strerror(errno));
			continue;
		}
		ret = fscanf(f, "%s", upbuf);
		up = (upbuf[0] == 'u');
		fclose(f);
		if (ret != 1) {
			/* Hmmm... read() may return -EINVAL if down */
			up = 0;
		}
		dev->up_now = up;
		if (up)
			dev->fsm_state = RVLAN_JUSTUP;
		if (verbose)
			printf("%s\n", up ? "up" : "down");
	}
	return 0;
}

/* Use netlink to get notifications */
int rvlan_process_netlink(int sock)
{
	struct rvlan_dev *dev;
	static char buf[8192];
	struct iovec iov = {buf, sizeof(buf)};
	struct sockaddr_nl peer;
	struct msghdr m = { &peer, sizeof(peer), &iov, 1};
	struct nlmsghdr *h;
	struct ifinfomsg *ifi;
	int ret, index, up;

	ret = recvmsg (sock, &m, 0);
	if (ret < 0) {
		fprintf(stderr, "%s: recvmsg(netlink): %s\n", prgname,
			strerror(errno));
		return -1;
	}

	for (h = (struct nlmsghdr *)buf;
	     NLMSG_OK(h, ret);
	     h = NLMSG_NEXT (h, ret)) {

		if (h->nlmsg_type == NLMSG_DONE)
			break;

		if (h->nlmsg_type == NLMSG_ERROR) {
			fprintf(stderr, "%s: netlink message error\n",
				prgname);
			continue;
		}
		if (h->nlmsg_type == RTM_NEWLINK) {
			ifi = NLMSG_DATA (h);
			index = ifi->ifi_index;
			up = (ifi->ifi_flags & IFF_RUNNING) ? 1 : 0;

			/* Look for this index */
			for (dev = devs; dev; dev = dev->next)
				if (dev->ifindex == index)
					break;
			if (!dev) {
				if (verbose)
					printf("ignored ifindex %i event\n",
					       index);
				continue;
			}
			dev->up_now = up; /* fsm will process it */
			continue;
		}
		fprintf(stderr, "%s: unexpected message %i\n", prgname,
			h->nlmsg_type);
	}
	return 0;
}

/* Wait for a netlink message and run the state machines */
int rvlan_poll_devices(int sock)
{
	struct rvlan_dev *dev;
	fd_set rdset;
	/* run once a minute, but be faster if verbose */
	struct timeval to = {60, 0};
	int ret, nrfd;

	FD_ZERO(&rdset);
	FD_SET(sock, &rdset);
	nrfd = sock + 1;

	for (dev = devs; dev; dev = dev->next) {
		/* If needed, shorten timeout */
		if (dev->fsm_state != RVLAN_DOWN &&
		    dev->fsm_state != RVLAN_SNIFF &&
		    dev->fsm_state != RVLAN_CONFIGURED) {
			to.tv_sec = 0;
			to.tv_usec = 500 * 1000;
		}
		/* If needed, wait for this fd too */
		if (dev->poll_fd >= 0) {
			FD_SET(dev->poll_fd, &rdset);
			if (dev->poll_fd >= nrfd)
				nrfd = dev->poll_fd + 1;
		}
	}

	/* Wait for an event */
	ret = select(nrfd, &rdset, NULL, NULL, &to);
	switch(ret) {
	case -1:
		if (errno == EINTR)
			return 0;
		fprintf(stderr, "%s: select(): %s\n", prgname,
			strerror(errno));
		return -1;
	case 0:
	default:
		/* go ahead with descriptor and fsm */ ;
	}

	if (FD_ISSET(sock, &rdset))
		rvlan_process_netlink(sock);
	/* This netlink event may fire a fsm action, so call them all */
	for (dev = devs; dev; dev = dev->next) {
		int prevstate = dev->fsm_state;
		rvlan_fsm(dev, &rdset);
		if (verbose && dev->fsm_state != prevstate)
			printf("FSM: %s: %s -> %s\n", dev->name,
			       fsm_names[prevstate],
			       fsm_names[dev->fsm_state]);
	}
	return 0;
}

/* This functions scans the interfaces, and creates wrsw_vlans parameters */
void rvlan_reconfigure_vlans(void)
{
	/* FIXME */
}

/* Save status for diagnostics, on SIGUSR1 */
void rvlan_dump_status(void)
{
	struct rvlan_dev *dev;

	if (verbose)
		printf("Dumping status on request\n");
	FILE *f = fopen(RVLAN_STATUS_FILE,"w");
	if (!f)
		return;
	for (dev = devs; dev; dev = dev->next) {
		char macstr[32];
		sprintf(macstr, "%02x%02x%02x%02x%02x%02x",
			dev->mac[0], dev->mac[1], dev->mac[2],
			dev->mac[3], dev->mac[4], dev->mac[5]);

		fprintf(f, "%s (%s <-> %s): state %s, vlan %i, pid %i, fd %i\n",
			dev->name, macstr, dev->peer_mac,
			fsm_names[dev->fsm_state],
			dev->chosen_vlan, dev->pid, dev->poll_fd);
	}
	fclose(f);
}
/* Do it again, on SIGUSR2 */
void rvlan_reauth(void)
{
	struct rvlan_dev *dev;
	for (dev = devs; dev; dev = dev->next)
		dev->fsm_state = RVLAN_JUSTUP;
}

void rvlan_sighandler(int signum)
{
	rvlan_gotsignal = signum;
	signal(signum, rvlan_sighandler);
}

/* And a main to keep it all together */
int main(int argc, char **argv)
{
	int sock;
	char *rvlan_dev_prefix = "wri";
	char *dotconfig = "/wr/etc/dot-config";
	char *servers;

	prgname = argv[0];
	verbose = getenv("RVLAN_VERBOSE") != NULL;
	if (verbose)
		printf("Enable verbose\n");
	dryrun = getenv("RVLAN_DRYRUN") != NULL;
	if (dryrun)
		printf("Enable dryrun\n");

	setlinebuf(stdout);

	signal(SIGUSR1, rvlan_sighandler);
	signal(SIGUSR2, rvlan_sighandler);

	if (getenv("RVLAN_DEV_PREFIX"))
		rvlan_dev_prefix = getenv("RVLAN_DEV_PREFIX");

	/* Access dot-config and retrieve global parameters */
	if (IS_WRS) {
		int i, mask;
		char *s;

		i = libwr_cfg_read_file(dotconfig);
		if (i < 0) {
			fprintf(stderr, "%s: %s:%i: can't parse\n",
				argv[0], dotconfig, -1);
			exit(1);
		}
		s = libwr_cfg_get("RVLAN_PMASK");
		if (s) {
			i = sscanf(s, "%x", &mask);
			if (i != 1) {
				fprintf(stderr, "%s: can't parse config %s\n",
					argv[0], s);
				exit(1);
			}
			if (verbose)
				printf("Pmask = 0x%08x\n", mask);
			rvlan_pmask = mask;
		}

		s = libwr_cfg_get("RVLAN_AUTH_VLAN");
		if (s) {
			sscanf(s, "%i", &rvlan_auth_vlan);
		} else {
			fprintf(stderr, "%s: missing AUTH_VLAN config\n",
				argv[0]);
			exit(1);
		}

		s = libwr_cfg_get("RVLAN_NOAUTH_VLAN");
		if (s) {
			sscanf(s, "%i", &rvlan_noauth_vlan);
		} else {
			fprintf(stderr, "%s: missing NOAUTH_VLAN config\n",
				argv[0]);
			exit(1);
		}

		s = libwr_cfg_get("RVLAN_OBEY_DOTCONFIG");
		if (s && s[0]=='y')
			rvlan_obey_dotconfig = 1;

		servers = libwr_cfg_get("RVLAN_RADIUS_SERVERS");
		rvlan_radius_secret = libwr_cfg_get("RVLAN_RADIUS_SECRET");
		if (!servers || !rvlan_radius_secret) {
			fprintf(stderr, "%s: missing dot-config items\n",
				argv[0]);
			exit(1);
		}
		rvlan_server_mklist(servers);
	}

	/* Create a list of "wr" devices */
	if (rvlan_enumerate_devices(rvlan_dev_prefix) < 0)
		exit(1);

	if (rvlan_change_pending)
		rvlan_reconfigure_vlans();

	/* Open a netlink socket to monitor state changes */
	if ((sock = rvlan_prepare_poll()) < 0)
		exit(1);

	/* Check which ones are already up, and handle them */
	if (rvlan_initial_check() < 0)
		exit(1);

	while (1) {
		/* Use netlink/sniff/radius to run the state machines */
		rvlan_poll_devices(sock);
		if (rvlan_change_pending)
			rvlan_reconfigure_vlans();

		if (rvlan_gotsignal == SIGUSR1) {
			rvlan_dump_status();
			rvlan_gotsignal = 0;
		}
		if (rvlan_gotsignal == SIGUSR2) {
			FILE *f;
			verbose = !verbose;
			f = fopen("/tmp/rvlan-is-verbose", "w");
			if (f) {
				fprintf(f, "%i\n", verbose);
				fclose(f);
			}
			rvlan_reauth();
			rvlan_gotsignal = 0;
		}
	}
}
