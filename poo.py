utils.py:

```python
# utils.py

class Packet:
    def __init__(self, seqnum, acknum, ack, last, length, payload):
        self.seqnum = seqnum
        self.acknum = acknum
        self.ack = ack
        self.last = last
        self.length = length
        self.payload = payload

def build_packet(seqnum, acknum, ack, last, length, payload):
    return Packet(seqnum, acknum, ack, last, length, payload)

def print_recv(packet):
    print(f"RECV {packet.seqnum} {packet.acknum}{' LAST' if packet.last else ''}{' ACK' if packet.ack else ''}")

def print_send(packet, resend):
    if resend:
        print(f"RESEND {packet.seqnum} {packet.acknum}{' LAST' if packet.last else ''}{' ACK' if packet.ack else ''}")
    else:
        print(f"SEND {packet.seqnum} {packet.acknum}{' LAST' if packet.last else ''}{' ACK' if packet.ack else ''}")
```

server.py:

```python
# server.py

import socket
from utils import *

def main():
    listen_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_addr = ('', SERVER_PORT)
    client_addr_from = ('', 0)
    client_addr_to = (LOCAL_HOST, CLIENT_PORT_TO)
    addr_size = 0
    expected_seq_num = 0
    buffer = Packet(0, 0, 0, 0, 0, b'')
    recv_len = 0
    ack_pkt = Packet(0, 0, 0, 0, 0, b'')

    # Buffer to store out-of-order packets
    packet_buffer = [Packet(0, 0, 0, 0, 0, b'')] * MAX_SEQUENCE
    buffer_filled = [0] * MAX_SEQUENCE

    send_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listen_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    listen_sockfd.bind(server_addr)

    fp = open("output.txt", "wb")

    while True:
        recv_len, client_addr_from = listen_sockfd.recvfrom(1024)
        buffer = Packet.from_bytes(recv_len)

        print_recv(buffer)
        print(f"Received packet with sequence number: {buffer.seqnum}")

        if buffer.seqnum != expected_seq_num:
            print(f"Out-of-order packet. Expected sequence number: {expected_seq_num}, but received: {buffer.seqnum}")
            packet_buffer[buffer.seqnum] = buffer
            buffer_filled[buffer.seqnum] = 1
        else:
            print(f"Packet is in order. Expected sequence number: {expected_seq_num}")

            while True:
                fp.write(buffer.payload)
                buffer_filled[buffer.seqnum] = 0
                expected_seq_num += 1

                if buffer_filled[expected_seq_num]:
                    buffer = packet_buffer[expected_seq_num]
                else:
                    break

        print(f"Sending ACK for sequence number: {expected_seq_num - 1}")
        ack_pkt = build_packet(0, expected_seq_num - 1, 0, 1, 0, b'')
        send_sockfd.sendto(ack_pkt.to_bytes(), client_addr_to)
        print_send(ack_pkt, 0)

        if buffer.last:
            break

    fp.close()
    listen_sockfd.close()
    send_sockfd.close()

if __name__ == "__main__":
    main()
```

client.py:

```python
# client.py

import socket
import time
from utils import *

def main():
    listen_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client_addr = ('', CLIENT_PORT)
    server_addr_to = (SERVER_IP, SERVER_PORT_TO)
    server_addr_from = ('', 0)
    addr_size = 0
    pkt = Packet(0, 0, 0, 0, 0, b'')
    ack_pkt = Packet(0, 0, 0, 0, 0, b'')
    buffer = b''
    seq_num = 0
    ack_num = 0
    last = 0
    ack = 0

    if len(sys.argv) != 2:
        print("Usage: ./client <filename>")
        return 1
    filename = sys.argv[1]

    listen_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    server_addr_to = (SERVER_IP, SERVER_PORT_TO)
    client_addr = ('', CLIENT_PORT)

    listen_sockfd.bind(client_addr)

    fp = open(filename, "rb")

    next_seq_num = 0
    window_start = 0
    window = [Packet(0, 0, 0, 0, 0, b'')] * WINDOW_SIZE
    acks = [0] * WINDOW_SIZE
    window_filled = 0

    while window_start < MAX_SEQUENCE:
        while window_filled < WINDOW_SIZE and next_seq_num < MAX_SEQUENCE:
            bytes_read = fp.read(PAYLOAD_SIZE)
            if bytes_read > 0:
                window[window_filled] = build_packet(next_seq_num, 0, feof(fp), 0, bytes_read, buffer)
                send_sockfd.sendto(window[window_filled].to_bytes(), server_addr_to)
                print_send(window[window_filled], 0)
                acks[window_filled] = 0
                next_seq_num += 1
                window_filled += 1
            else:
                break

        tv = TIMEOUT
        listen_sockfd.settimeout(tv)

        try:
            ack_pkt, server_addr_from = listen_sockfd.recvfrom(1024)
        except socket.timeout:
            for i in range(window_filled):
                if not acks[i]:
                    send_sockfd.sendto(window[i].to_bytes(), server_addr_to)
                    print_send(window[i], 1)

        else:
            if ack_pkt.acknum >= window_start and ack_pkt.acknum < window_start + WINDOW_SIZE:
                acks[ack_pkt.acknum - window_start] = 1

            while acks[0]:
                acks = acks[1:] + [0]
                window = window[1:] + [Packet(0, 0, 0, 0, 0, b'')]
                window_filled -= 1
                window_start += 1

    fp.close()
    listen_sockfd.close()
    send_sockfd.close()

if __name__ == "__main__":
    main()
```