// This describes the variables we want reported to opmon, etc., which
// give an overview of the rate and quality of packets being received
// using the tools in this package

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.dpdklibs.receiverinfo");

local info = {
   int4 :    s.number(  "int4",    "i4",          doc="A signed integer of 4 bytes"),
   uint4 :   s.number(  "uint4",   "u4",          doc="An unsigned integer of 4 bytes"),
   int8 :    s.number(  "int8",    "i8",          doc="A signed integer of 8 bytes"),
   uint8 :   s.number(  "uint8",   "u8",          doc="An unsigned integer of 8 bytes"),
   float4 :  s.number(  "float4",  "f4",          doc="A float of 4 bytes"),
   double8 : s.number(  "double8", "f8",          doc="A double of 8 bytes"),
   boolean:  s.boolean( "Boolean",                doc="A boolean"),
   string:   s.string(  "String",                 doc="A string"),   

   info: s.record("Info", [
       s.field("total_packets", self.int8, -999, doc="Total number of packets received"),
       s.field("packets_per_second", self.double8, -999, doc="Packets/s"),
       s.field("bytes_per_second", self.double8, -999, doc="Bytes/s"),
       s.field("bad_ts_packets_per_second", self.double8, -999, doc="Packets/s with an unexpected timestamp"),
       s.field("max_bad_ts_deviation", self.int8, -999, doc="Largest difference found between expected and actual packet timestamp"),
       s.field("bad_seq_id_packets_per_second", self.double8, -999, doc="Packets/s with an unexpected sequence ID"),
       s.field("max_bad_seq_id_deviation", self.int8, -999, doc="Largest difference found between expected and actual packet sequence ID"),
       s.field("bad_size_packets_per_second", self.double8, -999, doc="Packets/s with an unexpected payload size"),
       s.field("max_packet_size",  self.int8, -999, doc="Maximum bytes for a packet"),
       s.field("min_packet_size",  self.int8, -999, doc="Minimum bytes for a packet"),
   ], doc="Packet receiver info")
};

moo.oschema.sort_select(info)

