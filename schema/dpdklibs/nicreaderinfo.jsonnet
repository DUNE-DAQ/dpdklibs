// This is the application info schema used by the card reader module.
// It describes the information object structure passed by the application
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.dpdklibs.nicreaderinfo");

local info = {
    uint8  : s.number("uint8", "u8",
        doc="An unsigned of 8 bytes"),
    float8 : s.number("float8", "f8",
        doc="A float of 8 bytes"),

info: s.record("LinkInfo", [
    s.field("card_id", self.uint8, 0, doc="Card ID")
  ], doc="Bla bla")
};

moo.oschema.sort_select(info)
