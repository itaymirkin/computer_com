#include <winsock2.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_header.h"
#include <signal.h>
#pragma comment(lib, "Ws2_32.lib")

#define WINDOW_SIZE 5 // Sending window size for checking for collisions (ms)

int running = 1;


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

void handle_sigint(int sig) {
	printf("\nKeyboard interrupt has been detected, starting cleaning sequence\n");
	running = 0;
}
int main(int argc, char* argv[]) {
    printf("Server starting...\n");

    // Init Winsock
    WSADATA wsaData;
    int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaStartupResult != 0) {
        printf("WSAStartup failed: %d\n", wsaStartupResult);
        return 1;
    }

    // Check number of params
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <slot_time_ms>\n", argv[0]);
        WSACleanup();
        return 1;
    }

    int chan_port = atoi(argv[1]);
    int slot_time = atoi(argv[2]);

    char user_input[100];

    fd_set sock_set;
    FD_ZERO(&sock_set);

    int conflict = 0;
    int sending_id = 0;
    int server_is_sending = 0;

    SOCKET client_sock;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = slot_time * 1000; 
    
    ULONGLONG send_window_start = 0;
    ULONGLONG send_window_end;

    channel_stats_t clients_statistics[MAX_CLIENTS] = { 0 };

    signal(SIGINT, handle_sigint);

    // Create Socket 
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    printf("Socket created.\n");

    struct sockaddr_in server_addr = { 0 };
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(chan_port);

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) { // Bind socket to port
        printf("bind() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }
    printf("Socket bound to port %d.\n", chan_port);

    FD_SET(listen_sock, &sock_set); //listening socket to the set

    if (listen(listen_sock, MAX_CLIENTS) == SOCKET_ERROR) { // listen for incoming connections
        printf("listen() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }
    printf("Listening for incoming connections...\n");

    
    while (running) {

        conflict = 0;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        read_fds = sock_set;  // copy the original set

        // Set timeout for each iteration
        struct timeval current_timeout = timeout;

        int result = select(0, &read_fds, NULL, NULL, &current_timeout); // Wait for activity on the socket
        if (result == SOCKET_ERROR) {
            printf("select() failed: %d\n", WSAGetLastError());
            break;
        }
        else if (result == 0) {
            // Timeout occurred
            continue;
        }

        // Check if a new connection is available
        if (FD_ISSET(listen_sock, &read_fds)) {
            SOCKET new_sock = accept(listen_sock, NULL, NULL);
            if (new_sock == INVALID_SOCKET) {
                printf("accept() failed: %d\n", WSAGetLastError());
                continue; 
            }
            FD_SET(new_sock, &sock_set); // Add the new socket to the set
            printf("New connection accepted. Total connections: %d\n", sock_set.fd_count - 1);
        }

        send_window_end = GetTickCount64();

        if ((send_window_end - send_window_start) > WINDOW_SIZE) {
            server_is_sending = 0;
		}
        // Check for data on client sockets
        for (int i = 1; i < sock_set.fd_count; i++) {
            SOCKET current_sock = sock_set.fd_array[i];

            // Skip the listening socket
            if (current_sock == listen_sock) continue;

            if (FD_ISSET(current_sock, &read_fds)) {
                if (server_is_sending) {
                    conflict = 1;
                    clients_statistics[sending_id].collisions++; // Increment collision count for the previous collision found
                    printf("Conflict detected between clients %d and %d\n", sending_id, i);
                }
                sending_id = i;
                server_is_sending = 1;
                client_sock = current_sock;
            }
        }

        if (conflict) {
            clients_statistics[sending_id].collisions++;
            packet_header_t packet = { 0 };
            packet.type = PACKET_TYPE_NOISE;

            if (send_to_all_sockets(&sock_set, (char*)&packet, HEADER_SIZE) < 0) {
                printf("Failed to send noise packet\n");       
            }
            else {
            printf("Conflict detected, noise packet sent.\n");
            }
            
        }
        else if (server_is_sending) {
            char packet[MAX_FRAME_SIZE];
            int packet_size = recv(client_sock, packet, sizeof(packet), 0);

            if (packet_size == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error == WSAECONNRESET || error == WSAECONNABORTED) {
                    printf("Client disconnected unexpectedly\n");
                    closesocket(client_sock);
                    FD_CLR(client_sock, &sock_set);
                }
                else {
                    printf("recv() failed: %d\n", error);
                }
                continue;
            }
            else if (packet_size == 0) {
                //client disconnect
                printf("Client %d disconnected\n", sending_id);
                closesocket(client_sock);
                FD_CLR(client_sock, &sock_set);
                continue;
            }
            else if (packet_size < HEADER_SIZE) {
                fprintf(stderr, "Incomplete packet received (size: %d)\n", packet_size);
                packet_header_t noise_packet = { 0 };
                noise_packet.type = PACKET_TYPE_NOISE;

                if (send_to_all_sockets(&sock_set, (char*)&noise_packet, HEADER_SIZE) < 0) {
                    printf("Failed to send noise packet for incomplete transmission\n");
                }
                continue;
            }

            // Get the packet header
            packet_header_t* packet_header = (packet_header_t*)packet;

            // Update the client's statistics
            clients_statistics[sending_id].src_port = packet_header->src_port;

            struct in_addr src;
            src.s_addr = packet_header->src_ip; // Extract the IP address from the packet header

            inet_ntop(AF_INET, &src, clients_statistics[sending_id].src_ip, 16);
            clients_statistics[sending_id].sent++;

            printf("Received packet of size %d from client %d (IP: %s, Port: %d)\n",
                packet_size, sending_id, clients_statistics[sending_id].src_ip,
                clients_statistics[sending_id].src_port);

            if (send_to_all_sockets(&sock_set, packet, packet_size) < 0) {
                printf("Failed to forward packet to all clients\n");
                // Just continue, don't break the loop
            }
            else {

                send_window_start = GetTickCount64();

                printf("Packet successfully forwarded to all clients\n");
            }

        }
    }

    // Clean up
    for (int i = 0; i < sock_set.fd_count; i++) {
        closesocket(sock_set.fd_array[i]);
    }

    // Print Statistics
    printf("\n========== Channel Statistics ==========\n");
    int active_clients = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients_statistics[i].sent > 0) {
            active_clients++;
            printf("Client %d - IP: %s, Port: %d: %d frames, %d collisions\n",
                i, clients_statistics[i].src_ip, clients_statistics[i].src_port,
                clients_statistics[i].sent, clients_statistics[i].collisions);
        }
    }

    if (active_clients == 0) {
        printf("No active clients recorded\n");
    }

    printf("========================================\n");

    WSACleanup();
    printf("\nChannel shutting down...\n");

    return 0;
}