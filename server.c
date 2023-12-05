#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h> 

#include "utils.h"

// long getCurrentTimeInMicroseconds() {
//     struct timeval currentTime;
//     gettimeofday(&currentTime, NULL);

//     // Convert seconds to microseconds and add microseconds
//     long microseconds = currentTime.tv_sec * 1000000L + currentTime.tv_usec;
//     return microseconds;
// }

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
int create_ack(struct packet* pkt, unsigned short ack_n) {
    build_packet(pkt, 0, ack_n, 0, 1, 0, "");
    return 0;
}

int write_packet_to_file(struct packet* pkt, FILE* fp) {
    fwrite(pkt->payload, 1, pkt->length, fp);
    fflush(fp);
    return 0;
}

int main() {
    // VARIABLE TO TOGGLE PRINTING AND TIMEOUT STRATEGY ---------------------------
    short do_print = 1;

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

    for (int i = 0; i < WINDOW_SIZE; i++) {
        window_state[i] = 0;
        packet_buffer[i] = NULL;
    }

    int first_seq = 0;

    struct packet recieved_packet;

    int wrote_last = 0;

    if(do_print) printf("-------------------- SERVER -------------------\n\n\n");

    // long time_to_stop = getCurrentTimeInMicroseconds() + 1000000L * (long) (0.903633 * pow((double) max_sequence, 0.753) + 5.0);


    while (1) {
        
        // on a loop, if first slot in the window is recieved (1)
        while (window_state[0] == 1) {
            // write the data to the file, move all the data to the left (in window_state, window_timout, and ), append a 0, and increment first_seq.

            if(do_print) printf("Writing packet %d to file, and sliding window.\n", first_seq);

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

            // print_window_state(window_state, first_seq, do_print);
        }

        // if we have written the last packet, and the window is empty, we are done
        if (wrote_last) {
            if(do_print) printf("Wrote last packet and window is empty, so server is done.\n\n\n\n");
            break;
        }

        // check if we recieve a packet
        if (recvfrom(listen_sockfd, &recieved_packet, sizeof(recieved_packet), 0, (struct sockaddr *)&client_addr_from, &addr_size) > 0 && recieved_packet.seqnum < WINDOW_SIZE + first_seq) {

            int seq_n = recieved_packet.seqnum;
            int slot_affected = seq_n - first_seq;
            int ack_n = seq_n + 1;

            if(do_print) printf("Recieved Packet.\nSending ACK=%d\n", ack_n);

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

            // get first_not_recieved, the index of the first slot in the window that is not recieved
            int first_not_recieved = -1;
            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (window_state[i] == 0) {
                    first_not_recieved = i;
                    break;
                }
            }

            if (first_not_recieved >= 0 && first_not_recieved < slot_affected) {
                // send ack packet for the spot before it, that way client knows to send first_not_recieved again
                if(do_print) printf("Also Sending ACK=%d For our missing packet\n", first_seq + first_not_recieved);
                create_ack(&buffer, first_seq + first_not_recieved);
                sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));
            }

            if(do_print) printf("\n\n");

            // print_window_state(window_state, first_seq, do_print);

        }

    }

    // printf("over with server");

    // wait 100ms
    usleep(100000);

    for (int i = 0; i < 20; i++){
        if(do_print) printf("Sending final ACKS %d\n", i);
        for (int ack_n = first_seq - WINDOW_SIZE; ack_n <= first_seq; ack_n++) {
            create_ack(&buffer, ack_n);
            sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));
        }
        usleep(10000);
    }

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
