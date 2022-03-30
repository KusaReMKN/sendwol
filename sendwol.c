/*-
 * SPDX short identifier: BSD-2-Clause
 *
 * Copyright 2022 KusaReMKN <https://github.com/KusaReMKN>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <locale.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * 48-bit MAC address
 */
struct macaddr {
#define MACADDR_LEN	6
	uint8_t macaddr[MACADDR_LEN];
} __attribute__((__packed__));

/*
 * Wake-on-LAN magic packet without password field
 */
struct magic_packet {
	struct macaddr mp_sync;
#define MP_TARGET_REP	16
	struct macaddr mp_target[MP_TARGET_REP];
} __attribute__((__packed__));

/*
 * database list
 */
struct dblist {
	struct dblist *dbl_next;
	const char *dbl_path;
};

static int brd_connect(int domain, const char *restrict servname,
		const char *restrict interface);
static struct magic_packet *build_magicpacket(const struct macaddr *target);
static void free_dblist(struct dblist *head);
static char *home_sendwol(void);
static struct macaddr *lookup_dblist(const struct dblist *restrict head,
		const char *restrict hostname);
static struct macaddr *mac_aton(const char *a);
static const char *multicast_if(int fd, int domain, const char *interface);
static int push_dblist(struct dblist **restrict headp,
		const char *restrict path);
static int udp_connect(int domain, const char *restrict nodename,
		const char *restrict servname, const char *restrict interface);
static void usage(void);

int
main(int argc, char *argv[])
{
	int c, fd, yes;
	int broadcast, domain, forcev4, forcev6;
	char *homesendwol, *interface, *nodename, *servname;
	struct dblist *list;
	struct macaddr *target;
	struct magic_packet *packet;
	ssize_t written;

	(void)setlocale(LC_ALL, "");

	list = NULL;
	if (push_dblist(&list, "/etc/ethers") == -1)
		goto dberror;

	homesendwol = home_sendwol();
	if (homesendwol != NULL) {
		if (push_dblist(&list, homesendwol) == -1) {
dberror:		warn(NULL);
			warnx("database files are not available");
			free_dblist(list);
			list = NULL;
		}
	} else {
		warn(NULL);
		warnx("~/.sendwol is not available");
	}

	forcev4 = 0;
	forcev6 = 0;
	broadcast = 0;
	interface = NULL;
	nodename = NULL;
	servname = "9";
	while ((c = getopt(argc, argv, "46ba:h:f:i:p:s:")) != -1)
		switch (c) {
		case '4':
			forcev4 = 1;
			break;
		case '6':
			forcev6 = 1;
			break;
		case 'b':
			broadcast = 1;
			break;
		case 'a':
		case 'h':
			nodename = optarg;
			break;
		case 'f':
			if (list == NULL)
				break;
			if (push_dblist(&list, optarg) == -1) {
				warn(NULL);
				warnx("database files are not available");
				free_dblist(list);
				list = NULL;
			}
			break;
		case 'i':
			interface = optarg;
			break;
		case 'p':
		case 's':
			servname = optarg;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (forcev4 && forcev6)
		errx(1, "-4 and -6 cannot be used simultaneously");
	domain = forcev4 ? AF_INET : forcev6 ? AF_INET6 : AF_UNSPEC;

	if (broadcast && nodename != NULL)
		errx(1, "-b and -a (or -h) cannot be used simultaneously");
	if (nodename == NULL)
		broadcast = 1;

	fd = broadcast
		? brd_connect(domain, servname, interface)
		: udp_connect(domain, nodename, servname, interface);
	yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1)
		err(1, "SO_BROADCAST");

	for (; *argv != NULL; argv++) {
		target = mac_aton(*argv);
		if (target == NULL) {
			target = lookup_dblist(list, *argv);
			if (target == NULL) {
				warnx("%s: invalid MAC address"
						" or unknown host", *argv);
				continue;
			}
		}
		packet = build_magicpacket(target);
		written = write(fd, packet, sizeof(*packet));
		if (written != sizeof(*packet))
			err(1, "write");
	}

	free_dblist(list);

	if (close(fd) == -1)
		err(1, "close");

	return 0;
}

static int
brd_connect(int domain, const char *restrict servname,
		const char *restrict interface)
{
	int fd, port;
	char *cause, *endptr;
	struct servent *sp;
	struct ifaddrs *ifa, *res;
	socklen_t sslen;
	struct sockaddr_storage ss;

	if (domain != AF_UNSPEC && domain != AF_INET && domain != AF_INET6)
		errno = EAFNOSUPPORT, err(1, "brd_connect");

	port = htons((uint16_t)strtol(servname, &endptr, 10));
	if (*endptr != '\0') {
		sp = getservbyname(servname, "udp");
		if (sp == NULL)
			errx(1, "unknown service: %s", servname);
		port = sp->s_port;
	}

	if (getifaddrs(&res) == -1)
		err(1, "getifaddrs");
	errno = 0;
	for (ifa = res; ifa != NULL; ifa = ifa->ifa_next) {
		if (interface != NULL
				&& strcmp(interface, ifa->ifa_name) != 0
				|| ifa->ifa_broadaddr == NULL
				|| ifa->ifa_broadaddr->sa_family != AF_INET
				&& ifa->ifa_broadaddr->sa_family != AF_INET6
				|| domain != AF_UNSPEC
				&& ifa->ifa_broadaddr->sa_family != domain)
			continue;

		fd = socket(ifa->ifa_broadaddr->sa_family, SOCK_DGRAM, 0);
		if (fd == -1) {
			cause = "socket";
			continue;
		}

		sslen = ifa->ifa_broadaddr->sa_family == AF_INET
			? sizeof(struct sockaddr_in)
			: sizeof(struct sockaddr_in6);
		memcpy(&ss, ifa->ifa_broadaddr, sslen);
		if (ss.ss_family == AF_INET)
			((struct sockaddr_in *)&ss)->sin_port = port;
		else
			((struct sockaddr_in6 *)&ss)->sin6_port = port;

		if (connect(fd, (struct sockaddr *)&ss, sslen) == -1) {
			if (close(fd) == -1)
				err(1, "close");
			cause = "connect";
			continue;
		}
		break;	/* SUCCESS */
	}
	if (ifa == NULL && errno != 0)
		err(1, "%s", cause);
	if (ifa == NULL)
		errno = ENXIO, err(1, "%s", interface);
	freeifaddrs(res);

	return fd;
}

