import socket
import sys

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)

s.bind(('0.0.0.0', 4444))

while True:
    data, address = s.recvfrom(4096)
    print("Server received: ", data.decode('utf-8'), "\n")
