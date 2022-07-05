import socket
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('ip')

args = parser.parse_args()

UDP_TARGET_IP = args.ip
UDP_TARGET_PORT = 4444

PAYLOAD = 'hello'

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)
ret = sock.sendto(PAYLOAD.encode(), (UDP_TARGET_IP, UDP_TARGET_PORT))
print("Sent {0} bytes".format(ret))

sock.close()
