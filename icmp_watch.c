// icmp_watch.c
//
// By Abraham Stolk.
// MIT LICENSE

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>	// for tcsetattr()
#include <netdb.h>	// for getattrinfo()
#include <time.h>	// for nanosleep()
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/select.h>

// NOTE:
// To allow root to use icmp sockets, run:
// $ sysctl -w net.ipv4.ping_group_range="0 0"
// To allow all users to use icmp sockets, run:
// $ sysctl -w net.ipv4.ping_group_range="0 2147483647"

#define ESC	    "\x1B"

#define RESETALL    ESC "[0m"

#define CURSORHOME  ESC "[H"

#define CLEARSCREEN ESC "[H" ESC "[2J" ESC "[3J"

#define FGWHT	    ESC "[1;37m"

#define BGRED	    ESC "[1;41m"

#define BGGRN	    ESC "[1;42m"

static struct termios orig_termios;    // The terminal settings before we modified it.

void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO);	     // Don't echo key presses.
	raw.c_lflag &= ~(ICANON);    // Read by char, not by line.
	raw.c_cc[VMIN] = 0;	     // No minimum nr of chars.
	raw.c_cc[VTIME] = 0;	     // No waiting time.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static int ping_all(int cnt, struct in_addr* destinations, int* response_times, struct timeval* timeout)
{
	static int sequence = 0;
	const int seq = sequence++;        // the sequence number we will use for this run.
	const int waittime = (int) (timeout->tv_sec * 1000000 + timeout->tv_usec);
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (sock < 0)
	{
		perror("socket");
		exit(4);
	}

	for (int i = 0; i < cnt; ++i)
		response_times[i] = -1;	   // what we return if we did not get a reply.

	// Send an ICMP request to each destination.
	struct icmphdr icmp_hdr;
	unsigned char txdata[256];
	const int payloadsz = 10;
	memcpy(txdata + sizeof icmp_hdr, "icmp_watch", payloadsz);    // icmp payload
	for (int i = 0; i < cnt; ++i)
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof addr);
		addr.sin_family = AF_INET;
		addr.sin_addr = destinations[i];

		memset(&icmp_hdr, 0, sizeof icmp_hdr);
		icmp_hdr.type = ICMP_ECHO;
		icmp_hdr.un.echo.id = 0xbeef;

		icmp_hdr.un.echo.sequence = seq;
		memcpy(txdata, &icmp_hdr, sizeof icmp_hdr);
		int rc = sendto(sock, txdata, sizeof icmp_hdr + payloadsz, 0, (struct sockaddr*) &addr, sizeof addr);
		if (rc <= 0)
		{
			perror("Sendto");
			exit(3);
		}
	}

	fd_set read_set;
	memset(&read_set, 0, sizeof read_set);
	FD_SET(sock, &read_set);

	int num_replies = 0;
	while (num_replies < cnt)
	{
		// wait for a reply with a timeout
		const int rc0 = select(sock + 1, &read_set, NULL, NULL, timeout);
		if (rc0 == 0)
		{
			// Timed out without a reply.
			close(sock);
			sock = 0;
			return num_replies;
		}
		else if (rc0 < 0)
		{
			close(sock);
			sock = 0;
			perror("select");
			return -1;
		}

		unsigned char rcdata[256];
		struct icmphdr rcv_hdr;
		struct sockaddr_in other_addr;
		socklen_t other_addr_len = sizeof(other_addr);
		const int rc1 = recvfrom(sock, rcdata, sizeof rcdata, 0, (struct sockaddr*) &other_addr, &other_addr_len);
		if (rc1 <= 0)
		{
			perror("recvfrom");
			exit(5);
		}
		if (rc1 < (int) sizeof(rcv_hdr))
			exit(6);			// ICMP packet was too short.
		memcpy(&rcv_hdr, rcdata, sizeof rcv_hdr);
		assert(rcv_hdr.type == ICMP_ECHOREPLY);
		if (rcv_hdr.un.echo.sequence == seq)	// The sequence number should match, otherwise it's not a valid response.
		{
			const struct in_addr send_addr = other_addr.sin_addr;
			int idx = -1;
			// Look up which host sent us this reply.
			for (int j = 0; j < cnt; ++j)
				if (destinations[j].s_addr == send_addr.s_addr)
					idx = j;
			assert(idx >= 0);
			const int timeleft = (int) (timeout->tv_sec * 1000000 + timeout->tv_usec);
			response_times[idx] = waittime - timeleft;
			num_replies += 1;
		}
	}
	if (close(sock) < 0)
		perror("close(socket)");
	sock = 0;
	return num_replies;
}

// Resolves a set of hostnames to IPv4 numbers.
static int get_ip_addresses(int cnt, char** hosts, struct in_addr* ips)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	for (int i = 0; i < cnt; ++i)
	{
		struct addrinfo* infos;
		const char* host = hosts[i];
		const int rv = getaddrinfo(host, 0, &hints, &infos);
		if (rv)
		{
			fprintf(stderr, "getaddrinfo() %s\n", gai_strerror(rv));
			return i;
		}
		for (struct addrinfo* inf = infos; inf != 0; inf = inf->ai_next)
		{
			struct sockaddr_in* addr = (struct sockaddr_in*) inf->ai_addr;
			ips[i] = addr->sin_addr;
		}
		freeaddrinfo(infos);
	}
	return cnt;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("usage: %s destination_ip [... destination_ip]\n", argv[0]);
		return 1;
	}

	const int cnt = argc - 1;    // Every argument on command line is a hostname.
	struct in_addr dst[cnt];     // The IP numbers of the hosts.
	int response_times[cnt];     // The response time for each host we ping.

	fprintf(stderr, "Looking up %d ip numbers...", cnt);
	fflush(stderr);
	const int num = get_ip_addresses(cnt, argv + 1, dst);
	fprintf(stderr, "DONE\n");
	if (num != cnt)
	{
		fprintf(stderr, "Could not resolve all hostnames. Aborting.\n");
		exit(2);
	}

	enableRawMode();    // Don't echo keyboard characters, don't buffer them.

	int done = 0;
	while (!done)
	{
		// When ESC or Q is pressed, we should terminate.
		char c;
		const int numr = read(STDIN_FILENO, &c, 1);
		if (numr == 1 && (c == 27 || c == 'q' || c == 'Q'))
			done = 1;
		struct timeval timeout = {1, 0};    // seconds, microseconds.
		ping_all(cnt, dst, response_times, &timeout);
		fprintf(stdout, CLEARSCREEN);
		for (int i = 0; i < cnt; ++i)
		{
			const int t = response_times[i];
			fprintf(stdout, "%-20s", argv[1 + i]);
			if (t < 0)
				fprintf(stdout, FGWHT BGRED "NO REPLY" RESETALL "\n");
			else
				fprintf(stdout, FGWHT BGGRN "%5d ms" RESETALL "\n", t / 1000);
		}
		// Pace ourselves.
		const int NS_PER_US = 1000;
		const int us_left = (int) (timeout.tv_sec * 1000000 + timeout.tv_usec);
		struct timespec ts = {0, us_left * NS_PER_US};
		nanosleep(&ts, 0);
	}
	fprintf(stdout, CLEARSCREEN);
	return 0;
}

// vi: tabstop=8
