/*
 * UDP Tunnel - Main Program
 * 
 * This program provides bidirectional tunneling between UDP and TCP protocols.
 * It operates in two modes:
 * 
 * SERVER MODE (-s): Listens for TCP connections and relays encapsulated packets
 * to a UDP destination. TCP clients send length-prefixed UDP packets that are
 * extracted and forwarded via UDP.
 * 
 * CLIENT MODE (default): Listens for UDP packets and encapsulates them in a TCP
 * connection to a server. UDP packets are prefixed with their length and sent
 * over a persistent TCP connection.
 * 
 * PROTOCOL: The TCP stream uses a simple length-prefix protocol:
 * - Required handshake: 32-byte authentication string (client sends, server validates)
 * - Packet format: [2-byte length][UDP payload data]
 * - Length is in network byte order (big-endian)
 * - Maximum UDP payload: 65534 bytes (TCPBUFFERSIZE - 2)
 * 
 * FEATURES:
 * - Socket activation support (systemd/inetd)
 * - Configurable timeouts for idle connections
 * - Fork-based server model for multiple concurrent connections
 * - Comprehensive logging with multiple verbosity levels
 * 
 * Copyright (C) 2018 Marco d'Itri
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 * Parts of this program are derived from udptunnel.c by Jonathan Lennox.
 * This is the license of the original code:
 *
 * Copyright 1999, 2001 by Columbia University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* for sigaction... */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_SYSTEMD_SD_DAEMON_H
#include <systemd/sd-daemon.h>
#endif

#include "libs/utils/utils.h"
#include "libs/log/log.h"
#include "libs/network/network.h"

#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

/*
 * Buffer size constants for the tunnel protocol:
 * - TCPBUFFERSIZE: 65536 bytes (64KB) provides large buffer for efficient TCP stream parsing
 *   state machine and can accommodate multiple encapsulated UDP packets
 * - UDPBUFFERSIZE: reserves 2 bytes for the length prefix in the TCP stream format,
 *   allowing maximum UDP payload of 65534 bytes per encapsulated packet
 */
#define TCPBUFFERSIZE 65536					// TCP stream buffer size (64KB)
#define UDPBUFFERSIZE (TCPBUFFERSIZE - 2)	// Maximum UDP payload size (minus 2-byte length prefix)

/**
 * TCP packet wrapper for sending UDP data over TCP connection.
 * Uses network byte order (big-endian) length prefix followed by UDP payload.
 */
struct out_packet {
    uint16_t length;               // Length of UDP payload in network byte order
    char buf[UDPBUFFERSIZE];       // UDP packet data (max 65534 bytes)
};

/**
 * Command-line configuration options.
 * Stores parsed arguments and operational mode settings.
 */
struct opts {
    const char *udpaddr, *tcpaddr; // Source and destination address strings

    int is_server;                 // 1 = server mode (TCP->UDP), 0 = client mode (UDP->TCP)
    int use_inetd;                 // 1 = running under inetd/systemd, 0 = standalone
    char *handshake;               // Authentication handshake string (32 bytes)
    int timeout;                   // Idle connection timeout in seconds
};

/**
 * Connection relay state and buffers.
 * Manages the bidirectional tunnel between UDP and TCP protocols, including
 * the TCP stream parsing state machine and connection addressing.
 */
struct relay {
    struct sockaddr_storage remote_udpaddr; // UDP peer address for replies

    int udp_sock, tcp_sock;        // Socket file descriptors

    int expect_handshake;          // 1 if handshake validation required (server mode)
    char handshake[32];            // Expected handshake string for authentication
    int udp_timeout, tcp_timeout;  // Timeout values for each protocol direction
    char buf[TCPBUFFERSIZE];       // TCP stream buffer for parsing packets
    char *buf_ptr, *packet_start;  // Buffer pointers for stream parsing
    int packet_length;             // Expected length of current packet being read
    enum {
		uninitialized = 0,         // Initial state - determine next operation
		reading_handshake,         // Expecting handshake data from TCP peer
		reading_length,            // Reading 2-byte length prefix
		reading_packet,            // Reading UDP payload data
    } state;                       // TCP stream parsing state machine
};

/**
 * Display program usage information and exit.
 *
 * @param status (int) - Exit code - 0 for help request, non-zero for error
 *
 * @return Does not return - calls exit(status)
 */
