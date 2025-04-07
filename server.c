#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winsock2.h"

#include "server_header.h"
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

//send fucntion

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

	// Create a pointer to the packet header
	packet_header_t* header = (packet_header_t*)packet;

	//fill header attributes
	
	header->type = PACKET_TYPE_DATA;
	header->seq_num = htonl(seq_num);
	header->src_ip = chan_addr->sin_addr.s_addr;
	header->src_port = chan_addr->sin_port;
	header->length = htons((uint16_t)(frame_size - HEADER_SIZE));

	//cp data for rest of packet
	memcpy(packet + HEADER_SIZE, data, frame_size - HEADER_SIZE);

	struct timeval start_time, end_time;
	gettimeofday(&start_time, NULL);

	while (nof_failures < max_attempts) {
		//try_sending
		if (send(socket, packet, frame_size, 0) < 0) {
			perror("send");
			return status;
		}

		status.nof_retransmit++;

		//check if sent

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(socket, &readfds);

		struct timeval tv;
		tv.tv_sec = timeout;
		tv.tv_usec = 0;

		int select_resp = select(socket + 1, &readfds, NULL, NULL, &tv);
		//error
		if (select_resp < 0) {
			perror("select");
			return status;

		}
		//timeout
		else if (select_resp == 0){
			nof_failures++;
			
			usleep(1000 * rand() % (1 << nof_failures) * slot_time); //exp backoff
			continue;
			
		}

		//wait for ack 

		char* recv_packet = (char*)malloc(frame_size);
		if (recv_packet == NULL) {  // Corrected check here
			perror("Failed to allocate memory for recv_packet");
			exit(1);
		}

		int recv_len = recv(socket, recv_packet, frame_size, 0);

		if (recv_len < 0) {
			perror("recv");
			return status;
		}
		else if (recv_len < HEADER_SIZE) {  // Incomplete transmission
			nof_failures++;
			usleep(1000 * rand() % (1 << nof_failures) * slot_time); // Exponential backoff
			continue;
		}

		packet_header_t* recv_header = (packet_header_t*)recv_packet;

		//check for errors in 

		if (recv_header == PACKET_TYPE_NOISE)
		{
			nof_failures++;
			usleep(1000 * rand() % (1 << nof_failures) * slot_time); // Exponential backoff
			continue;
		}

		// Check if our packet
		if (
			recv_header->type == PACKET_TYPE_DATA &&
			ntohl(recv_header->seq_num) == seq_num &&
			recv_header->src_ip == chan_addr->sin_addr.s_addr &&
			recv_header->src_port == chan_addr->sin_port) {
			// Success
			gettimeofday(&end_time, NULL);
			status.success = 1;
			return status;
		}
		else {
			nof_failures++;
			usleep(1000 * rand() % (1 << nof_failures) * slot_time); // Exponential backoff
			continue;

		}
		//To many attempts
		return status;
	}



}

int main(int argc, char* argv[]) {

	//Check number of params
	if (argc != 0) {
		fprintf(stderr, "Not enough parametes - need 8");
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
		perror("cant open file");
		return 1;
	}
	// get file size 
	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	rewind(file);


	
	// Create socket
	int channel_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (channel_socket < 0) {
		perror("socket");
		fclose(file);
		return 1;
	}

	struct sockaddr_in chan_addr;
	memset(&chan_addr, 0, sizeof(chan_addr));
	chan_addr.sin_family = AF_INET;
	chan_addr.sin_port = htons(chan_port);

	if (inet_pton(AF_INET, chan_ip, &chan_addr.sin_addr) <= 0) {
		perror("inet_pton");
		close(channel_socket);
		fclose(file);
		return 1;
	}

	// Connect to channel
	if (connect(channel_socket, (struct sockaddr*)&chan_addr, sizeof(chan_addr)) < 0) {
		perror("connect");
		close(channel_socket);
		fclose(file);
		return 1;
	}

	int payload_size = frame_size - HEADER_SIZE;
	int total_frames = file_size / payload_size + 1; //round up

	//data transfering

	char* payload = malloc(payload_size);
	if (!payload) {
		perror("malloc");
		close(channel_socket);
		fclose(file);
		return 1;
	}
	//time start of the packet
	struct timeval start_time, end_time;
	gettimeofday(&start_time, NULL);

	

	int seq_num = 0;
	int success = 1;  // Overall success/failure flag

	while (!feof(file))
	{
		size_t read_chunk = fread(payload, 1, payload_size, file);
		if (read_chunk == 0) break;
		
		send_st_t send_st = send_func(channel_socket, payload, (size_t*)HEADER_SIZE + read_chunk, seq_num, slot_time, timeout, chan_addr);

		if (!send_st.success) {
			success = 0;
			break;
		}

		//Statistics 
		total_bytes_sent += read_chunk;
		total_transmissions += send_st.nof_retransmit;
		
		seq_num++;

		

	}
	gettimeofday(&end_time, NULL);

	//Calc time in ms
	total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000;

	//Calc BandWidth
	double bandwidth = 0;
	if (total_time_ms > 0)
	{
		bandwidth = (total_bytes_sent * 8.0) / (total_time_ms / 1000.0) / 1000000.0;
	}
	// Display statistics
	fprintf(stderr, "Sent file %s\n", file_name);
	fprintf(stderr, "Result: %s\n", success ? "Success :)" : "Failure :(");
	fprintf(stderr, "File size: %llu Bytes (%d frames)\n", total_bytes_sent, total_frames);
	fprintf(stderr, "Total transfer time: %llu milliseconds\n", total_time_ms);


	double avg_transmissions = 0;
	if (total_frames > 0) {
		avg_transmissions = (double)total_transmissions / total_frames;
	}

	fprintf(stderr, "Transmissions/frame: average %.2f, maximum %d\n",
		avg_transmissions, max_transmissions);
	fprintf(stderr, "Average bandwidth: %.3f Mbps\n", bandwidth);

	// Clean up
	free(payload);
	close(channel_socket);
	fclose(file);

	return 0;


}