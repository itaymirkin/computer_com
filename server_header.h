#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>


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
} packet_header_t;

#define HEADER_SIZE sizeof(packet_header_t)

#endif /* PROTOCOL_H */