static void usage(int status)
{
    FILE *fp = status == 0 ? stdout : stderr;

    fprintf(fp, "Usage: udptunnel [OPTION]... [[SOURCE:]PORT] DESTINATION:PORT\n\n");
    fprintf(fp, "-s    --server         listen for TCP connections\n");
    fprintf(fp, "-i    --inetd          expect to be started by inetd\n");
    fprintf(fp, "-T N  --timeout N      close the source connection after N seconds\n");
    fprintf(fp, "                       where no data was received\n");
    fprintf(fp, "-S    --syslog         log to syslog instead of standard error\n");
    fprintf(fp, "-v    --verbose        explain what is being done\n");
    fprintf(fp, "-h    --help           display this help and exit\n");
    fprintf(fp, "\nSOURCE:PORT must not be specified when using inetd or socket activation.\n\n");
    fprintf(fp, "If the -s option is used then the program will listen on SOURCE:PORT for TCP\n");
    fprintf(fp, "connections and relay the encapsulated packets with UDP to DESTINATION:PORT.\n");
    fprintf(fp, "Otherwise it will listen on SOURCE:PORT for UDP packets and encapsulate\n");
    fprintf(fp, "them in a TCP connection to DESTINATION:PORT.\n");

    exit(status);
}

/**
 * Parse command-line arguments and configure program options.
 * Sets up logging verbosity, validates argument count, and populates the opts structure
 * with parsed configuration including addresses, timeouts, and operational modes.
 *
 * @param argc (int) - Number of command-line arguments
 * @param argv (char*[]) - Array of command-line argument strings
 * @param opts (struct opts*) - Output structure to populate with parsed options
 *
 * @return void - exits program on invalid arguments via usage()
 */
static void parse_args(int argc, char *argv[], struct opts *opts)
{
#ifdef HAVE_GETOPT_LONG
    const struct option longopts[] = {
		{"inetd",			no_argument,		NULL, 'i' },
		{"server",			no_argument,		NULL, 's' },
		{"syslog",			no_argument,		NULL, 'S' },
		{"timeout",			required_argument,	NULL, 'T' },
		{"help",			no_argument,		NULL, 'h' },
		{"verbose",			no_argument,		NULL, 'v' },
		{NULL,				0,			NULL, 0   },
    };
    int longindex;
#endif
    int c;
    int expected_args;
    int verbose = 0;
    int use_syslog = 0;

    /*
     * Initialize default 32-byte handshake authentication token:
     * - Bytes 0-15: "udptunnel by md." + 3 null terminators (human-readable signature)
     * - Bytes 16-31: Binary entropy sequence \x01\x03\x06\x10\x15\x21\x28\x36\x45\x55\x66\x78\x91
     *   providing additional randomness and making the handshake harder to forge accidentally.
     *   The specific byte values create a distinctive fingerprint for protocol validation.
     */
    opts->handshake = NOFAIL(malloc(32));
    memcpy(opts->handshake, "udptunnel by md.\0\0\0\x01\x03\x06\x10\x15\x21\x28\x36\x45\x55\x66\x78\x91", 32);

    while ((c = GETOPT_LONGISH(argc, argv, "ihsvST:", longopts, &longindex)) > 0) {
		switch (c) {
			case 'i':
				opts->use_inetd = 1;
				break;
			case 's':
				opts->is_server = 1;
				break;
			case 'S':
				use_syslog = 1;
				break;
			case 'T':
				/*
				 * Parse timeout value: atol() returns long but we assign to int.
				 * On systems where sizeof(long) > sizeof(int), values exceeding
				 * INT_MAX will be truncated. For timeout values, this truncation
				 * is acceptable since timeouts > 2^31 seconds are impractical.
				 */
				opts->timeout = atol(optarg);
				break;
			case 'v':
				verbose++;
				break;
			case 'h':
				usage(0);
				break;
			default:
				usage(2);
				break;
		}
    }

    /*
     * Determine expected number of command-line arguments based on runtime mode:
     * - Standalone mode: requires 2 args (source address + destination address)
     * - Socket activation/inetd mode: requires 1 arg (destination only, source provided by system)
     * sd_listen_fds(0) returns number of systemd-provided sockets (0 if no activation, >0 if socket activation),
     * use_inetd flag indicates traditional inetd mode; either condition means 1 arg expected
     */
    expected_args = (sd_listen_fds(0) || opts->use_inetd) ? 1 : 2;

    if (argc - optind == 0)
		usage(2);
    if (argc - optind != expected_args) {
		fprintf(stderr, "Expected %d argument(s)!\n\n", expected_args);
		usage(2);
    }

    /* Parse source and destination addresses based on mode */
    if (opts->is_server) {
		if (expected_args == 2)
			opts->tcpaddr = NOFAIL(strdup(argv[optind++])); // Server mode: TCP listen address
		opts->udpaddr = NOFAIL(strdup(argv[optind++]));     // UDP destination to relay to
    } else {
		if (expected_args == 2)
			opts->udpaddr = NOFAIL(strdup(argv[optind++])); // Client mode: UDP listen address
		opts->tcpaddr = NOFAIL(strdup(argv[optind++]));     // TCP destination to connect to
    }

    if (!verbose)
		log_set_options(log_warning);
    else if (verbose == 1)
		log_set_options(log_notice);
    else if (verbose == 2)
		log_set_options(log_info);
    else
		log_set_options(log_debug);

    if (use_syslog)
		log_set_options(log_get_filter_level() | log_syslog);
}

