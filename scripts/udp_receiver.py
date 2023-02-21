import socket
import sys
import binascii
import detdataformats
import time


def print_header(wib_frame,prefix="\t"):
    header = wib_frame.get_daqheader()
    print(f'{prefix}Version: 0x{header.version:x}')
    print(f'{prefix}Detector ID: 0x{header.det_id:x}')
    print(f'{prefix}(Crate,Slot,Stream): (0x{header.crate_id:x},0x{header.slot_id:x},0x{header.stream_id:x})')
    print(f'{prefix}Timestamp: 0x{header.timestamp:x}')
    print(f'{prefix}Seq ID: {header.seq_id}') 
    print(f'{prefix}Block length: 0x{header.block_length:x}')

def dump_data(data):
    print(f'Size of the message received: {len(data)}')
    data2=data
    if len(data)%2 ==1:
        data2=data[0:-1]
    print("\n".join(str(binascii.hexlify(data2,' ', bytes_per_sep=8)).split(' ')))

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)

    s.bind(('0.0.0.0', 0x4444))
    prev_stream0_ts = 0
    prev_stream1_ts = 0
    i=0
    dtstart = time.time()
    while i>=0:
        data, address = s.recvfrom(20000)
        wf = detdataformats.wibeth.WIBEthFrame(data)
        header = wf.get_daqheader()
        if header.stream_id == 0:
            stream0_ts = header.timestamp
            if (stream0_ts-prev_stream0_ts) != 2048:
                print(f'{stream0_ts-prev_stream0_ts=} for {header.stream_id=} ')
                #print_header(wf)
            prev_stream0_ts = stream0_ts
        if header.stream_id == 1:
            stream1_ts = header.timestamp
            if (stream1_ts-prev_stream1_ts) != 2048:
                print(f'{stream1_ts-prev_stream1_ts=} for {header.stream_id=} ')
                #print_header(wf)
            prev_stream1_ts = stream1_ts
        i+=1;
        if i%100000 ==0:
            dtnow = time.time()
            print(f'Received {i} packets; throughput = {7192*i/(1000000*(dtnow-dtstart)):.3f} MB/s')

if __name__ == '__main__':
    main()

