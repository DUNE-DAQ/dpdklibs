#!/usr/bin/env python
import socket
import sys
import binascii
import detdataformats
import fddetdataformats
import time
import click

N_STREAM = 128
FRAME_SIZE = 7200
FRAME_TS_GAP_BDE = 2048
FRAME_TS_GAP_TDE = 2000

def print_header(wib_frame,prefix="\t"):
    header = wib_frame.get_daqheader()
    print(f'{prefix}Version: 0x{header.version:x}')
    print(f'{prefix}Detector ID: 0x{header.det_id:x}')
    print(f'{prefix}(Crate,Slot,Stream): (0x{header.crate_id:x},0x{header.slot_id:x},0x{header.stream_id:x})')
    print(f'{prefix}Timestamp: 0x{header.timestamp:x}')
    print(f'{prefix}Seq ID: {header.seq_id}') 
    print(f'{prefix}Block length: 0x{header.block_length:x}')

def dump_data(data):
    data2=data

    n_word = (len(data) // 8) +len(data) % 8
    for i in range(n_word):
        w = int.from_bytes(data[i*8:(i+1)*8], byteorder='little', signed=False)
        print(f"{i:04d} 0x{w:016x}")

@click.command()
@click.option('-d', '--dump-packet', is_flag=True, default=False)
@click.option('-w', '--words', type=int, default=8)
@click.option('-c', '--count', type=int, default=None)
@click.option('-p', '--port', type=int, default=0x4444)
@click.option('-g', '--gap', type=int, default=None)
@click.option('-f', '--frame-type', type=click.Choice(['wib', 'tde','daphne']), default=None)
def main(dump_packet, words, count, port, gap, frame_type):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)

    s.bind(('', port))

    prev_stream = {}
    i=0
    dtstart = time.time()
    dtlast = dtstart
    sampling = 100000
    match frame_type:
        case 'wib':
            frame_class = fddetdataformats.WIBEthFrame
            port = port if not port is None else 0x4444
            gap = gap if not gap is None else FRAME_TS_GAP_BDE
        case 'tde':
            frame_class = fddetdataformats.TDEEthFrame
            port = port if not port is None else 54323
            gap = gap if not gap is None else FRAME_TS_GAP_TDE
        case 'daphne':
            frame_class = fddetdataformats.DAPHNEEthFrame
            port = port if not port is None else 0x4444
            # gap = gap if not gap is None else 2048

            
    print('Receiver started')
    while (count==None or i<count):
    # while i<10:
        data, address = s.recvfrom(20000)


        if unpack_frames:

            print()
            l = 0
            l_pkt = len(data)
            frames = []
            while l < l_pkt:

                d_blk = data[l:]
                # dump_data(d_blk[0:4*8])
                h = detdataformats.DAQEthHeader(d_blk)
                print(f"len(data) = {l_pkt} block_len = {h.block_length*8:d} 0x({h.block_length:x}) [l = {l}]")
                l_frm = (h.block_length+1)*8
                frames += [d_blk[:l_frm]]
                
                l += l_frm # +1 for the header

            print(f"Scanning complete (scanned {l} over {l_pkt} bytes)")


            for i,f in enumerate(frames):
                print(f"Frame {i}")
                dump_data(f[:4*8])






        header = detdataformats.DAQEthHeader(data)

        # hdr_id = header.stream_id
        hdr_id = (header.det_id, header.crate_id, header.slot_id, header.stream_id)
        
        # if hdr_id < N_STREAM:
        stream_ts = header.timestamp
        # print(hdr_id, header.seq_id, hex(stream_ts))
        if dump_packet:

            print('----')
            print(f'Frame (size {len(data)}) from (DetID, Crate, Slot, Stream) = (0x{header.det_id}, 0x{header.crate_id:x}, 0x{header.slot_id:x}, 0x{header.stream_id:x})')
            print(f'  Timestamp: 0x{header.timestamp:x}')
            print(f'  Seq ID: {header.seq_id}, Block length: {header.block_length*8:d} (0x{header.block_length:x})')

            print()
            dump_data(data[0:words*8])
            print()

        if hdr_id not in prev_stream:
            pass
        else:
            prev_strm_ts = prev_stream[hdr_id]
            if (not gap is None) and (stream_ts - prev_strm_ts) != gap:
                print(f'delta_ts {stream_ts-prev_strm_ts} for {hdr_id} ')

        
        # if prev_stream[hdr_id] is None:
            # pass
        # elif (stream_ts-prev_stream[hdr_id]) != 2048:
            # print(f'delta_ts {stream_ts-prev_stream[hdr_id]} for det {header.det_id} strm {hdr_id} ')
        prev_stream[hdr_id] = stream_ts


        i+=1;
        if i%100000 ==0:
            dtnow = time.time()
            avg_throughput = FRAME_SIZE*i/(1000000*(dtnow-dtstart))
            throughput = FRAME_SIZE*sampling/(1000000*(dtnow-dtlast))
            print(f'Received {i} packets; throughput = {throughput:.3f} MB/s [avg = {avg_throughput:.3f} MB/s]')
            dtlast = dtnow
            print_header(wf)
            for k,ts in prev_stream.items():
                if ts is None:
                    continue
                print(f"stream {k}: last_ts {ts}")

            # b = data
            # for i in rnage(len(b)//8):
            #     w = [f"{s:02x}" for s in b[8*i:8*(i+1)]]
            #     w.reverse()
            #     print("0x"+''.join(w))

if __name__ == '__main__':
    main()