/**
 * Validate and return a UDP socket from systemd socket activation.
 * Ensures exactly one UDP socket is provided and validates its type.
 *
 * @param num (const int) - Number of file descriptors from socket activation
 *
 * @return int - File descriptor of the validated UDP socket, exits on error
 */
int udp_listener_sa(const int num)
{
    int fd = SD_LISTEN_FDS_START; // systemd passes sockets starting from this fd number

    if (num != 1)
		log_printf_exit(2, log_err, "UDP socket activation supports a single socket.");

    if (sd_is_socket(fd, AF_UNSPEC, SOCK_DGRAM, -1) <= 0) // Verify it's a UDP socket
		log_printf_exit(2, log_err, "UDP socket activation fd %d is not valid.", fd);

    return fd;
}

/**
 * Validate and return TCP sockets from systemd socket activation.
 * Validates all provided TCP sockets and returns them in a null-terminated array.
 *
 * @param num (const int) - Number of file descriptors from socket activation
 *
 * @return int* - Null-terminated array of validated TCP socket file descriptors
 */
int *tcp_listener_sa(const int num)
{
    int fd;
    int *fds;
    int fd_num = 0;

    fds = NOFAIL(malloc((num + 1) * sizeof(int)));

    for (fd = SD_LISTEN_FDS_START; fd < SD_LISTEN_FDS_START + num; fd++) {
		if (sd_is_socket(fd, AF_UNSPEC, SOCK_STREAM, 1) <= 0)
			log_printf_exit(2, log_err, "TCP socket activation fd %d is not valid.", fd);
		fds[fd_num++] = fd;
    }

    fds[fd_num] = -1;

    return fds;
}

/**
 * Receive UDP packet and encapsulate it in TCP stream.
 * Reads a UDP packet, stores the sender's address for replies, and sends the packet
 * over TCP with a length prefix. Used in client mode to tunnel UDP through TCP.
 *
 * @param relay (struct relay*) - Connection state and socket information
 *
 * @return void - exits program on socket errors
 */
static void udp_to_tcp(struct relay *relay)
{
    struct out_packet p;
    int buflen;
    struct sockaddr_storage remote_udpaddr;
    socklen_t addrlen = sizeof(remote_udpaddr);

    /*
     * Receive UDP packet and capture sender's address for bidirectional tunnel operation.
     * The sender address is essential because UDP is connectionless - we need to know
     * where to send replies when data comes back through the TCP tunnel from the server.
     * This enables proper bidirectional communication in client mode.
     */
    buflen = recvfrom(relay->udp_sock, p.buf, UDPBUFFERSIZE, 0, (struct sockaddr *) &remote_udpaddr, &addrlen);
    if (buflen < 0)
		err_sys("recvfrom(udp)");
    if (buflen == 0)
		return;	/* ignore empty packets */

    /*
     * Store the source address of the received UDP packet, to be able to use
     * it in send_udp_packet as the destination address of the next UDP reply.
     * addrlen from recvfrom() ensures only valid address bytes are copied.
     */
    memcpy(&(relay->remote_udpaddr), &remote_udpaddr, addrlen);

#ifdef DEBUG
    log_printf(log_debug, "Received a %d bytes UDP packet from %s", buflen,
	    print_addr_port((struct sockaddr *) &remote_udpaddr, addrlen));
#endif

    p.length = htons(buflen);
    if (send(relay->tcp_sock, &p, buflen + sizeof(p.length), 0) < 0) // Send struct: 2-byte length header + UDP payload data (total: buflen + 2 bytes)
		err_sys("send(tcp)");
}

