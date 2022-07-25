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

    float : s.number("Float", "f4", doc="A float number"),

    link : s.record("Link", [
        s.field("id", self.id, 0, doc="Logical link ID"),
        s.field("ip", self.string, "", doc="IP address of source link"),
        s.field("rx_q", self.id, 0, doc="NIC queue for flow control"),
        s.field("lcore", self.id, 1, doc="Assigned LCore")
    ], doc=""),

    links : s.sequence("LinksList", self.link, doc="A list of links"),

    rxqs : s.sequence("RXQList", self.id, doc="A list of RX Queue IDs"),

    lcore : s.record("LCore", [
        s.field("lcore_id", self.id, 0, doc="ID of lcroe"),
        s.field("rx_qs", self.rxqs, doc="A set of RX queue IDs to process")
    ], doc=""),

    lcores : s.sequence("CoreList", self.lcore, doc="A list of lcores"),

    conf: s.record("Conf", [
        s.field("card_id", self.id, 0,
                doc="Physical card identifier (in the same host)"),

        s.field("with_drop_flow", self.choice, true,
                doc="Enable drop flow of non-UDP packets"),

        s.field("eal_arg_list", self.string, "",
                doc="A string with EAL arguments"),

        s.field("dest_ip", self.string, "",
                doc="Destination IP, usually the 100Gb input link to RU"),

        s.field("rx_cores", self.lcores,
                doc="RX core processors"),

        s.field("ip_sources", self.links, 
                doc="Enabled IP sources on NIC port")

    ], doc="Generic UIO reader DAQ Module Configuration"),

};

moo.oschema.sort_select(nicreader, ns)
