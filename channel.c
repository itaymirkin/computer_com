#include <winsock2.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_header.h"
#pragma comment(lib, "Ws2_32.lib")

int send_to_all_sockets(fd_set* sock_set, char* packet, int frame_size) {
	for (int i = 1; i < sock_set->fd_count; i++) {
		SOCKET sock = sock_set->fd_array[i];
			if (send(sock, packet, frame_size, 0) == SOCKET_ERROR) {
				printf("send() failed: %d\n", WSAGetLastError());
				return -1;
		}
	}
	return 0;
}

// TODO: 
// 1. Ask how to handle errors
// 2. MAX sizez
// 3. Ask about timeout behavior, do we need to use select or sleep

int main(int argc, char* argv[]) {

	//Check number of params
	if (argc != 3) {
		fprintf(stderr, "Not enough parameters - need 3");
		return 1;
	}

	int chan_port = argv[0];
	int slot_time = argv[1];
	
	char user_input[100];

	fd_set sock_set;
	FD_ZERO(&sock_set);

	int conflict		  = 0;
	int sending_id		  = 0;
	int server_is_sending = 0;

	SOCKET client_sock;

	struct timeval timeout;
	timeout.tv_sec = slot_time * 0.001;

	channel_stats_t clients_statistics[MAX_CLIENTS] = { 0 };

	// Create Socket 
	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		perror("socket");
		return 1;
	}


	struct sockaddr_in server_addr = { 0 };
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;      
	server_addr.sin_port = htons(chan_port);

	if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { // Bind socket to port
		perror("bind");
		close(listen_sock);
		return 1;
	}

	FD_SET(listen_sock, &sock_set); // Add the listening socket to the set

	if (listen(listen_sock, MAX_CLIENTS) < 0) { // Listen for incoming connections
		perror("listen");
		close(listen_sock);
		return 1;
	}

	SOCKET active_sock = accept(listen_sock, NULL, NULL); // Accept a connection

	if (active_sock < 0) {
		perror("accept");
		close(listen_sock);
		return 1;
	}

	FD_SET(active_sock, &sock_set); // Add the active socket to the set


	while (1) 
	{
		if (fgets(user_input, sizeof(user_input), stdin) == NULL) {
			// This happens when Ctrl+Z is pressed
			break;
		}

		server_is_sending = 0;
		conflict = 0;

		int result = select(0, &sock_set, NULL, NULL, NULL); // Wait for activity on the socket (need to add timeout)
		if (result < 0) {
			perror("select");
			break;
		}

		else if (result == 0) continue;

		if (FD_ISSET(listen_sock, &sock_set)) { // New connection
			SOCKET new_sock = accept(listen_sock, NULL, NULL);
			if (new_sock < 0) {
				perror("accept");
				break;
			}
			FD_SET(new_sock, &sock_set); // Add the new socket to the set
		}

		for (int i = 1; i < sock_set.fd_count; i++) 
		{
			if (FD_ISSET(sock_set.fd_array[i], &sock_set)) 
			{
				if (server_is_sending)
				{
					conflict = 1;
					clients_statistics[sending_id].collisions++; // Increment collision count for the previous collision found
				}
				sending_id = i;
				server_is_sending = 1;
				client_sock = sock_set.fd_array[i];
			}
		}

		if (conflict)
		{
			clients_statistics[sending_id].collisions++;
			packet_header_t* packet = { 0 };
			packet->type = PACKET_TYPE_NOISE;
			if (send_to_all_sockets(&sock_set, (char*)packet, HEADER_SIZE) < 0)
			{
				perror("send");
				break;
			}
		}
		else if (server_is_sending)
		{
			char packet[MAX_FRAME_SIZE];
			int packet_size = recv(client_sock, packet, sizeof(packet), 0);

			// Get the packet header
			packet_header_t* packet_header = (packet_header_t*)packet;

			// Update the client's statistics
			clients_statistics[sending_id].src_port = packet_header->src_port;

			struct in_addr src;
			src.s_addr = packet_header->src_ip; // Extract the IP address from the packet header

			inet_ntop(AF_INET, &src, clients_statistics[sending_id].src_ip, 16);
			clients_statistics[sending_id].sent++;

			if (packet_size < 0) {
				perror("recv");
				break;
			}
			else if (packet_size < HEADER_SIZE) { // Incomplete transmission
				fprintf(stderr, "Incomplete packet received\n");
				packet_header_t* packet = { 0 };
				packet->type = PACKET_TYPE_NOISE;
				packet_size = HEADER_SIZE; // Change the packet to only the size of the header
			}

			if (send_to_all_sockets(&sock_set, (char*)packet, packet_size) < 0)
			{
				perror("send");
				break;
			}
		}
	}


	// Clean up
	for (int i = 0; i < sock_set.fd_count; i++) {
		closesocket(sock_set.fd_array[i]);
	}

	// Print Statistics
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (clients_statistics[i].sent > 0) {
			fprintf(stderr, "From %s, Port: %d: %d frames, %d collisions\n",
				clients_statistics[i].src_ip, clients_statistics[i].src_port,
				clients_statistics[i].sent, clients_statistics[i].collisions);
		}
	}


	WSACleanup();

	return 0;
}