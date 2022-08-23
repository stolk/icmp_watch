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
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <getopt.h>		// for getopt_long()

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

struct destination_info {
	int response_time;		// Response time in milliseconds
	int error;				// errno if there was an error, 0 otherwise
	struct sockaddr *address;			// pointer to either an sockadder_in or sockaddr_in6 struct
};

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

static int ping_all(int cnt, struct destination_info* destinations, struct timeval* timeout)
{
	static int sequence = 0;
	const int seq = sequence++;        // the sequence number we will use for this run.
	const int waittime = (int) (timeout->tv_sec * 1000000 + timeout->tv_usec);
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	int sock6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
	if (sock < 0)
	{
		perror("socket");
		exit(4);
	}
	if (sock6 < 0) {
		perror("v6 socket");
		exit(6);
	}

	for (int i = 0; i < cnt; ++i) {
		destinations[i].response_time = -1;	  	// what we return if we did not get a reply.
		destinations[i].error = 0;			  	// what we return if there wasn’t an error
	}
	
	// Prepare an ICMP(v4 and v6) request for each destination.
	struct icmphdr icmp_hdr;
	unsigned char txdata[256];
	const int payloadsz = 10;
	memcpy(txdata + sizeof icmp_hdr, "icmp_watch", payloadsz);    // icmp payload
	
	struct icmp6_hdr icmp6_hdr;
	unsigned char tx6data[256];
	memcpy(tx6data + sizeof icmp6_hdr, "icmp_watch", payloadsz);    // icmpv6 payload

	// A fork in the road, as the code if different for v4 and v6
	for (int i = 0; i < cnt; ++i) {
		if (destinations[i].address->sa_family == AF_INET) {
			struct sockaddr_in addr;
			memset(&addr, 0, sizeof addr);
			addr.sin_family = AF_INET;
			addr.sin_addr = ((struct sockaddr_in *) destinations[i].address)->sin_addr;

			memset(&icmp_hdr, 0, sizeof icmp_hdr);
			icmp_hdr.type = ICMP_ECHO;
			icmp_hdr.un.echo.id = 0xbeef;

			icmp_hdr.un.echo.sequence = seq;
			memcpy(txdata, &icmp_hdr, sizeof icmp_hdr);
			int rc = sendto(sock, txdata, sizeof icmp_hdr + payloadsz, 0, (struct sockaddr*) &addr, sizeof addr);
			if (rc <= 0)
			{
				destinations[i].error = errno;
			}
		} else if (destinations[i].address->sa_family == AF_INET6) {
			struct sockaddr_in6 addr;
			memset(&addr, 0, sizeof addr);
			addr.sin6_family = AF_INET6;
			addr.sin6_addr = ((struct sockaddr_in6 *) destinations[i].address)->sin6_addr;

			memset(&icmp6_hdr, 0, sizeof icmp6_hdr);
			icmp6_hdr.icmp6_type = ICMP6_ECHO_REQUEST;
			icmp6_hdr.icmp6_id = 0xbeef;

			icmp6_hdr.icmp6_seq = seq;
			memcpy(tx6data, &icmp6_hdr, sizeof icmp6_hdr);
			int rc = sendto(sock6, tx6data, sizeof icmp6_hdr + payloadsz, 0, (struct sockaddr*) &addr, sizeof addr);
			if (rc <= 0)
			{
				destinations[i].error = errno;
			}
		}
	}

	int highestfd;
	if (sock > sock6)
		highestfd = sock;
	else
		highestfd = sock6;

	int num_replies = 0;
	while (num_replies < cnt)
	{
		// Set the file descriptor set for select()
		// According to the man page for select(), this should be done every iteration of a loop
		fd_set read_set;
		FD_ZERO(&read_set);
		FD_SET(sock, &read_set);
		FD_SET(sock6, &read_set);
		
		// wait for a reply with a timeout
		const int rc0 = select(highestfd + 1, &read_set, NULL, NULL, timeout);
		if (rc0 == 0)
		{
			// Timed out without a reply.
			close(sock);
			close(sock6);
			sock = 0;
			sock6 = 0;
			return num_replies;
		}
		else if (rc0 < 0)
		{
			close(sock);
			close(sock6);
			sock = 0;
			sock6 = 0;
			perror("select");
			return -1;
		}
		
		if (FD_ISSET(sock, &read_set)) {
			/* IPv4 code */
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
					if(((struct sockaddr_in* ) destinations[j].address)->sin_addr.s_addr == send_addr.s_addr) {
						idx = j;
						break;
					}
				assert(idx >= 0);
				const int timeleft = (int) (timeout->tv_sec * 1000000 + timeout->tv_usec);
				destinations[idx].response_time = waittime - timeleft;
				num_replies += 1;
			}
		}
		if (FD_ISSET(sock6, &read_set)) {
			/* IPv6 code */
			unsigned char rcdata6[256];
			struct icmp6_hdr rcv6_hdr;
			struct sockaddr_in6 other_addr6;
			socklen_t other_addr6_len = sizeof(other_addr6);

			const int rc1v6 = recvfrom(sock6, rcdata6, sizeof rcdata6, 0, (struct sockaddr*) &other_addr6, &other_addr6_len);
			if (rc1v6 <= 0) {
				perror("recvfrom (v6)");
				exit(5);
			}
			if (rc1v6 < (int) sizeof(rcv6_hdr))
				exit(8);
			
			memcpy(&rcv6_hdr, rcdata6, sizeof rcv6_hdr);
			assert(rcv6_hdr.icmp6_type == ICMP6_ECHO_REPLY);
			if (rcv6_hdr.icmp6_seq == seq) {
				// The sequence number should match, otherwise it's not a valid response
				const struct in6_addr send6_addr = other_addr6.sin6_addr;
				int idx = -1;
				// Search for which host sent the reply
				for (int j = 0; j < cnt; ++j) {
					// For IPv6 we have to compare all 16 unsigned chars of the address (128 bits)
					int thisone = 1;
					for(int x = 0; x < 16; x++) {
						if(((struct sockaddr_in6* ) destinations[j].address)->sin6_addr.s6_addr[x] != send6_addr.s6_addr[x]) {
							// One of the bytes don't match, so it isn't this host
							thisone = 0;
							break;
						}
					}
					if (thisone) {
						idx = j;
						break;
					}
				}
				assert(idx >= 0);
				const int timeleft = (int) (timeout->tv_sec * 1000000 + timeout->tv_usec);
				destinations[idx].response_time = waittime - timeleft;
				num_replies += 1;
			}
		}
	}
	
	if (close(sock) < 0)
		perror("close(socket)");
	sock = 0;
	
	if (close(sock6) < 0)
		perror("close(socket for v6)");
	sock6 = 0;
	return num_replies;
}

