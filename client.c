#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "utils.h"


int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;
    char last = 0;
    char ack = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server
    unsigned short next_seq_num = 0;
    unsigned short window_start = 0;
    struct packet window[WINDOW_SIZE];
    int acks[WINDOW_SIZE] = {0};
    int window_filled = 0;

    while (window_start < MAX_SEQUENCE) {
        // Fill the window with packets
        while (window_filled < WINDOW_SIZE && next_seq_num < MAX_SEQUENCE) {
            int bytes_read = fread(buffer, 1, PAYLOAD_SIZE, fp);
            if (bytes_read > 0) {
                build_packet(&window[window_filled], next_seq_num, 0, feof(fp), 0, bytes_read, buffer);
                sendto(send_sockfd, &window[window_filled], sizeof(window[window_filled]), 0, (struct sockaddr *)&server_addr_to, sizeof(server_addr_to));
                printSend(&window[window_filled], 0);
                acks[window_filled] = 0;
                next_seq_num++;
                window_filled++;
            } else {
                break;
            }
        }

        // Wait for ACK with timeout
        tv.tv_sec = TIMEOUT;
        setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        printf("Sending packet with sequence number: %d. %s\n", window[window_filled].seqnum, acks[window_filled] ? "Retransmission" : "New transmission");

        if (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&server_addr_from, &addr_size) < 0) {
            printf("Timeout occurred. Resending unacknowledged packets in the window.\n");
            // Timeout occurred, resend all unacknowledged packets in the window
            for (int i = 0; i < window_filled; i++) {
                if (!acks[i]) {
                    sendto(send_sockfd, &window[i], sizeof(window[i]), 0, (struct sockaddr *)&server_addr_to, sizeof(server_addr_to));
                    printSend(&window[i], 1); // Indicate that this is a resend
                }
            }
        } else {
            printf("Received ACK for sequence number: %d\n", ack_pkt.acknum);
            // Mark packet as acknowledged
            if (ack_pkt.acknum >= window_start && ack_pkt.acknum < window_start + WINDOW_SIZE) {
                acks[ack_pkt.acknum - window_start] = 1;
            }

            // Slide window forward if possible
            while (acks[0]) {
                memmove(&acks[0], &acks[1], (WINDOW_SIZE - 1) * sizeof(int));
                acks[WINDOW_SIZE - 1] = 0;
                memmove(&window[0], &window[1], (WINDOW_SIZE - 1) * sizeof(struct packet));
                window_filled--;
                window_start++;
            }
        }
    }
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

