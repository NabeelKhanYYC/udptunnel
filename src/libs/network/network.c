/*
 * Network Library - Socket Management and Address Resolution
 * 
 * Provides comprehensive network socket functionality for UDP tunnel operations,
 * including socket creation, binding, listening, and connection management for
 * both UDP and TCP protocols with IPv4/IPv6 dual-stack support.
 * 
 * Key Functions:
 * - print_addr_port(): Format socket addresses for logging and display
 * - udp_listener()/tcp_listener(): Create listening sockets with address resolution
 * - udp_client()/tcp_client(): Create client connections with automatic retry
 * - accept_connections(): Multi-socket connection acceptance with process forking
 * 
 * Dependencies: POSIX sockets, getaddrinfo/getnameinfo for address resolution
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
 */

// for getaddrinfo...
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ctype.h>

#include "network.h"
#include "../utils/utils.h"
#include "../log/log.h"

/**
 * Formats a socket address into a human-readable string with address and port.
 * Handles both IPv4 and IPv6 addresses, using appropriate formatting conventions
 * (IPv6 addresses are bracketed when combined with port numbers).
 *
 * @param addr (struct sockaddr*) - Socket address structure to format
 * @param addrlen (socklen_t) - Length of the socket address structure
 *
 * @return char* - Pointer to static buffer containing formatted address:port string
 *                Returns formatted string like "192.168.1.1:8080" for IPv4
 *                or "[2001:db8::1]:8080" for IPv6
 */
