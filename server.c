#include <winsock2.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_header.h"
#pragma comment(lib, "Ws2_32.lib")
#define MAX_CLIENTS 256

// Global variables to track statistics
unsigned long long total_bytes_sent = 0;
unsigned long long total_time_ms = 0;
int total_frames = 0;
int total_transmissions = 0;
int max_transmissions = 0;

typedef struct {
    int success;
    int nof_retransmit;
} send_st_t;

//send function
send_st_t send_func(int socket, const void* data, size_t frame_size, int seq_num, int slot_time, int timeout, struct sockaddr_in* chan_addr) {
    send_st_t status = { 0,0 };
    int max_attempts = 10;
    int nof_failures = 0;

    //create packet
    char* packet = (char*)malloc(frame_size);
    if (packet == NULL) {
        perror("Failed to allocate memory for packet");
        exit(1);
    }
    printf("Packet created successfully.\n");
    // Create a pointer to the packet header
    packet_header_t* header = (packet_header_t*)packet;

    //fill header attributes
    header->type = PACKET_TYPE_DATA;
    header->seq_num = htonl(seq_num);
    header->src_ip = chan_addr->sin_addr.s_addr;
    header->src_port = chan_addr->sin_port;
    header->length = htons((uint16_t)(frame_size - HEADER_SIZE));
    header->MAC = rand() % 256; // Random MAC for demonstration
    printf("Header created successfully.\n");
    //cp data for rest of packet
    memcpy(packet + HEADER_SIZE, data, frame_size - HEADER_SIZE);

    LARGE_INTEGER start_time, end_time;
    QueryPerformanceCounter(&start_time);

    while (nof_failures < max_attempts) {
        //try_sending
        if (send(socket, packet, frame_size, 0) < 0) {
            perror("send");
            OutputDebugString("Send failed\n");
            return status;
        }
        
        status.nof_retransmit++;
        printf("Packet sent %d times\n", status.nof_retransmit);
        OutputDebugString("Packet sent\n");

        //check if sent
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout;
        tv.tv_usec = 0;

        int select_resp = select(socket + 1, &readfds, NULL, NULL, &tv);
        printf("Select response: %d\n", select_resp);
        //error
        if (select_resp < 0) {
            perror("select");
            OutputDebugString("Select failed\n");
            return status;
        }
        //timeout
        else if (select_resp == 0) {
            nof_failures++;
            OutputDebugString("Timeout occurred\n");
            int sleep_time = rand() % (1 << nof_failures) * slot_time;
            printf("Timeout, sleeping for %d ms\n", sleep_time);
            Sleep(sleep_time); //exp backoff
            continue;
        }

        //wait for ack
        char* recv_packet = (char*)malloc(frame_size);
        if (recv_packet == NULL) {
            perror("Failed to allocate memory for recv_packet");
            exit(1);
        }

        int recv_len = recv(socket, recv_packet, frame_size, 0);
        if (recv_len < 0) {
            perror("recv");
            OutputDebugString("Receive failed\n");
            return status;
        }
        else if (recv_len < HEADER_SIZE) {  // Incomplete transmission
            nof_failures++;
            OutputDebugString("Incomplete transmission\n");
            int sleep_time = rand() % (1 << nof_failures) * slot_time;
            printf("Timeout, sleeping for %d ms\n", sleep_time);
            Sleep(sleep_time); //exp backoff
            continue;
        }

        packet_header_t* recv_header = (packet_header_t*)recv_packet;

        //check for errors in
        if (recv_header->type == PACKET_TYPE_NOISE) {
            nof_failures++;
            printf("Received noise packet\n");
            int sleep_time = rand() % (1 << nof_failures) * slot_time;
            printf("Timeout, sleeping for %d ms\n", sleep_time);
            Sleep(sleep_time); //exp backoff
            continue;
        }

        // Check if our packet
        if (recv_header->type == PACKET_TYPE_DATA &&
            ntohl(recv_header->seq_num) == seq_num &&
            recv_header->src_ip == chan_addr->sin_addr.s_addr &&
            recv_header->src_port == chan_addr->sin_port
            ) {
            // Success
            QueryPerformanceCounter(&end_time);
            status.success = 1;
            OutputDebugString("Packet acknowledged\n");
            return status;
        }
        else {
            nof_failures++;
            
            printf("Received incorrect packet\n");
            int sleep_time = rand() % (1 << nof_failures) * slot_time;
            printf("Timeout, sleeping for %d ms\n", sleep_time);
            Sleep(sleep_time); //exp backoff
            continue;
        }
    }

    //Too many attempts
    OutputDebugString("Too many attempts, giving up\n");
    return status;
}

