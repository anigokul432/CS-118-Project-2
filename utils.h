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
#define PAYLOAD_SIZE 1194
#define WINDOW_SIZE 30


// Packet Layout
// You may change this if you want to
struct packet {
    unsigned short num;
    bool ack;
    bool last;
    unsigned short length;
    char payload[PAYLOAD_SIZE];
};

// Utility function to build a packet
void build_packet(struct packet* pkt, unsigned short num, bool last, bool ack, unsigned short length, const char* payload) {
    pkt->num = num;
    pkt->ack = ack;
    pkt->last = last;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// Utility function to print a packet
void printRecv(struct packet* pkt) {
    printf("RECV %d %s%s\n", pkt->num, pkt->last ? " LAST": "", (pkt->ack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %s%s\n", pkt->num, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %s%s\n", pkt->num, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
}

#endif