char *print_addr_port(const struct sockaddr *addr, socklen_t addrlen)
{
    static char buf[1100], address[1025], port[32];	// Static buffers for thread-unsafe but simple usage
    int err;

    /*
     * Convert binary socket address to human-readable strings.
     * NI_NUMERICHOST | NI_NUMERICSERV forces numeric output instead of
     * attempting hostname/service lookups, which could block or fail.
     */
    err = getnameinfo(addr, addrlen, address, sizeof(address), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (err == EAI_SYSTEM)
		err_sys("getnameinfo");			// System error (errno set)
    else if (err)
		log_printf_exit(1, log_err, "getnameinfo: %s", gai_strerror(err));	// GAI error

    /*
     * Format according to standard conventions:
     * IPv6: [address]:port (brackets prevent ambiguity with colons in address)
     * IPv4: address:port (no brackets needed)
     */
    if (addr->sa_family == AF_INET6)
		snprintf(buf, sizeof(buf) - 1, "[%s]:%s", address, port);
    else
		snprintf(buf, sizeof(buf) - 1, "%s:%s", address, port);

    return buf;							// Return pointer to static buffer - caller must not free
}

/*
 * Convenience wrapper to format addrinfo structure addresses.
 * Used internally for logging resolved addresses during socket operations.
 */
static char *ai_print_addr_port(struct addrinfo *ai)
{
    return print_addr_port((struct sockaddr *) ai->ai_addr, ai->ai_addrlen);
}

/**
 * Parses various address and port string formats into separate components.
 * Handles IPv4, IPv6, hostnames, and port-only specifications. This flexible
 * parsing supports multiple input formats commonly used in network applications.
 *
 * Supported formats:
 * - IPv4 with port: "192.168.1.1:8080"
 * - IPv6 with port: "[2001:db8::1]:8080"
 * - IPv6 without port: "2001:db8::1"
 * - Hostname with port: "example.com:8080"
 * - Port only: "8080"
 * - Address only: "192.168.1.1" or "example.com"
 *
 * @param input (const char*) - Input string to parse
 * @param address (char**) - Output pointer for address string (NULL if not found)
 * @param port (char**) - Output pointer for port string (NULL if not found)
 *
 * @return void - Results returned via address and port parameters
 *               Caller must free() non-NULL address and port strings
 */
static void parse_address_port(const char *input, char **address, char **port)
{
    const char *p;

    *address = NULL;
    *port = NULL;

    /*
     * Parse different address formats:
     * Empty string: return both address and port as NULL
     * IPv6 bracketed format: [address]:port or [address]
     * IPv6 unbracketed: address (multiple colons, no port)
     * IPv4 with port: address:port
     * Port only: numeric string
     * Address only: non-numeric string
     */
    if (*input == '\0') {
		return;
    } else if (*input == '[' && (p = strchr(input, ']'))) {	/* IPv6 bracketed format */
		char *s;
		int len = p - input - 1;		// Length of address between brackets

		/* Extract IPv6 address from between [ and ] */
		*address = s = NOFAIL(malloc(len + 1));
		memcpy(s, input + 1, len);			// Copy address part, skip opening [
		*(s + len) = '\0';			    	// Null terminate

		/* Look for optional port after the closing ] */
		p = strchr(p, ':');					// Find : after ]
		if (p && *(p + 1) != '\0')			// If : exists and has content after it
			*port = NOFAIL(strdup(p + 1));	// Extract port
	} else if ((p = strchr(input, ':')) &&	/* IPv6 without brackets */
		strchr(p + 1, ':')) {				/* Multiple colons = IPv6 address */

		/* Unbracketed IPv6 address (no port possible due to ambiguity) */
		*address = NOFAIL(strdup(input));
	} else if ((p = strchr(input, ':'))) {		/* IPv4 + port or hostname + port */
		char *s;
		int len = p - input;			// Length of address part before :

		/* Extract address part (everything before first :) */
		if (len) {						/* Only allocate if there's an address part */
			*address = s = NOFAIL(malloc(len + 1));
			memcpy(s, input, len);
			*(s + len) = '\0';
		}

		/* Extract port part (everything after first :) */
		p++;							// Skip the :
		if (*p != '\0')					// Only set port if there's content after :
			*port = NOFAIL(strdup(p));
	} else {
		/* Ambiguous case: could be address-only or port-only */
		/* Check if string is all digits -> port, otherwise -> address */
		for (p = input; *p; p++)
			if (!isdigit(p[0]))	 // Non-digit found
				break;
		if (*p)									// Non-digit found -> this is an address
			*address = NOFAIL(strdup(input));	// Address without port
		else									// All digits -> this is a port number
			*port = NOFAIL(strdup(input));		// Port without address
    }
}

/**
 * Creates a UDP listening socket bound to the specified address and port.
 * Performs address resolution and attempts to bind to the first available
 * address family (IPv4 or IPv6). Used for setting up UDP tunnel endpoints.
 *
 * @param s (const char*) - Address specification string (parsed by parse_address_port)
 *                         Examples: "8080", "192.168.1.1:8080", "[::1]:8080"
 *
 * @return int - File descriptor of the bound UDP socket
 *              Function exits on error (address resolution or bind failure)
 */
int udp_listener(const char *s)
{
    char *address, *port;
    struct addrinfo hints, *res, *ai;
    int err, fd;

    parse_address_port(s, &address, &port);

    if (!port)
		log_printf_exit(2, log_err, "Missing port in '%s'!", s);

    /*
     * Set up address resolution hints:
     * AF_UNSPEC: Accept both IPv4 and IPv6 addresses
     * SOCK_DGRAM: UDP socket type
     * AI_PASSIVE: Address will be used for bind() (listening)
     * AI_ADDRCONFIG: Only return addresses for which we have network interfaces
     * AI_IDN: Support Internationalized Domain Names
     */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE | AI_IDN;

    err = getaddrinfo(address, port, &hints, &res);
    if (err == EAI_SYSTEM)
		err_sys("getaddrinfo(%s:%s)", address, port);
    else if (err)
		log_printf_exit(1, log_err, "Cannot resolve %s:%s: %s", address, port, gai_strerror(err));

    /*
     * Clean up dynamically allocated memory from parse_address_port().
     * These strings were allocated with malloc/strdup and must be freed.
     */
    if (address)
		free(address);
    if (port)
		free(port);

    /*
     * Try to bind to the first available address. Unlike tcp_listener(),
     * this only creates one socket since UDP is connectionless and doesn't
     * need to listen on multiple address families simultaneously.
     */
    for (ai = res; ai; ai = ai->ai_next) {
		if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0)
			continue;				// ignore socket creation failure, try next address
		if (bind(fd, (struct sockaddr *) ai->ai_addr, ai->ai_addrlen) == 0)
			break;					// success - bound to address
		close(fd);					// bind failed, clean up and try next
    }

    if (!ai)
		err_sys("Cannot bind to %s", s);

    log_printf(log_info, "Listening for UDP connections on %s", ai_print_addr_port(ai));

    freeaddrinfo(res);

    return fd;
}

