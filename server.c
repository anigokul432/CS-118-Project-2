#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int recv_len;
    struct packet ack_pkt;

    // Buffer to store out-of-order packets
    struct packet packet_buffer[MAX_SEQUENCE];
    int buffer_filled[MAX_SEQUENCE] = {0};

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

    // TODO: Receive file from the client and save it as output.txt
        
    while (1) {
        // Receive packets
        recv_len = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_from, &addr_size);
        if (recv_len > 0) {
            printRecv(&buffer);
            printf("Received packet with sequence number: %d\n", buffer.seqnum);
            // Store packet in buffer if out of order
            if (buffer.seqnum != expected_seq_num) {
                printf("Out-of-order packet. Expected sequence number: %d, but received: %d\n", expected_seq_num, buffer.seqnum);
                packet_buffer[buffer.seqnum] = buffer;
                buffer_filled[buffer.seqnum] = 1;
            } else {
                printf("Packet is in order. Expected sequence number: %d\n", expected_seq_num);
                // Write the received data to the file and check buffer for next packets
                do {
                    fwrite(buffer.payload, 1, buffer.length, fp);
                    buffer_filled[buffer.seqnum] = 0;
                    expected_seq_num++;

                    // Check next packet in buffer
                    if (buffer_filled[expected_seq_num]) {
                        buffer = packet_buffer[expected_seq_num];
                    } else {
                        break;
                    }
                } while (1);
            }

            printf("Sending ACK for sequence number: %d\n", expected_seq_num - 1);
            // Send ACK for the last in-order packet received
            build_packet(&ack_pkt, 0, expected_seq_num - 1, 0, 1, 0, NULL);
            sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));
            printSend(&ack_pkt, 0);

            // Check if this is the last packet
            if (buffer.last) {
                break;
            }
        }
    }

    

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