// Resolves a set of hostnames to IPv4 or IPv6 addresses
static int get_ip_addresses(int cnt, char** hosts, int args_left, struct destination_info* destinations)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	for (int i = 0; i < cnt; ++i)
	{
		struct addrinfo* infos;
		const char* host = hosts[args_left + i];
		const int rv = getaddrinfo(host, 0, &hints, &infos);
		if (rv)
		{
			fprintf(stderr, "getaddrinfo() %s\n", gai_strerror(rv));
			return i;
		}
		for (struct addrinfo* inf = infos; inf != 0; inf = inf->ai_next)
		{
			int haveaddr = 0;
			// Check if the returned address was v4 or v6
 			switch(inf->ai_family) {
				case AF_INET: {
					// Copy the inf->ai_addr structure
					// It'll need to be freed later with something like free(destinations->address)
					struct sockaddr_in* copied_ai_addr = malloc(sizeof(struct sockaddr_in));
					if(copied_ai_addr == NULL) {
						// Check we actually got some memory, if not, exit
						fprintf(stderr, "Failed allocate memory for copied_ai_addr!\n");
						exit(EXIT_FAILURE);
					}
					memcpy(copied_ai_addr, inf->ai_addr, sizeof(struct sockaddr_in));
					destinations[i].address = (struct sockaddr*) copied_ai_addr;
					haveaddr = 1;
					break;
				}
				case AF_INET6: {
					// Copy the inf->ai_addr structure
					// It'll need to be freed later with something like free(destinations->address)
					struct sockaddr_in6* copied_ai_addr = malloc(sizeof(struct sockaddr_in6));
					if(copied_ai_addr == NULL) {
						// Check we actually got some memory, if not, exit
						fprintf(stderr, "Failed allocate memory for copied_ai_addr!\n");
						exit(EXIT_FAILURE);
					}
					memcpy(copied_ai_addr, inf->ai_addr, sizeof(struct sockaddr_in6));
					destinations[i].address = (struct sockaddr*) copied_ai_addr;
					haveaddr = 1;
					break;
				}
				default: {
					fprintf(stderr, "For %s, we got an address type (%i) that wasn't AF_INET (%i) or AF_INET6 (%i)", host, inf->ai_family, AF_INET, AF_INET6);
				}
			}
			if (haveaddr) {
				// Break out of the loop if we have an address
				break;
			}
		}
		freeaddrinfo(infos);
	}
	return cnt;
}

