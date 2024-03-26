// This is the application info schema used by the card reader module.
// It describes the information object structure passed by the application
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.dpdklibs.nicreaderinfo");

local info = {
    // uint8 : s.number("uint8", "f8", doc="A double of 8 bytes"),
    uint8  : s.number("uint8", "u8", doc="An unsigned of 8 bytes"),
    uint4  : s.number("uint4", "u4", doc="An unsigned of 8 bytes"),
// TODO: rename to 
// info: s.record("XStats", [
ethinfo: s.record("EthInfo", [
    s.field("ipackets", self.uint8, 0, doc="New since last poll"),
    s.field("opackets", self.uint8, 0, doc="New since last poll"),
    s.field("ibytes", self.uint8, 0, doc="New since last poll"),
    s.field("obytes", self.uint8, 0, doc="New since last poll"),
    s.field("imissed", self.uint8, 0, doc="New since last poll"),
    s.field("ierrors", self.uint8, 0, doc="New since last poll"),
    s.field("oerrors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_nombuf", self.uint8, 0, doc="New since last poll"),

    ], doc="NIC Stats information"),

info: s.record("Info", [

    s.field("groups_sent", self.uint8, 0, doc="Number of groups of frames sent"),
    s.field("total_groups_sent", self.uint8, 0, doc="Total groups of frames sent"),
    s.field("rx_good_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_good_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_missed_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_unicast_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_multicast_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_broadcast_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_dropped_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_unknown_protocol_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_crc_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_illegal_byte_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_error_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("mac_local_errors", self.uint8, 0, doc="New since last poll"),
    s.field("mac_remote_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_len_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_xon_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_xoff_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_64_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_65_to_127_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_128_to_255_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_256_to_511_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_512_to_1023_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_1024_to_1522_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_size_1523_to_max_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_undersized_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_oversize_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_mac_short_pkt_dropped", self.uint8, 0, doc="New since last poll"),
    s.field("rx_fragmented_errors", self.uint8, 0, doc="New since last poll"),
    s.field("rx_jabber_errors", self.uint8, 0, doc="New since last poll"),
    s.field("x", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q1_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q2_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q3_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q4_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q5_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q6_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q7_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q8_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q9_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q10_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q11_packets", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q0_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q1_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q2_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q3_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q4_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q5_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q6_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q7_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q8_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q9_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q10_bytes", self.uint8, 0, doc="New since last poll"),
    s.field("rx_q11_bytes", self.uint8, 0, doc="New since last poll"),
  ], doc="NIC Extended Stats information"),

queue: s.record("QueueStats", [
    s.field("packets_received", self.uint8, 0, doc="Packets received"),
    s.field("bytes_received", self.uint8, 0, doc="Bytes received"),
    
    s.field("packets_dropped_spsc_full", self.uint8, 0, doc="Number of packets dropped because the spsc queue is full"),
    s.field("spsc_queue_occupancy", self.uint8, 0, doc="Number of elements in the sosc queue"),
    
    s.field("packets_copied", self.uint8, 0, doc="Packets copied"),
    s.field("bytes_copied", self.uint8, 0, doc="Bytes copied"),

    s.field("full_rx_burst", self.uint8, 0, doc="Bytes received"),
    s.field("max_burst_size", self.uint4, 0, doc="Bytes received"),
], doc="Queue Statistics"),

source: s.record("SourceStats",[
    s.field("dropped_frames", self.uint4, 0, doc="Dropped frames"),
], doc="Source Statistics"),

};

moo.oschema.sort_select(info)