/**
 * Creates multiple TCP listening sockets for all available address families.
 * Unlike udp_listener(), this function creates sockets for all resolved addresses
 * (both IPv4 and IPv6) to handle dual-stack scenarios. Returns a dynamically
 * allocated array of file descriptors terminated with -1.
 *
 * @param s (const char*) - Address specification string (parsed by parse_address_port)
 *                         Examples: "8080", "192.168.1.1:8080", "[::1]:8080"
 *
 * @return int* - Dynamically allocated array of TCP socket file descriptors,
 *               terminated with -1. Caller must free() the returned array.
 *               Function exits on error (address resolution or bind failure)
 */
int *tcp_listener(const char *s)
{
    char *address, *port;
    struct addrinfo hints, *res, *ai;
    int err, fd, opt;
    int fd_num = 0;
    int *fd_list = NULL;
    size_t allocated_fds = 0;

    parse_address_port(s, &address, &port);

    if (!port)
		log_printf_exit(2, log_err, "Missing port in '%s'!", s);

    /*
     * Set up address resolution hints for TCP listening:
     * AF_UNSPEC: Accept both IPv4 and IPv6 addresses
     * SOCK_STREAM: TCP socket type
     * AI_PASSIVE: Address will be used for bind() (listening)
     * AI_ADDRCONFIG: Only return addresses for which we have network interfaces
     * AI_IDN: Support Internationalized Domain Names
     */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE | AI_IDN;

    err = getaddrinfo(address, port, &hints, &res);
    if (err == EAI_SYSTEM)
		err_sys("getaddrinfo(%s:%s)", address, port);
    else if (err)
		log_printf_exit(1, log_err, "Cannot resolve %s:%s: %s", address, port, gai_strerror(err));

    /*
     * Clean up dynamically allocated memory from parse_address_port().
     * These strings were allocated with malloc/strdup and must be freed.
     */
    if (address)
		free(address);
    if (port)
		free(port);

    /*
     * Create listening sockets for ALL resolved addresses (IPv4 and IPv6).
     * This enables dual-stack operation where the server can accept connections
     * on both address families simultaneously. Each successful socket is added
     * to the dynamically growing fd_list array.
     */
    for (ai = res; ai; ai = ai->ai_next) {
	if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0)
	    continue;		// ignore socket creation failure, try next address
		/*
		 * Enable SO_REUSEADDR to allow immediate reuse of the port after
		 * server restart, avoiding "Address already in use" errors during
		 * development and testing.
		 */
	opt = 1;
	if (ai->ai_family == AF_INET6) {
	    /* we are going to bind to IPv6 address only */
	    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0)
	        err_sys("setsockopt(IPPROTO_IPV6, IPV6_V6ONLY)");
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	    err_sys("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
	if (bind(fd, (struct sockaddr *) ai->ai_addr, ai->ai_addrlen) < 0)
	    err_sys("Cannot bind to %s", s);

		/* success */
		if (listen(fd, 128) < 0)
			err_sys("listen");

		/*
		 * Grow the fd_list array dynamically. We need space for:
		 * - Current socket (fd_num)
		 * - New socket (+1) 
		 * - Array terminator -1 (+1)
		 * Allocate in chunks of 8 to reduce realloc() calls.
		 */
		if (allocated_fds < fd_num + 1 + 1) {
			allocated_fds += 8;
			fd_list = realloc(fd_list, allocated_fds * sizeof(int));
		}
		fd_list[fd_num++] = fd;

		log_printf(log_info, "Listening for TCP connections on %s", ai_print_addr_port(ai));
    }

    /*
     * Terminate the file descriptor array with -1 sentinel value.
     * This allows callers to iterate through the array without knowing
     * the exact count. Ensure we have space for the terminator.
     */
    if (allocated_fds < fd_num + 1 + 1)
		fd_list = realloc(fd_list, ++allocated_fds * sizeof(int));
    fd_list[fd_num] = -1;				// Array terminator

    /*
     * Check for complete failure: no sockets were created successfully.
     * This could happen if all addresses failed to resolve or bind.
     */
    if (!fd_list)
		err_sys("socket");				// No listening sockets created

    freeaddrinfo(res);

    return fd_list;
}

