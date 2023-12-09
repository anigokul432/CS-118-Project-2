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

// function to create a packet with seq_n,file_segments with all the strings in order.  The packet payload is bytes seq_n * PAYLOAD_SIZE to (seq_n + 1) * PAYLOAD_SIZE - 1 (or less if the end of file)
// return 0 if the packet is the last packet, 1 otherwise
int create_packet(struct packet* pkt, int seq_n, char file_segments[][PAYLOAD_SIZE], long max_sequence, long file_size) {
    build_packet(pkt, seq_n, 0, 0, 0, PAYLOAD_SIZE, file_segments[seq_n]);
    if (seq_n == max_sequence - 1) {
        pkt->last = 1;
        pkt->length = (file_size % PAYLOAD_SIZE);
        if (pkt->length == 0)
            pkt->length = PAYLOAD_SIZE;
        return 0;
    }
    return 1;
}

int probe_timeout( int listen_sockfd, int send_sockfd, struct packet recieved_packet, struct sockaddr_in server_addr_to, struct sockaddr_in server_addr_from, socklen_t addr_size, short do_print) {

    unsigned long time = getCurrentTimeInMicroseconds();
    unsigned long probe_timeout_time = 1000000L; // resend probe after 1 second
    unsigned long time_sent_probe = getCurrentTimeInMicroseconds() - probe_timeout_time;

    int recv_len = 0;

    do{

        time = getCurrentTimeInMicroseconds();

        // send probe packet if it's time
        if (time > time_sent_probe + probe_timeout_time){
            if(do_print) printf("Sending probe packet\n");
            struct packet pkt;
            build_packet(&pkt, 0, 0, 0, 0, 0, ""); 
            sendto(send_sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&server_addr_to, sizeof(server_addr_to));
            time_sent_probe = getCurrentTimeInMicroseconds();
        }
        
        // check if we recieve a packet
        recv_len = recvfrom(listen_sockfd, &recieved_packet, sizeof(recieved_packet), MSG_DONTWAIT, (struct sockaddr *)&server_addr_from, &addr_size);
    } while (recv_len <= 0);

    // calculate timeout_time = 1.5 * RTT
    unsigned long timeout_time = (getCurrentTimeInMicroseconds() - time_sent_probe) * 1.5;

    return timeout_time;
}

void print_window_state(short window_state[WINDOW_SIZE], int first_seq, short do_print) {
    if(!do_print)
        return;

    printf("Window state: ");
    for (int i = 0; i < WINDOW_SIZE; i++) {
       printf("%d:%d ", first_seq + i, window_state[i]);
    }
   printf("\n\n");
}

long ceiled_div(long a, long b) {
    return a/b + (a % b != 0);
}

