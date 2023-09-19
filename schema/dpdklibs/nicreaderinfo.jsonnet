// This is the application info schema used by the card reader module.
// It describes the information object structure passed by the application
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.dpdklibs.nicreaderinfo");

local info = {
    double8 : s.number("double8", "f8", doc="A double of 8 bytes"),
    float8 : s.number("float8", "f8", doc="A float of 8 bytes"),

info: s.record("Info", [
    s.field("groups_sent", self.double8, 0, doc="Number of groups of frames sent"),
    s.field("total_groups_sent", self.double8, 0, doc="Total number of groups of frames sent"),
    s.field("rx_good_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_good_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_missed_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_unicast_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_multicast_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_broadcast_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_dropped_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_unknown_protocol_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_crc_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_illegal_byte_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_error_bytes", self.double8, 0, doc="Total number of"),
    s.field("mac_local_errors", self.double8, 0, doc="Total number of"),
    s.field("mac_remote_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_len_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_xon_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_xoff_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_64_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_65_to_127_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_128_to_255_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_256_to_511_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_512_to_1023_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_1024_to_1522_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_size_1523_to_max_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_undersized_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_oversize_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_mac_short_pkt_dropped", self.double8, 0, doc="Total number of"),
    s.field("rx_fragmented_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_jabber_errors", self.double8, 0, doc="Total number of"),
    s.field("rx_q0_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q1_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q2_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q3_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q4_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q5_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q6_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q7_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q8_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q9_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q10_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q11_packets", self.double8, 0, doc="Total number of"),
    s.field("rx_q0_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q1_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q2_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q3_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q4_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q5_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q6_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q7_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q8_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q9_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q10_bytes", self.double8, 0, doc="Total number of"),
    s.field("rx_q11_bytes", self.double8, 0, doc="Total number of"),
  ], doc="NIC Reader information"),

};

moo.oschema.sort_select(info)
