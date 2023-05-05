// The schema used by classes in the appfwk code tests.
//
// It is an example of the lowest layer schema below that of the "cmd"
// and "app" and which defines the final command object structure as
// consumed by instances of specific DAQModule implementations (ie,
// the test/* modules).

local moo = import "moo.jsonnet";

// A schema builder in the given path (namespace)
local ns = "dunedaq.dpdklibs.nicreader";
local s = moo.oschema.schema(ns);

// Object structure used by the test/fake producer module
local nicreader = {
    count  : s.number("Count", "u4", doc="Count of things"),

    big_count : s.number("BigCount", "i8", doc="A count of more things"),

    id : s.number("Identifier", "i4", doc="An ID of a thingy"),

    choice : s.boolean("Choice"),

    string : s.string("String", doc="A string field"),

    ipv4:   s.string("ipv4", pattern=moo.re.ipv4, doc="ipv4 string"),

    mac:    s.string("mac", pattern="^[a-fA-F0-9]{2}(:[a-fA-F0-9]{2}){5}$", doc="mac string"),

    float : s.number("Float", "f4", doc="A float number"),

    stream_map : s.record("StreamMap", [
        s.field("source_id", self.id, 0, doc="Source ID"),
        s.field("stream_id", self.id, 0, doc="Stream ID")
    ], doc="A stream map"),
 
    src_streams_mapping : s.sequence("SrcStreamsMapping", self.stream_map, doc="A list of streams"),

    src_info : s.record("SrcGeoInfo", [
        s.field("det_id", self.id, 0, doc="Detector ID"),
        s.field("crate_id", self.id, 0, doc="Crate ID"),
        s.field("slot_id", self.id, 0, doc="Slot ID")
    ], doc="Source GeoID Information"),

    source : s.record("Source", [
        s.field("id", self.id, 0, doc="ID of a source"),
        s.field("ip_addr", self.ipv4, "192.168.0.1", doc="Source IP address"),
        s.field("lcore", self.id, 0, doc="Assigned CPU lcore"),
        s.field("rx_q", self.id, 0, doc="Assigned RX queue of interface"),
        s.field("src_info", self.src_info, doc="Source information"),
        s.field("src_streams_mapping", self.src_streams_mapping, doc="Source streams mapping")
    ], doc="Source field"),

    sources : s.sequence("Sources", self.source, doc="A list of sources"),

    iface : s.record("Interface", [
        s.field("mac_addr", self.mac, "AA:BB:CC:DD:EE:FF", doc="Logical Interface ID"),
        s.field("ip_addr", self.ipv4, "192.168.0.1", doc="IP address of interface"),
        s.field("with_flow_control", self.choice, true, doc="FlowAPI enabled"),
        s.field("promiscuous_mode", self.choice, false, doc="Promiscuous mode enabled"),
        s.field("mtu", self.count, 9000, doc="MTU of interface"),
        s.field("rx_ring_size", self.count, 1024, doc="Size of a single RX ring"),
        s.field("tx_ring_size", self.count, 1024, doc="Size of a single TX ring"),
        s.field("num_mbufs", self.count, 8191, doc="Number of total MBUFs"),
        s.field("mbuf_cache_size", self.count, 250, doc="MBUF cache size"),
        s.field("burst_size", self.count, 250, doc="RX burst size"),
        s.field("expected_sources", self.sources, doc="A list of expected sources")
    ], doc="Configuration an Ethernet interface through DPDK RTE"),

    ifaces : s.sequence("IfaceList", self.iface, doc="A list of interfaces to use"),

    conf: s.record("Conf", [
        s.field("ifaces", self.ifaces,
                doc="List of interfaces to configure"),

        s.field("eal_arg_list", self.string, "",
                doc="A string with EAL arguments")

    ], doc="Generic UIO reader DAQ Module Configuration"),

};

moo.oschema.sort_select(nicreader, ns)