/**
 * Send UDP packet to the stored remote address.
 * Transmits the current packet data to the UDP peer address that was stored
 * from the most recent received UDP packet. Handles ECONNREFUSED errors gracefully.
 *
 * @param relay (struct relay*) - Connection state with packet data and remote address
 *
 * @return void - logs errors but continues execution
 */
static void send_udp_packet(struct relay *relay)
{
    int opt = 0;
    socklen_t len = sizeof(opt);

    if (relay->remote_udpaddr.ss_family == 0) { // No UDP peer address stored yet
		log_printf(log_info, "Ignoring a packet for a still unknown UDP destination!");
		return;
    }

    if (sendto(relay->udp_sock, relay->packet_start, relay->packet_length, 0, (struct sockaddr *) &relay->remote_udpaddr, sizeof(relay->remote_udpaddr)) >= 0) // Send UDP packet to stored peer address
		return;

    /* this is the error path */
    if (errno != ECONNREFUSED)
		err_sys("sendto(udp)");

    /*
     * Handle ECONNREFUSED gracefully: this commonly occurs when the UDP peer
     * is not yet listening or has temporarily stopped. Since UDP is connectionless,
     * we continue operation rather than terminating the tunnel. The getsockopt()
     * call retrieves and clears any pending socket error state.
     */
    log_printf(log_info, "sendto(udp) returned ECONNREFUSED: ignored");
    if (getsockopt(relay->udp_sock, SOL_SOCKET, SO_ERROR, &opt, &len) < 0) // Retrieve and clear pending socket error state
		err_sys("getsockopt(udp, SOL_SOCKET, SO_ERROR)");

    return;
}

/**
 * Parse TCP stream and extract UDP packets for forwarding.
 * Implements a state machine to parse the TCP stream: reads handshake (if expected),
 * then alternates between reading 2-byte length prefixes and UDP packet data.
 * Forwards complete UDP packets via send_udp_packet().
 *
 * @param relay (struct relay*) - Connection state including parsing buffers and state machine
 *
 * @return void - exits program on TCP connection close or socket errors
 */
static void tcp_to_udp(struct relay *relay)
{
    int read_len;

    /*
     * Initialize TCP stream parsing state machine on first call.
     * Server mode: expect handshake authentication first, then switch to packet parsing
     * Client mode: immediately start parsing length-prefixed packets
     */
    if (relay->state == uninitialized) {
		if (relay->expect_handshake) {
			relay->state = reading_handshake;
			relay->packet_length = sizeof(relay->handshake); // Expect 32-byte handshake first
		} else {
			relay->state = reading_length;
			relay->packet_length = sizeof(uint16_t); // Expect 2-byte length prefix
		}
		relay->buf_ptr = relay->buf;        // Current write position in buffer
		relay->packet_start = relay->buf;   // Start of current packet being parsed
    }

    read_len = read(relay->tcp_sock, relay->buf_ptr, (relay->buf + TCPBUFFERSIZE - relay->buf_ptr)); // Read into remaining buffer space
    if (read_len < 0)
		err_sys("read(tcp)");

    if (read_len == 0) // TCP connection closed by peer
		log_printf_exit(0, log_notice, "Remote closed the connection");

    relay->buf_ptr += read_len; // Advance write pointer

    while (relay->buf_ptr - relay->packet_start >= relay->packet_length) { // Process complete packets
		if (relay->state == reading_handshake) {
			/* check the handshake string */
			if (memcmp(relay->packet_start, &(relay->handshake), sizeof(relay->handshake)) != 0) // Memory comparison of 32-byte handshake
			log_printf_exit(0, log_info, "Received a bad handshake, exiting");
			log_printf(log_debug, "Received a good handshake");
			relay->packet_start += sizeof(relay->handshake); // Skip past handshake in buffer
			relay->state = reading_length;
			relay->packet_length = sizeof(uint16_t);
		} else if (relay->state == reading_length) {
			/* Extract packet length from network byte order */
			relay->packet_length = ntohs(*(uint16_t *) relay->packet_start); // Convert from big-endian
			relay->packet_start += sizeof(uint16_t); // Skip past 2-byte length field in stream
			relay->state = reading_packet;
		} else if (relay->state == reading_packet) {
			/* read an encapsulated packet and send it as UDP */
	#ifdef DEBUG
			log_printf(log_debug, "Received a %u bytes TCP packet", relay->packet_length);
	#endif

			send_udp_packet(relay);

			/*
			 * Compact buffer: move any remaining unprocessed data to the start of the buffer.
			 * This prevents buffer overflow and maintains parsing state across multiple reads.
			 * - Source: data after the current packet (relay->packet_start + relay->packet_length)
			 * - Destination: beginning of buffer (relay->buf)
			 * - Length: remaining unprocessed bytes (relay->buf_ptr - processed_data_end)
			 */
			memmove(relay->buf, relay->packet_start + relay->packet_length, relay->buf_ptr - (relay->packet_start + relay->packet_length));
			/* Update buffer pointer to reflect the compacted buffer state */
			relay->buf_ptr -= relay->packet_length + (relay->packet_start - relay->buf);
			relay->packet_start = relay->buf;
			relay->state = reading_length;
			relay->packet_length = sizeof(uint16_t);
		}
    }
}

