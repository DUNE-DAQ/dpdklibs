#!/usr/bin/env python

import socket
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('ip', help='ip of the destination')
parser.add_argument('--size', default=5, type=int, help='Size of the message to send (in bytes)')

args = parser.parse_args()

UDP_TARGET_IP = args.ip
UDP_TARGET_PORT = 4444

payload = 'hello' * ((args.size + 4) // 5)
payload = payload[:args.size]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)
ret = sock.sendto(payload.encode(), (UDP_TARGET_IP, UDP_TARGET_PORT))
print("Sent {0} bytes".format(ret))

sock.close()
