#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h> 

#include "utils.h"

unsigned long getCurrentTimeInMicroseconds() {
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);

    // Convert seconds to microseconds and add microseconds
    unsigned long microseconds = currentTime.tv_sec * 1000000L + currentTime.tv_usec;
    return microseconds;
}

void print_window_state(short window_state[WINDOW_SIZE], int first_seq, short do_print) {
    if(!do_print)
        return;
    printf("Window state: ");
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("%d:%hd ", first_seq + i, window_state[i]);
    }
   printf("\n\n");
}

// function to create a packet with ack_n, no payload, no seq_n, no last, ack = 1
int create_ack(struct packet* pkt, unsigned short ack_n, char *sack_payload, unsigned int sack_payload_length) {
    build_packet(pkt, ack_n, 0, 1, sack_payload_length, sack_payload);
    return 0;
}

int write_packet_to_file(struct packet* pkt, FILE* fp) {
    fwrite(pkt->payload, 1, pkt->length, fp);
    fflush(fp);
    return 0;
}

void send_sack_packet( int send_sockfd, struct sockaddr_in client_addr_to, short window_state[WINDOW_SIZE], int first_seq, short do_print, packet buffer) {
    // We will do SACKS, we will send one ack and the payload will be bits 0 or 1 for each packet in the window after the first not recieved
    int sack_payload_length = WINDOW_SIZE;
    char sack_payload[sack_payload_length];

    // send the state of every packet in the window
    for (int i = 0; i < WINDOW_SIZE; i++) {
        if( window_state[i] == 0) {
            sack_payload[i] = '0';
        }else{
            sack_payload[i] = '1';
        }
    }

    if(do_print) printf("Sending ACK %d with SACK payload of length %d\n", first_seq, sack_payload_length);
    create_ack(&buffer, first_seq, sack_payload, sack_payload_length);
    sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));

    if(do_print) printf("\n\n");
}

int main() {
    // VARIABLE TO TOGGLE PRINTING AND TIMEOUT STRATEGY ---------------------------
    short do_print = 0;

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

    
    struct packet* packet_buffer[WINDOW_SIZE];

    short window_state[WINDOW_SIZE]; // 0 = not-recieved, 1 = recieved

    for (int i = 0; i < WINDOW_SIZE; i++) {
        window_state[i] = 0;
        packet_buffer[i] = NULL;
    }

    int first_seq = 0;

    struct packet recieved_packet;

    int wrote_last = 0;

    unsigned long time = getCurrentTimeInMicroseconds();
    unsigned long last_time_ack_sent = time;
    unsigned long resend_ack_time = 1000000; // 1 second


    if(do_print) printf("-------------------- SERVER -------------------\n\n\n");


    while (1) {

        time = getCurrentTimeInMicroseconds();
        
        // on a loop, if first slot in the window is recieved (1):
        while (window_state[0] == 1) {
            // write the data to the file, move all the data to the left (in window_state, window_timout, and ), append a 0, and increment first_seq.

            int payload_length = packet_buffer[0]->length;
            char payload_first_10[11];
            memcpy(payload_first_10, packet_buffer[0]->payload, 10);
            payload_first_10[10] = '\0';

            if(do_print) printf("Writing packet %d to file, and sliding window. (len = %d, first 10 characters are %s)\n", first_seq, payload_length, payload_first_10);

            write_packet_to_file(packet_buffer[0], fp);

            wrote_last = packet_buffer[0]->last;

            for (int i = 0; i < WINDOW_SIZE - 1; i++) {
                window_state[i] = window_state[i + 1];
                packet_buffer[i] = packet_buffer[i + 1];
            }
            window_state[WINDOW_SIZE - 1] = 0;
            packet_buffer[WINDOW_SIZE - 1] = NULL;
            first_seq++;

            if(do_print) printf("\n\n");
        }

        // if we have written the last packet, and the window is empty, we are done
        if (wrote_last) {
            if(do_print) printf("Wrote last packet and window is empty, so server is done.\n\n\n\n");
            break;
        }

        // check if we recieve a packet
        int recv = recvfrom(listen_sockfd, &recieved_packet, sizeof(recieved_packet), 0, (struct sockaddr *)&client_addr_from, &addr_size);
        if (recv > 0 && recieved_packet.num < WINDOW_SIZE + first_seq) {

            int seq_n = recieved_packet.num;
            int slot_affected = seq_n - first_seq;
            if(do_print) printf("Recieved Packet %d.\n", seq_n);

            if(recieved_packet.length <= 0 && recieved_packet.last == 0) {

                // This is a bit weird, but the condition above mean this is a probe packet
                if(do_print) printf("This is a probe packet, so we reply and ignore the rest.\n\n\n");
                build_packet(&buffer, 0, 0, 0, 0, "");
                sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));

            }else{
                // this is a normal packet

                // buffer packet if it is in the window
                if (slot_affected >= 0 && slot_affected < WINDOW_SIZE && window_state[slot_affected] == 0) {
                    // deep copy packet into window_buffer slot, and set window_state to 1
                    struct packet* new_packet = (struct packet*)malloc(sizeof(struct packet));
                    memcpy(new_packet, &recieved_packet, sizeof(struct packet));
                    packet_buffer[slot_affected] = new_packet;
                    window_state[slot_affected] = 1;
                }

                // send ack for packet if it is in the window
                send_sack_packet(send_sockfd, client_addr_to, window_state, first_seq, do_print, buffer);

                if ( time - last_time_ack_sent > resend_ack_time) {
                    // it's been a while since we sent an ack, so send 8 extra acks (to make sure the client gets it)
                    for (int i = 0; i < 8; i++) {
                        send_sack_packet(send_sockfd, client_addr_to, window_state, first_seq, do_print, buffer);
                    }
                    last_time_ack_sent = time;
                }
                
                last_time_ack_sent = time;
            }

        }

    }

    // printf("over with server");

    // send 20 "last" packets to the client
    for (int i = 0; i < 20; i++) {
        build_packet(&buffer, 0, 1, 1, 0, "");
        sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));
    }

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