int main(int argc, char *argv[]) {
    
    // VARIABLE TO TOGGLE PRINTING AND TIMEOUT STRATEGY ---------------------------
    short do_print = 0;
    short do_timeout_estimation = 0;
    short do_probe_timeout = 0;

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
    long file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    long max_sequence = ceiled_div(file_size, PAYLOAD_SIZE);


    // store file segments in an array for easy access
    char file_segments[max_sequence][PAYLOAD_SIZE];


    // read file into file_segments
    for (int i = 0; i < max_sequence; i++) {
        if(i == max_sequence - 1) {

            int length_of_last_segment = file_size % PAYLOAD_SIZE;
            if (length_of_last_segment == 0)
                length_of_last_segment = PAYLOAD_SIZE;

            // Last Segment's size is the remainder of file_size / PAYLOAD_SIZE
            fread(file_segments[i], sizeof(char), length_of_last_segment, fp);
        } else {

            // Any Segment that is not the last segment is of size PAYLOAD_SIZE
            fread(file_segments[i], sizeof(char), PAYLOAD_SIZE, fp);
        }
    }


    // all time in is microseconds = 10^-6 seconds
    int concurrent = 1; // number of packets we can send at once, normally cwnd would vary but this is equivalent, since window size is large
    short is_slow_start = 1;
    int slow_start_threshold = 8;
    int concurrent_max = WINDOW_SIZE; // the absolute maximum number of packets we can send at once
    int ack_dupe_limit = 3;


    short window_state[WINDOW_SIZE];  // 0 = not-sent, 1 = sent, 2 = acked
    int ack_count[WINDOW_SIZE]; // number of times we have recieved an ack for this packet, used for congestion control
    unsigned long window_time_sent[WINDOW_SIZE]; // tracks the time the packet was sent, used for timeout detection

    // initialize window_state, ack_count, and window_time_sent
    for (int i = 0; i < WINDOW_SIZE; i++) {
        window_state[i] = 0;
        ack_count[i] = 0;
        window_time_sent[i] = 0L;
    }

    unsigned int first_seq = 0;

    int in_progress_count;
    unsigned long time = getCurrentTimeInMicroseconds();
    struct packet recieved_packet;

    unsigned long time_of_last_timeout = 0L;

    // ----- PROBE PACKET -----

    unsigned long timeout_time = 1000000L; // 1 second

    if(do_probe_timeout) {
        if(do_print) printf("Probing for timeout\n");
        timeout_time = probe_timeout(listen_sockfd, send_sockfd, recieved_packet, server_addr_to, server_addr_from, addr_size, do_print);
        if(do_print) printf("Timeout time is %ld\n", timeout_time);
    }


    // ----- MAIN LOOP -----


    if(do_print) printf("-------------------- CLIENT -------------------\n");

    if(do_print) printf("Initial timeout_time: %ld\nInitial concurrent=%d\nFile is %ld bytes long, and will be sent into %ld packets total\n\n\n", timeout_time, concurrent, file_size, max_sequence);

    // first_seq == max_sequence means the last packet has been acked
    while (first_seq < max_sequence) {

        time = getCurrentTimeInMicroseconds();
        
        // slide window as long as first packet is acked
        while (window_state[0] == 2) {

           if(do_print) printf("First Slot is ACKED, sliding window\n");

            // shift window to the left
            for (int i = 0; i < WINDOW_SIZE - 1; i++) {
                window_state[i] = window_state[i + 1];
                window_time_sent[i] = window_time_sent[i + 1];
                ack_count[i] = ack_count[i + 1];
            }
            // set last slot to not-sent
            window_state[WINDOW_SIZE - 1] = 0;
            window_time_sent[WINDOW_SIZE - 1] = 0L;
            ack_count[WINDOW_SIZE - 1] = 0;

            first_seq++;

            if(do_print) printf("\n\n");
        }

        // set all timed out packets to not-sent
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (window_state[i] == 1 && time > window_time_sent[i] + timeout_time){
                window_state[i] = 0;

                // Timout estimation
                if (do_timeout_estimation){
                    timeout_time *= 1.01;
                    if (timeout_time > 500000L)
                        timeout_time = 500000L;
                }
                
                if (time > time_of_last_timeout + 2*timeout_time)
                {
                    // Congestion Control (Timeout)
                    slow_start_threshold = concurrent * 0.5;
                    if (slow_start_threshold < 1)
                        slow_start_threshold = 1;
                    concurrent = 1;

                    time_of_last_timeout = time;

                    if(do_print) printf("Packet %d timed out. It took %ld ms, timeout=%ld, concurrent=%d, slow_start_threshold=%d\n\n\n", first_seq + i, time - window_time_sent[i], timeout_time, concurrent, slow_start_threshold);

                } else {
                    
                    // here we are in the case where we have timed out, but we have already timed out recently
                    if(do_print) printf("Packet %d timed out. But already timed out recently, so ignoring. \n\n\n", first_seq + i);
                }
                
            }
        }

        // calculate in_progress_count, the number of packets sent but not acked
        in_progress_count = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (window_state[i] == 1)
                in_progress_count++;
        }

        // Send as many packets as possible ( <= concurrent )
        for (int i = 0; i < WINDOW_SIZE && in_progress_count < concurrent; i++) {
            if (window_state[i] == 0 && first_seq + i < max_sequence) {

                // create packet
                struct packet pkt;
                create_packet(&pkt, first_seq + i, file_segments, max_sequence, file_size);

                // send packet
                sendto(send_sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&server_addr_to, sizeof(server_addr_to));

                // update window state
                window_state[i] = 1;
                window_time_sent[i] = time;

                // update in_progress_count
                in_progress_count++;

                if(do_print) printf("Sending packet %d - sending availability is now = %d/%d\n\n\n", first_seq + i, in_progress_count, concurrent);
            }
        }

        // check if we recieve a packet
        int recv_len = recvfrom(listen_sockfd, &recieved_packet, sizeof(recieved_packet), MSG_DONTWAIT, (struct sockaddr *)&server_addr_from, &addr_size);
        if (recv_len > 0) {
            
            // make sure it's an ack packet (otherwise ignore packet)
            if (recieved_packet.ack) {
                unsigned short ack_n = recieved_packet.acknum;
                int slot_demanded = ack_n - first_seq;

                ack_count[slot_demanded]++;

                // go through all the packets in the window that are before the acked packet, and set them to acked
                for(int i = 0; i < slot_demanded; i++) {
                    
                    if (window_state[i] != 2) {

                        window_state[i] = 2;
                        ack_count[i] = 0;

                        // Timeout estimation
                        if (do_timeout_estimation)
                            timeout_time = (timeout_time * 0.9) + ((getCurrentTimeInMicroseconds() - window_time_sent[i]) * 0.1);

                        // Congestion Control (ACKed packet)
                        if (is_slow_start == 1) { 
                            concurrent *= 2;
                            if (concurrent >= slow_start_threshold) {
                                is_slow_start = 0;
                                concurrent = slow_start_threshold;
                            }
                        } else {
                            // congestion avoidance
                            if (concurrent < concurrent_max) {
                                concurrent++;
                            }
                        }

                         if(do_print) printf("Recieved ACK %d, packet arrived. timeout=%ld, concurrent=%d, slow_start_threshold=%d\n", recieved_packet.acknum, timeout_time, concurrent, slow_start_threshold);

                    }
                
                }

                if (slot_demanded >= 0 && slot_demanded < WINDOW_SIZE && ack_count[slot_demanded] >= ack_dupe_limit) {
                    
                    ack_count[slot_demanded] = 0;
                
                    // set packet to not-sent
                    window_state[slot_demanded] = 0;
                    
                    // Congestion Control (Duplicate ACK)
                    slow_start_threshold = concurrent * 0.5;
                    if (slow_start_threshold < 1)
                        slow_start_threshold = 1;

                    concurrent = slow_start_threshold + 2;

                    // Ensure concurrent does not exceed max window size
                    if (concurrent > concurrent_max) concurrent = concurrent_max;

                    if(do_print) printf("Duplicate ACK on packet %d. concurrent=%d, slow_start_threshold=%d\n", first_seq + slot_demanded, concurrent, slow_start_threshold);
                }

                if(do_print) printf("\n\n");
            }
        }

        // check if we have recieved the last ack, and last packet is in first slot of window
        if (first_seq >= max_sequence) {
            if(do_print) printf("All packets have been sent and ACKED, so we are done\n\n\n\n\n\n");
            break;
        }
        
    }
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

