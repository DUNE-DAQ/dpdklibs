// The schema used by classes in the appfwk code tests.
//
// It is an example of the lowest layer schema below that of the "cmd"
// and "app" and which defines the final command object structure as
// consumed by instances of specific DAQModule implementations (ie,
// the test/* modules).

local moo = import "moo.jsonnet";

// A schema builder in the given path (namespace)
local ns = "dunedaq.dpdklibs.nicsender";
local s = moo.oschema.schema(ns);

// Object structure used by the test/fake producer module
local nicsender = {
    count  : s.number("Count", "u4", doc="Count of things"),

    big_count : s.number("BigCount", "i8", doc="A count of more things"),

    id : s.number("Identifier", "i4", doc="An ID of a thingy"),

    choice : s.boolean("Choice"),

    string : s.string("String", doc="A string field"),

    float : s.number("Float", "f4", doc="A float number"),

    conf: s.record("Conf", [
        s.field("card_id", self.id, 0, doc="Physical card identifier (in the same host)"),
        s.field("eal_arg_list", self.string, doc="A string with EAL arguments"),
        s.field("number_of_cores", self.count, 1, doc="Number of cores that will be used for sending"),
        s.field("burst_size", self.big_count, 1, doc="Burst size used when sending"),
        s.field("rate", self.float, 1, doc="Rate used for the sender")

    ], doc="Generic UIO sender DAQ Module Configuration"),

};

moo.oschema.sort_select(nicsender, ns)
