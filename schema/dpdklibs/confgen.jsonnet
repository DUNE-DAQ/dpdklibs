
// This is the configuration schema for dpdklibs

local moo = import "moo.jsonnet";
local sdc = import "daqconf/confgen.jsonnet";
local daqconf = moo.oschema.hier(sdc).dunedaq.daqconf.confgen;

local ns = "dunedaq.dpdklibs.confgen";
local s = moo.oschema.schema(ns);

// A temporary schema construction context.
local cs = {
  number: s.number  ("number", "i8", doc="a number"),
  string : s.string("String", doc="A string field"),
  flag:            s.boolean(  "Flag", doc="Parameter that can be used to enable or disable functionality"),
  host:            s.string(   "Host", moo.re.dnshost,          doc="A hostname"),

  dpdklibs : s.record("dpdklibs", [
    s.field('only_sender',      self.flag,   default=false, doc='Enable only the sender'),
    s.field('only_reader',      self.flag,   default=false, doc='Enable only the reader'),
    s.field('host_sender',      self.host, default='np04-srv-021', doc='Host to run the sender on'),
    s.field('host_reader',      self.host, default='np04-srv-022', doc='Host to run the reader on'),
    s.field('sender_rate',      self.number, default=1, doc='Rate with which the sender sends packets'),
    s.field('sender_cores',     self.number, default=1, doc='How many cores to use for sending'),
    s.field('sender_boards',    self.number, default=1, doc='How many AMC boards to send from'),
    s.field('sender_burst_size',           self.number, default=1, doc='Burst size used for sending packets'),
    s.field('sender_time_tick_difference', self.number, default=1000, doc='How many ticks between timestamps'),
    s.field('opmon_impl', self.string, default='', doc='How many ticks between timestamps'),
    s.field('ers_impl', self.string, default='', doc='How many ticks between timestamps'),
    ]),

  dpdklibs_gen: s.record("dpdklibs_gen", [
    s.field('boot',             daqconf.boot,default=daqconf.boot, doc='Boot parameters'),
    s.field('dpdklibs', self.dpdklibs, default=self.dpdklibs, doc='dpdklibs parameters')
  ]),
};

// Output a topologically sorted array.
sdc + moo.oschema.sort_select(cs, ns)