#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "utils.h"

long getCurrentTimeInMicroseconds() {
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);

    // Convert seconds to microseconds and add microseconds
    long microseconds = currentTime.tv_sec * 1000000L + currentTime.tv_usec;
    return microseconds;
}

void print_window_state(short window_state[WINDOW_SIZE], int first_seq) {
    printf("Window state: ");
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("%d:%d ", first_seq + i, window_state[i]);
    }
    printf("\n\n");
}


// function to create a packet with ack_n, no payload, no seq_n, no last, ack = 1
int create_ack(struct packet* pkt, unsigned short ack_n) {
    build_packet(pkt, 0, ack_n, 0, 1, 0, "");
    return 0;
}

int write_packet_to_file(struct packet* pkt, FILE* fp) {
    printf("Writing string of size %d to file in method. Payload = \"%s\"\n", pkt->length, pkt->payload);
    fwrite(pkt->payload, 1, pkt->length, fp);
    fflush(fp);
    return 0;
}

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);

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

    // Open the target file for writing (always write to output.txt, it might not exist, and we must create it)
    FILE *fp = fopen("output.txt", "w");
    if (fp == NULL) {
        perror("Could not open file");
        return 1;
    }

    // TODO: Receive file from the client and save it as output.txt
        
    struct packet* packet_buffer[WINDOW_SIZE];

    // int concurrent_max = 2;
    // long timeout_time = 210000L; // 210ms
    // long time;
    short window_state[WINDOW_SIZE]; // 0 = not-recieved, 1 = recieved
    int first_seq = 0;

    struct packet recieved_packet;

    while (1) {
        // on a loop, if first slot in the window is recieved (1)
        while (window_state[0] == 1) {
            // write the data to the file, move all the data to the left (in window_state, window_timout, and ), append a 0, and increment first_seq.

            printf("Writing packet %d to file, and sliding window. Payload = \"%s\"\n", first_seq, packet_buffer[0]->payload);

            write_packet_to_file(packet_buffer[0], fp);

            // free the memory for packet_buffer[0]
            free(packet_buffer[0]);

            for (int i = 0; i < WINDOW_SIZE - 1; i++) {
                window_state[i] = window_state[i + 1];
                packet_buffer[i] = packet_buffer[i + 1];
            }
            window_state[WINDOW_SIZE - 1] = 0;
            packet_buffer[WINDOW_SIZE - 1] = NULL;
            first_seq++;

            print_window_state(window_state, first_seq);
        }

        // check if we recieve a packet
        if (recvfrom(listen_sockfd, &recieved_packet, sizeof(recieved_packet), 0, (struct sockaddr *)&client_addr_from, &addr_size) > 0 && recieved_packet.seqnum < WINDOW_SIZE + first_seq) {

            printf("Recieved packet %d\n", recieved_packet.seqnum);
            
            int seq_n = recieved_packet.seqnum;
            int slot_affected = seq_n - first_seq;
            int ack_n = seq_n + 1;

            printf("first_seq = %d, so slot_affected = %d.\nSending ACK=%d\n", first_seq, slot_affected, ack_n);

            // send ack packet
            create_ack(&buffer, ack_n);
            sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));

            if (slot_affected >= 0 && slot_affected < WINDOW_SIZE && window_state[slot_affected] == 0) {
                // deep copy packet into window_buffer slot, and set window_state to 1
                struct packet* new_packet = malloc(sizeof(struct packet));
                memcpy(new_packet, &recieved_packet, sizeof(struct packet));
                packet_buffer[slot_affected] = new_packet;
                window_state[slot_affected] = 1;
            }

            print_window_state(window_state, first_seq);

        }

    }

    

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
