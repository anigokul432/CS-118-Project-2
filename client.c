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

// function to create a packet with seq_n,file_segments with all the strings in order.  The packet payload is bytes seq_n * PAYLOAD_SIZE to (seq_n + 1) * PAYLOAD_SIZE - 1 (or less if the end of file)
// return 0 if the packet is the last packet, 1 otherwise
int create_packet(struct packet* pkt, unsigned short seq_n, char file_segments[][PAYLOAD_SIZE], int max_sequence) {
    build_packet(pkt, seq_n, 0, 0, 0, PAYLOAD_SIZE, file_segments[seq_n]);
    if (seq_n == max_sequence - 1) {
        pkt->last = 1;
        pkt->length = strlen(file_segments[seq_n]);
        return 0;
    }
    return 1;
}

void print_window_state(short window_state[WINDOW_SIZE], int first_seq) {
    printf("Window state: ");
    for (int i = 0; i < WINDOW_SIZE; i++) {
        printf("%d:%d ", first_seq + i, window_state[i]);
    }
    printf("\n\n");
}

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);

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

    // caclulate max_sequence, = ceil(file_size / PAYLOAD_SIZE)
    fseek(fp, 0L, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    unsigned int max_sequence = (file_size + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;

    // store file segments in an array for easy access
    char file_segments[max_sequence][PAYLOAD_SIZE];

    for (unsigned int i = 0; i < max_sequence; i++) {
        fread(file_segments[i], 1, PAYLOAD_SIZE, fp);
    }

    // TODO: Read from file, and initiate reliable data transfer to the server
     // all time in is microseconds
    int concurrent_max = 5;
    long timeout_time = 210000L; // 210ms


    short window_state[WINDOW_SIZE] = {0};  // 0 = not-sent, 1 = sent, 2 = acked
    double window_timeout[WINDOW_SIZE] = {0.0};
    unsigned int first_seq = 0;

    int in_progress_count;
    long time;
    struct packet recieved_packet;

    while (first_seq < max_sequence) {

        time = getCurrentTimeInMicroseconds();
        
        // move window as long as first packet is acked (this is O(n^2) but window is so small and c is so fast that it doesn't matter)
        while (window_state[0] == 2) {

            printf("Sliding window\n");

            // shift window to the left
            for (int i = 0; i < WINDOW_SIZE - 1; i++) {
                window_state[i] = window_state[i + 1];
                window_timeout[i] = window_timeout[i + 1];
            }
            window_state[WINDOW_SIZE - 1] = 0;
            window_timeout[WINDOW_SIZE - 1] = 0.0;
            first_seq++;

            print_window_state(window_state, first_seq);
        }

        // set all timed out packets to not-sent
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (window_state[i] == 1 && time > window_timeout[i])
                window_state[i] = 0;
        }

        // calculate in_progress_count, the number of packets sent but not acked
        in_progress_count = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (window_state[i] == 1)
                in_progress_count++;
        }

        // Send at most concurrent_max-in_progress_count of the not-sent packets in the window
        for (int i = 0; i < WINDOW_SIZE && in_progress_count < concurrent_max; i++) {
            if (window_state[i] == 0 && first_seq + i < max_sequence) {
                // create packet
                struct packet pkt;
                create_packet(&pkt, first_seq + i, file_segments, max_sequence);
                // send packet
                sendto(send_sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&server_addr_to, sizeof(server_addr_to));
                // update window state
                window_state[i] = 1;
                window_timeout[i] = time + timeout_time;
                // update in_progress_count
                in_progress_count++;

                printf("Sending packet %d\n", first_seq + i);

                print_window_state(window_state, first_seq);

            }
        }

        // check if we recieve a packet
        int recv_len = recvfrom(listen_sockfd, &recieved_packet, sizeof(recieved_packet), MSG_DONTWAIT, (struct sockaddr *)&server_addr_from, &addr_size);
        if (recv_len > 0) {
            printf("Recieved ACK %d\n", recieved_packet.acknum);
            // make sure it's an ack packet (otherwise ignore packet)
            if (recieved_packet.ack) {
                unsigned short ack_n = recieved_packet.acknum;
                int slot_affected = ack_n - first_seq - 1;

                // set window state to acked if it's in the window and not already acked
                if (slot_affected >= 0 && slot_affected < WINDOW_SIZE && window_state[slot_affected] == 1) {
                    window_state[slot_affected] = 2;
                }
            }
            print_window_state(window_state, first_seq);
        }

        // check if we have recieved the last ack, and last packet is in first slot of window
        if (first_seq >= max_sequence) {
            printf("All packets have been sent and ACKED\n");
            print_window_state(window_state, first_seq);
            break;
        }
        
    }
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