int main(int argc, char* argv[]) {
    Sleep(2000); //Remove 
    //Check number of params
    if (argc != 8) {
        fprintf(stderr, "Not enough parameters - need 8");
        OutputDebugString("Not enough parameters\n");
        return 1;
    }

    //assign params
    const char* chan_ip = argv[1];
    int chan_port = atoi(argv[2]);
    const char* file_name = argv[3];
    int frame_size = atoi(argv[4]);
    int slot_time = atoi(argv[5]);
    int seed = atoi(argv[6]);
    int timeout = atoi(argv[7]);

    //read file
    FILE* file = fopen(file_name, "rb");
    if (!file) {
        perror("Can't open file");
        OutputDebugString("Can't open file\n");
        return 1;
    }

    // get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        OutputDebugString("WSAStartup failed\n");
        return 1;
    }

    // Create socket
    int channel_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (channel_socket < 0) {
        perror("socket");
        fclose(file);
        OutputDebugString("Socket creation failed\n");
        return 1;
    }

    printf("Socket created successfully.\n");
    OutputDebugString("Socket created successfully\n");

    struct sockaddr_in chan_addr;
    memset(&chan_addr, 0, sizeof(chan_addr));
    chan_addr.sin_family = AF_INET;
    chan_addr.sin_port = htons(chan_port);

    if (inet_pton(AF_INET, chan_ip, &chan_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        OutputDebugString("inet_pton failed\n");
        return 1;
    }
    printf("IP address converted successfully.\n");
    // Connect to channel
    if (connect(channel_socket, (struct sockaddr*)&chan_addr, sizeof(chan_addr)) < 0) {
        perror("connect");
        closesocket(channel_socket);
        fclose(file);
        OutputDebugString("Connection failed\n");
        return 1;
    }
    printf("Connected to channel.\n");
    int payload_size = frame_size - HEADER_SIZE;
    int total_frames = file_size / payload_size + 1; //round up

    //data transferring
    char* payload = (char*)malloc(payload_size);
    if (!payload) {
        perror("malloc");
        closesocket(channel_socket);
        fclose(file);
        OutputDebugString("Memory allocation failed\n");
        return 1;
    }

    //time start of the packet
    LARGE_INTEGER start_time, end_time, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_time);

    int seq_num = 0;
    int success = 1;  // Overall success/failure flag

    while (!feof(file)) {
        printf("Sending frame %d\n", seq_num);
        size_t read_chunk = fread(payload, 1, payload_size, file);
        printf("Read chunk conrtains: %s\n", payload);
        if (read_chunk == 0) break;

        send_st_t send_st = send_func(channel_socket, payload, HEADER_SIZE + read_chunk, seq_num, slot_time, timeout, &chan_addr);
        printf("Send status: %d, retransmissions: %d\n", send_st.success, send_st.nof_retransmit);
        if (!send_st.success) {
            success = 0;
            break;
        }

        //Statistics
        total_bytes_sent += read_chunk;
        total_transmissions += send_st.nof_retransmit;

        seq_num++;
    }

    QueryPerformanceCounter(&end_time);

    //Calc time in ms
    double total_time_ms = (double)(end_time.QuadPart - start_time.QuadPart) * 1000.0 / freq.QuadPart;

    //Calc BandWidth
    double bandwidth = 0;
    if (total_time_ms > 0) {
        bandwidth = (total_bytes_sent * 8.0) / (total_time_ms / 1000.0) / 1000000.0;
    }

    // Display statistics
    fprintf(stderr, "Sent file %s\n", file_name);
    fprintf(stderr, "Result: %s\n", success ? "Success :)" : "Failure :(");
    fprintf(stderr, "File size: %llu Bytes (%d frames)\n", total_bytes_sent, total_frames);
    fprintf(stderr, "Total transfer time: %.2f milliseconds\n", total_time_ms);

    double avg_transmissions = 0;
    if (total_frames > 0) {
        avg_transmissions = (double)total_transmissions / total_frames;
    }



    fprintf(stderr, "Transmissions/frame: average %.2f, maximum %d\n",
        avg_transmissions, max_transmissions);
    fprintf(stderr, "Average bandwidth: %.3f Mbps\n", bandwidth);

    // Clean up
    free(payload);
    closesocket(channel_socket);
    fclose(file);

    return 0;
}