/**
 * Accepts incoming TCP connections on multiple listening sockets using select().
 * For each accepted connection, forks a child process to handle it while the
 * parent continues listening. The child process closes all listening sockets
 * and returns the connected socket descriptor for tunnel processing.
 *
 * This function implements a traditional pre-forking server model where each
 * client connection is handled by a separate process, providing isolation
 * between tunnel sessions.
 *
 * @param listening_sockets (int[]) - Array of listening socket descriptors
 *                                   terminated with -1 (from tcp_listener)
 *
 * @return int - In child process: file descriptor of accepted connection
 *              In parent process: Never returns normally (infinite loop)
 *              Function exits on system call errors
 */
int accept_connections(int listening_sockets[])
{
    while (1) {
		int max = 0;
		int i, fd;
		fd_set readfds;
		pid_t pid;

		/*
		 * Prepare for select() by setting up the file descriptor set.
		 * Make all listening sockets non-blocking to prevent hanging
		 * on accept() if another process steals the connection.
		 */
		FD_ZERO(&readfds);				// Clear the file descriptor set
		for (i = 0; listening_sockets[i] != -1; i++) {
			int flags;

			/* Set socket to non-blocking mode */
			if ((flags = fcntl(listening_sockets[i], F_GETFL, 0)) < 0)
				err_sys("fcntl(F_GETFL)");
			if (fcntl(listening_sockets[i], F_SETFL, flags | O_NONBLOCK) < 0)
				err_sys("fcntl(F_SETFL, O_NONBLOCK)");

			/* Add socket to the select() monitoring set */
			FD_SET(listening_sockets[i], &readfds);
			SET_MAX(listening_sockets[i]);		// Track highest fd number for select()
		}

		/*
		 * Block until at least one listening socket has an incoming connection.
		 * select() modifies readfds to indicate which sockets are ready.
		 */
		if (select(max, &readfds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;			// Handle interrupted system calls gracefully
			err_sys("select");
		}

		/*
		 * Check each listening socket that select() indicated is ready.
		 * Accept the first available connection and fork to handle it.
		 */
		for (i = 0; listening_sockets[i] != -1; i++) {
			int listen_sock;
			struct sockaddr_storage client_addr;	// Storage for client address
			socklen_t addrlen = sizeof(client_addr);

			/* Skip sockets that aren't ready (not set by select) */
			if (!FD_ISSET(listening_sockets[i], &readfds))
				continue;
			listen_sock = listening_sockets[i];

			/* Accept the incoming connection */
			fd = accept(listen_sock, (struct sockaddr *) &client_addr, &addrlen);
			if (fd < 0) {
				if (errno == EAGAIN)		// Would block (shouldn't happen after select)
					continue;
				err_sys("accept");
			}

			log_printf(log_notice, "Received a TCP connection from %s", print_addr_port((struct sockaddr *) &client_addr, addrlen));

#if 0
			/*
			 * Testing mode: handle connections in the same process.
			 * This simplifies debugging but prevents concurrent connections.
			 */
			pid = 0;
#else
			/*
			 * Production mode: fork a child process for each connection.
			 * This provides isolation between tunnel sessions and allows
			 * concurrent handling of multiple clients.
			 */
			pid = fork();
#endif

			if (pid < 0)
				err_sys("fork");

			if (pid > 0) {
				/* Parent process: close client socket and continue listening */
				close(fd);
			} else {
				/*
				 * Child process: close all listening sockets (inherited from parent)
				 * and return the client connection for tunnel processing.
				 */
				for (i = 0; listening_sockets[i] != -1; i++)
					close(listening_sockets[i]);
				return fd;
			}
		}
    }
}

/**
 * Creates a UDP client socket and resolves the remote peer address.
 * Sets up a UDP socket for sending packets to the specified destination.
 * The resolved address is stored for use in sendto() calls by the caller.
 *
 * @param s (const char*) - Remote address specification (must include both address and port)
 *                         Examples: "192.168.1.1:8080", "[2001:db8::1]:8080"
 * @param remote_udpaddr (struct sockaddr_storage*) - Buffer to store resolved remote address
 *                                                   Used as destination for subsequent UDP sends
 *
 * @return int - File descriptor of the UDP client socket
 *              Function exits on error (address resolution or socket creation failure)
 */
int udp_client(const char *s, struct sockaddr_storage *remote_udpaddr)
{
    char *address, *port;
    struct addrinfo hints, *res, *ai;
    int err, fd;

    parse_address_port(s, &address, &port);

    if (!address || !port)
		log_printf_exit(2, log_err, "Missing address or port in '%s'!", s);

    /*
     * Set up address resolution hints for UDP client:
     * AF_UNSPEC: Accept both IPv4 and IPv6 addresses
     * SOCK_DGRAM: UDP socket type
     * AI_ADDRCONFIG: Only return addresses for which we have network interfaces
     * AI_IDN: Support Internationalized Domain Names
     * Note: No AI_PASSIVE since this is for connecting, not binding
     */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_IDN;

    err = getaddrinfo(address, port, &hints, &res);
    if (err == EAI_SYSTEM)
		err_sys("getaddrinfo(%s:%s)", address, port);
    else if (err)
		log_printf_exit(1, log_err, "Cannot resolve %s:%s: %s", address, port, gai_strerror(err));

    /*
     * Clean up dynamically allocated memory from parse_address_port().
     * These strings were allocated with malloc/strdup and must be freed.
     */
    if (address)
		free(address);
    if (port)
		free(port);

    /*
     * Create UDP socket for the first resolvable address family.
     * Unlike TCP clients, UDP sockets don't need to "connect" - we just
     * need a socket of the appropriate family for sendto() operations.
     */
    for (ai = res; ai; ai = ai->ai_next) {
		if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0)
			continue;		// ignore socket creation failure, try next address
		break;				// success - have a UDP socket
    }

    if (!ai)
		err_sys("socket");

    log_printf(log_debug, "The UDP destination is %s", ai_print_addr_port(ai));

    /*
     * Store the resolved remote address for use in sendto() calls.
     * Since UDP is connectionless, the caller needs the destination address
     * for each packet transmission. This avoids repeated address resolution.
     */
    if (remote_udpaddr)
		memcpy(remote_udpaddr, ai->ai_addr, ai->ai_addrlen);

    freeaddrinfo(res);

    return fd;
}