static struct magic_packet *
build_magicpacket(const struct macaddr *target)
{
	static struct magic_packet mp;
	int i;

	memset(&mp.mp_sync, 0xFF, sizeof(mp.mp_sync));
	for (i = 0; i < MP_TARGET_REP; i++)
		memcpy(mp.mp_target + i, target, sizeof(mp.mp_target[0]));

	return &mp;
}

static void
free_dblist(struct dblist *head)
{
	struct dblist *tmp;

	while ((tmp = head) != NULL) {
		head = head->dbl_next;
		free(tmp);
	}
}

static char *
home_sendwol(void)
{
#define HOME_SENDWOL	"%s/.sendwol"
	int length;
	char *buffer, *home;

	home = getenv("HOME");
	if (home == NULL)
		return NULL;

	length = snprintf(NULL, 0, HOME_SENDWOL, home) + 1;
	buffer = (char *)malloc(length);
	if (buffer == NULL)
		return NULL;

	snprintf(buffer, length, HOME_SENDWOL, home);

	return buffer;
}

static struct macaddr *
lookup_dblist(const struct dblist *restrict head,
		const char *restrict hostname)
{
	int elem;
	FILE *fp;
	struct macaddr *res;
	char line[BUFSIZ];	/* I have no idea how much buffer is needed */
	char host[MAXHOSTNAMELEN];	/* This is also */
	char addrstr[18];

	for (; head != NULL; head = head->dbl_next) {
		fp = fopen(head->dbl_path, "r");
		if (fp == NULL)
			continue;
		while (fgets(line, sizeof(line), fp) != NULL) {
			if (line[0] == '#')
				continue;
			elem = sscanf(line, "%s%s", addrstr, host);
			if (elem < 2)
				continue;
			if (strcmp(hostname, host) == 0) {
				(void)fclose(fp);
				return mac_aton(addrstr);	/* found */
			}
		}
		(void)fclose(fp);
	}

	return NULL;
}

static struct macaddr *
mac_aton(const char *a)
{
	static struct macaddr addr;
	int i;
	unsigned int tmp[MACADDR_LEN];

	i = sscanf(a, "%x%*[-:]%x%*[-:]%x%*[-:]%x%*[-:]%x%*[-:]%x",
			tmp + 0, tmp + 1, tmp + 2, tmp + 3, tmp + 4, tmp + 5);
	if (i != 6)
		return NULL;
	
	for (i = 0; i < MACADDR_LEN; i++)
		addr.macaddr[i] = (uint8_t)tmp[i];

	return &addr;
}

static const char *
multicast_if(int fd, int domain, const char *interface)
{
	unsigned int ifindex;
	struct ifreq ifr;
	struct in_addr addr;

	if (domain != AF_INET && domain != AF_INET6)
		return errno = EAFNOSUPPORT, "multicast_if";

	if (domain == AF_INET) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFADDR, &ifr) == -1)
			return interface;
		addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
		if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
					&addr, sizeof(addr)) == -1)
			return "IP_MULTICAST_IF";
	} else {
		ifindex = if_nametoindex(interface);
		if (ifindex == 0)
			return interface;
		if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
					&ifindex, sizeof(ifindex)) == -1)
			return "IPV6_MULTICAST_IF";
	}

	return NULL;
}

static int
push_dblist(struct dblist **restrict headp, const char *restrict path)
{
	struct dblist *temp;

	temp = (struct dblist *)malloc(sizeof(*temp));
	if (temp == NULL)
		return -1;

	temp->dbl_next = *headp;
	temp->dbl_path = path;
	*headp = temp;

	return 0;
}

static int
udp_connect(int domain, const char *restrict nodename,
		const char *restrict servname, const char *restrict interface)
{
	int error, fd;
	const char *cause;
	struct addrinfo *ai, *res, hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = domain;
	hints.ai_socktype = SOCK_DGRAM;
	error = getaddrinfo(nodename, servname, &hints, &res);
	if (error != 0)
		errx(1, "getaddrinfo: %s", gai_strerror(error));

	for (ai = res; ai != NULL; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd == -1) {
			cause = "socket";
			continue;
		}

		if (interface != NULL) {
			cause = multicast_if(fd, ai->ai_family, interface);
			if (cause != NULL)
				continue;
		}

		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
			if (close(fd) == -1)
				err(1, "close");
			cause = "connect";
			continue;
		}
		break;	/* SUCCESS */
	}
	if (ai == NULL)
		err(1, "%s", cause);
	freeaddrinfo(res);

	return fd;
}

static void
usage(void)
{
	(void)fprintf(stderr,
			"usage: sendwol [-4 | -6]"
			" [-b | -a address | -h hostname] [-f file]\n"
			"               [-i interface] [-p port | -s service]"
			" target ...\n");
	exit(1);
}
