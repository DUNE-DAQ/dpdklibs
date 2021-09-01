// The schema used by classes in the appfwk code tests.
//
// It is an example of the lowest layer schema below that of the "cmd"
// and "app" and which defines the final command object structure as
// consumed by instances of specific DAQModule implementations (ie,
// the test/* modules).

local moo = import "moo.jsonnet";

// A schema builder in the given path (namespace)
local ns = "dunedaq.dpdklibs.uioreader";
local s = moo.oschema.schema(ns);

// Object structure used by the test/fake producer module
local uioreader = {
    count  : s.number("Count", "u4",
                      doc="Count of things"),

    id : s.number("Identifier", "i4",
                  doc="An ID of a thingy"),

    choice : s.boolean("Choice"),

    conf: s.record("Conf", [
        s.field("card_id", self.id, 0,
                doc="Physical card identifier (in the same host)"),

    ], doc="Generic UIO reader DAQ Module Configuration"),

};

moo.oschema.sort_select(uioreader, ns)