/**
 * Creates a TCP client connection to the specified remote address and port.
 * Attempts to connect to all resolved addresses until one succeeds, supporting
 * dual-stack connectivity. Used for establishing the TCP side of tunnel connections.
 *
 * @param s (const char*) - Remote address specification (must include both address and port)
 *                         Examples: "192.168.1.1:8080", "[2001:db8::1]:8080", "example.com:8080"
 *
 * @return int - File descriptor of the connected TCP socket
 *              Function exits on error (address resolution or connection failure)
 */
int tcp_client(const char *s)
{
    char *address, *port;
    struct addrinfo hints, *res, *ai;
    int err, fd;

    parse_address_port(s, &address, &port);

    if (!address || !port)
		log_printf_exit(2, log_err, "Missing address or port in '%s'!", s);

    /*
     * Set up address resolution hints for TCP client:
     * AF_UNSPEC: Accept both IPv4 and IPv6 addresses
     * SOCK_STREAM: TCP socket type
     * AI_ADDRCONFIG: Only return addresses for which we have network interfaces
     * AI_IDN: Support Internationalized Domain Names
     * Note: No AI_PASSIVE since this is for connecting, not binding
     */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_IDN;

    err = getaddrinfo(address, port, &hints, &res);
    if (err == EAI_SYSTEM)
		err_sys("getaddrinfo(%s:%s)", address, port);
    else if (err)
		log_printf_exit(1, log_err, "Cannot resolve %s:%s: %s", address, port, gai_strerror(err));

    /*
     * Clean up dynamically allocated memory from parse_address_port().
     * These strings were allocated with malloc/strdup and must be freed.
     */
    if (address)
		free(address);
    if (port)
		free(port);

    /*
     * Attempt to connect to each resolved address until one succeeds.
     * This implements "happy eyeballs" style connectivity - try IPv6 first
     * if available, fall back to IPv4 if IPv6 fails.
     */
    for (ai = res; ai; ai = ai->ai_next) {
		if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0)
			continue;			// ignore socket creation failure, try next address
		if (connect(fd, (struct sockaddr *) ai->ai_addr, ai->ai_addrlen) == 0)
			break;				// success - connected to remote host
		close(fd);					// connection failed, clean up and try next
    }

    if (!ai)
		err_sys("Cannot connect to %s", s);

    log_printf(log_info, "TCP connection opened to %s", ai_print_addr_port(ai));

    freeaddrinfo(res);

    return fd;
}