void print_help(char *progname) {
	// Restyled the help to more closely match the --help text for mv, etc.
	printf("Usage: %s [option]... destination_ip...\n"
		   "Send batch requests for ICMP and show the results\nPress q or escape to exit\n\n"
		   "  -i, --interval=INTERVAL\tspecify how long in seconds to wait for replies (real numbers, e.g. 1.5 are allowed)\n"
		   "  -h, --help\t\t\tshow this help\n", progname);
}

int main(int argc, char* argv[])
{
	struct timeval default_timeout = {1, 0};    // seconds, microseconds.
	
	// Parse command line options (we'll break out of the loop)
	while(1) {
		int option_index = 0;
		int c;
		static struct option long_options[] = {
			{"interval", required_argument, 0, 'i'},
			{"help", no_argument, 0, 'h'},
			{0, 0, 0, 0} 	// default option (for unknown options)
		};
		
		c = getopt_long(argc, argv, "i:h", long_options, &option_index);
		
		if (c == -1) {
			break; // no more options
		}
		
		//printf("Right now c is %i (%c)\n", c, c);
		switch(c) {
			case 0:
				printf("Unknown option %s\n", long_options[option_index].name);
				break;
			case 'i':
				// Convert the argument to --interval to a (double) float, then convert it to a timespec struct
				if(optarg) {
					double interval_double;
					errno = 0;		// Set errno to 0 so we can detect errors
					interval_double = strtod(optarg, NULL);
					if(errno != 0) {
						perror("Converting interval");
						exit(1);
					}
					// Set the specified interval as the default timeout
					default_timeout.tv_sec = (int) interval_double;
					default_timeout.tv_usec = (int) (interval_double * 1000000);
					break;
				} else {
					fprintf(stderr, "-i/--interval needs an argument\n");
					exit(1);
				}
			case 'h':
				print_help(argv[0]);
				exit(0);
			default:
				//fprintf(stderr, "getopt_long returned character code 0x%x\n", c);
		}
	} // Finished parsing command line options

	if (optind < argc) {
		//printf("%i non-option argv elements?\n", argc - optind);
	} else {
		print_help(argv[0]);
		return 1;
	}

	const int cnt = argc - optind;    // Every argument left after taking away the options is a hostname.
	struct destination_info destinations[cnt];

	fprintf(stderr, "Looking up %d ip numbers...", cnt);
	fflush(stderr);
	const int num = get_ip_addresses(cnt, argv, optind, destinations);
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
		struct timeval timeout = default_timeout;    // seconds, microseconds.
		ping_all(cnt, destinations, &timeout);
		fprintf(stdout, CLEARSCREEN);
		for (int i = 0; i < cnt; ++i)
		{
			const int t = destinations[i].response_time;
			const int e = destinations[i].error;
			fprintf(stdout, "%-20s", argv[optind + i]);
			if (t < 0)
				if (e != 0)
					fprintf(stdout, FGWHT BGRED "   ERROR" RESETALL " (%s)\n", strerror(e));
				else
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
	
	// Free addresses got from get_ip_addresses
	for (int i = 0; i < cnt; i++) {
		free(destinations[i].address);
	}
	
	return 0;
}

// vi: tabstop=8
