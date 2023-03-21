#!/usr/bin/env python
import socket
import sys
import binascii
import detdataformats
import time

N_STREAM = 128

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
    prev_stream = [None]*N_STREAM
    i=0
    dtstart = time.time()
    dtlast = dtstart
    sampling = 100000
    print('Starting receiver')
    while i>=0:
        data, address = s.recvfrom(20000)
        wf = detdataformats.wibeth.WIBEthFrame(data)
        header = wf.get_daqheader()


        hdr_id = header.stream_id
        if hdr_id < N_STREAM:
            stream_ts = header.timestamp
            if prev_stream[hdr_id] is None:
                pass
            elif (stream_ts-prev_stream[hdr_id]) != 2048:
                print(f'delta_ts {stream_ts-prev_stream[hdr_id]} for det {header.det_id} strm {hdr_id} ')
            prev_stream[hdr_id] = stream_ts


        i+=1;
        if i%100000 ==0:
            dtnow = time.time()
            avg_throughput = 7192*i/(1000000*(dtnow-dtstart))
            throughput = 7192*sampling/(1000000*(dtnow-dtlast))
            print(f'Received {i} packets; throughput = {throughput:.3f} MB/s [avg = {avg_throughput:.3f} MB/s]')
            dtlast = dtnow
            print_header(wf)
            for i,ts in enumerate( prev_stream ):
                if ts is None:
                    continue
                print(f"stream {i}: last_ts {ts}")

            # b = data
            # for i in range(len(b)//8):
            #     w = [f"{s:02x}" for s in b[8*i:8*(i+1)]]
            #     w.reverse()
            #     print("0x"+''.join(w))

if __name__ == '__main__':
    main()