/**
 * Send authentication handshake to TCP peer.
 * Transmits the 32-byte handshake string to establish the tunnel connection.
 * Used in client mode to authenticate with the server.
 *
 * @param relay (struct relay*) - Connection state with handshake data and TCP socket
 *
 * @return void - exits program on send errors
 */
static void send_handshake(struct relay *relay)
{
    if (sendto(relay->tcp_sock, relay->handshake, sizeof(relay->handshake), 0, (struct sockaddr *) &relay->remote_udpaddr, sizeof(relay->remote_udpaddr)) < 0)
		err_sys("sendto(tcp, handshake)");
}

/**
 * SIGCHLD signal handler to reap terminated child processes.
 * Prevents zombie processes in server mode by calling waitpid() for all available children.
 * Uses WNOHANG to avoid blocking if no children have exited.
 *
 * @param sig (int) - Signal number (unused, always SIGCHLD)
 *
 * @return void
 */
static void wait_for_child(int sig)
{
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/**
 * Main event loop for bidirectional packet relaying.
 * Uses select() to monitor both UDP and TCP sockets for incoming data,
 * handles timeout management, and dispatches to appropriate relay functions.
 * Runs indefinitely until a timeout occurs or an error forces program exit.
 *
 * @param relay (struct relay*) - Connection state with sockets and timeout configuration
 *
 * @return void - never returns normally, exits via timeout or error
 */
static void main_loop(struct relay *relay)
{
    time_t last_udp_input, last_tcp_input;

    last_udp_input = relay->udp_timeout ? time(NULL) : 0; // Initialize UDP timeout tracking
    last_tcp_input = relay->tcp_timeout ? time(NULL) : 0; // Initialize TCP timeout tracking

    while (1) {
		int ready_fds;
		int max = 0;
		fd_set readfds;
		struct timeval tv, *ptv;

		FD_ZERO(&readfds); // Clear file descriptor set
		FD_SET(relay->tcp_sock, &readfds); // Monitor TCP socket for data
		SET_MAX(relay->tcp_sock); // Track highest fd number for select()
		FD_SET(relay->udp_sock, &readfds); // Monitor UDP socket for data
		SET_MAX(relay->udp_sock); // Update highest fd number

		/*
		 * Configure select() timeout strategy:
		 * - If timeouts are enabled: use 10-second intervals to periodically check for idle connections
		 * - If no timeouts: block indefinitely waiting for socket activity
		 * This balances responsiveness (checking timeouts) with efficiency (not busy-waiting)
		 */
		if (last_udp_input || last_tcp_input) {
			tv.tv_usec = 0;
			tv.tv_sec = 10; // Check for timeouts every 10 seconds
			ptv = &tv;
		} else {
			ptv = NULL; // Block indefinitely if no timeouts configured
		}

		ready_fds = select(max, &readfds, NULL, NULL, ptv); // Wait for socket activity or timeout
		if (ready_fds < 0) {
			if (errno == EINTR || errno == EAGAIN) // Interrupted by signal or temporary error
				continue;
			err_sys("select");
		}

		/* check timeouts when no file descriptors are ready (ready_fds == 0) */
		if (last_udp_input && !ready_fds) {	/* select() timed out, check UDP timeout */
			if (time(NULL) - last_udp_input > relay->udp_timeout) // Check if UDP idle time exceeded configured limit
			log_printf_exit(0, log_notice, "Exiting after a %ds timeout for UDP input", relay->udp_timeout);
		}
		if (last_tcp_input && !ready_fds) {	/* select() timed out, check TCP timeout */
			if (time(NULL) - last_tcp_input > relay->tcp_timeout) // Check if TCP idle time exceeded configured limit
			log_printf_exit(0, log_notice, "Exiting after a %ds timeout for TCP input", relay->tcp_timeout);
		}

		if (FD_ISSET(relay->tcp_sock, &readfds)) { // TCP socket has data ready
			tcp_to_udp(relay);
			if (last_tcp_input)
			last_tcp_input = time(NULL); // Update activity timestamp
		}
		if (FD_ISSET(relay->udp_sock, &readfds)) { // UDP socket has data ready
			udp_to_tcp(relay);
			if (last_udp_input)
			last_udp_input = time(NULL); // Update activity timestamp
		}
    }
}

/**
 * Program entry point and initialization.
 * Parses command-line arguments, sets up signal handlers, establishes network connections
 * based on operational mode (server/client), and starts the main relay loop.
 *
 * @param argc (int) - Number of command-line arguments
 * @param argv (char*[]) - Array of command-line argument strings
 *
 * @return int - Program exit status (0 on success, never reached due to main_loop)
 */
int main(int argc, char *argv[])
{
    struct opts opts;
    struct relay relay;

    memset(&relay, 0, sizeof(relay)); // Initialize all fields to zero
    relay.tcp_sock = -1; // Mark TCP socket as invalid initially

    memset(&opts, 0, sizeof(opts));
    parse_args(argc, argv, &opts);
    if (opts.handshake) // Copy custom handshake if provided
		memcpy(relay.handshake, opts.handshake, sizeof(relay.handshake));

    sd_notify(0, "READY=1"); // Signal systemd that service is ready

    if (opts.is_server) {
		struct sigaction sa;

		sa.sa_handler = wait_for_child; // Set signal handler function
		sigemptyset(&sa.sa_mask); // Don't block any signals during handler execution
		sa.sa_flags = SA_RESTART; // Restart interrupted system calls automatically
		if (sigaction(SIGCHLD, &sa, NULL) == -1) // Install SIGCHLD handler for child reaping
			err_sys("sigaction");

		if (opts.timeout)
			relay.tcp_timeout = opts.timeout; // Server timeout applies to TCP connections
		relay.expect_handshake = 1; // Server expects handshake from clients

		if (opts.use_inetd) {
			relay.tcp_sock = 0; // inetd provides connection on stdin/stdout
			log_set_options(log_get_filter_level() | log_syslog); // Use syslog when running under inetd
		} else {
			int socket_activation_fds = sd_listen_fds(0);
			int *listening_sockets;

			if (socket_activation_fds) // systemd socket activation
				listening_sockets = tcp_listener_sa(socket_activation_fds);
			else // Create listening socket manually
				listening_sockets = tcp_listener(opts.tcpaddr);
			/*
			 * Accept TCP connection and fork child process to handle it (forking occurs within accept_connections()).
			 * Parent continues listening for new connections, child handles tunnel relay.
			 * This implements the fork-based server model mentioned in the header.
			 */
			relay.tcp_sock = accept_connections(listening_sockets);
		}
		relay.udp_sock = udp_client(opts.udpaddr, &relay.remote_udpaddr); // Connect to UDP destination
    } else {
		if (opts.timeout)
			relay.udp_timeout = opts.timeout; // Client timeout applies to UDP connections

		if (opts.use_inetd) {
			relay.udp_sock = 0;
			log_set_options(log_get_filter_level() | log_syslog);
		} else {
			int socket_activation_fds = sd_listen_fds(0);

			if (socket_activation_fds)
				relay.udp_sock = udp_listener_sa(socket_activation_fds);
			else
				relay.udp_sock = udp_listener(opts.udpaddr);
		}
		relay.tcp_sock = tcp_client(opts.tcpaddr); // Connect to TCP server

		send_handshake(&relay); // Send authentication handshake to server
    }

    main_loop(&relay);
    exit(0);
}
