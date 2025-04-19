#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#define MAX_CLIENTS 256
#define MAX_FRAME_SIZE 4096


// Packet types
#define PACKET_TYPE_DATA 1
#define PACKET_TYPE_NOISE 2

// Header structure for packets
typedef struct {

    uint8_t type;        // 1 for data, 2 for noise
    uint32_t seq_num;    // Sequence number of the packet
    uint32_t src_ip;     // Sender's IP in network byte order
    uint16_t src_port;   // Sender's port
    uint16_t length;     // Payload length
    uint16_t MAC;        // MAC address of the sender

} packet_header_t;


typedef struct {

    char src_ip[20];       // Sender's IP in network byte order
    uint16_t src_port;     // Sender's port
    uint32_t sent;         // Number of frames sent
    uint32_t collisions;   // Number of collisions
} channel_stats_t;


#define HEADER_SIZE sizeof(packet_header_t)

#endif /* PROTOCOL_H */