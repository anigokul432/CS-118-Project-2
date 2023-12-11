#ifndef UTILS_H
#define UTILS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACROS
#define SERVER_IP "127.0.0.1"
#define LOCAL_HOST "127.0.0.1"
#define SERVER_PORT_TO 5002
#define CLIENT_PORT 6001
#define SERVER_PORT 6002
#define CLIENT_PORT_TO 5001
#define PAYLOAD_SIZE 1197
#define WINDOW_SIZE 30


// Packet Layout
// You may change this if you want to
struct packet {
    char meta1;
    char meta2;
    char meta3;
    char payload[PAYLOAD_SIZE];
};

// Utility function to build a packet
void build_packet(struct packet* pkt, unsigned short num, bool last, bool ack, unsigned short length, const char* payload) {
    // meta1 (8 bits) | meta2 (8 bits) | meta3 (8 bits) = last (1 bit) | ack (1 bit) | length (11 bits) | num (11 bits)
    pkt->meta1 = (last << 7) | (ack << 6) | (length >> 5);
    pkt->meta2 = (length << 3) | (num >> 8);
    pkt->meta3 = num;
    
    memcpy(pkt->payload, payload, length);
}

void get_packet_info(struct packet* pkt, int* num, bool* last, bool* ack, int* length) {
    if (num != NULL) {
        *num = (unsigned short)((pkt->meta2 & 0x07) << 8) | (unsigned char)pkt->meta3;
    }
    if (last != NULL) {
        *last = (pkt->meta1 >> 7) & 0x01;
    }
    if (ack != NULL) {
        *ack = (pkt->meta1 >> 6) & 0x01;
    }
    if (length != NULL) {
        *length = (unsigned short)((pkt->meta1 & 0x1F) << 5) | (unsigned char)(pkt->meta2 >> 3);
    }
}

void print_packet_binary_meta(struct packet* pkt) {
    // Print meta data in binary
    printf("Meta data: ");
    for (int j = 7; j >= 0; j--) {
        printf("%d", (pkt->meta1 >> j) & 0x01);
    }
    printf(" ");
    for (int j = 7; j >= 0; j--) {
        printf("%d", (pkt->meta2 >> j) & 0x01);
    }
    printf(" ");
    for (int j = 7; j >= 0; j--) {
        printf("%d", (pkt->meta3 >> j) & 0x01);
    }
}

void print_packet(struct packet* pkt) {
    int num;
    bool last;
    bool ack;
    int length;
    get_packet_info(pkt, &num, &last, &ack, &length);
    printf("Packet: num=%d, last=%d, ack=%d, length=%d\n", num, last, ack, length);
}

#endif