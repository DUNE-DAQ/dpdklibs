# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io

moo.io.default_load_path = get_moo_model_path()

# Load configuration types
import moo.otypes

moo.otypes.load_types("dpdklibs/nicreader.jsonnet")

# Import new types
import dunedaq.dpdklibs.nicreader as nrc

from daqconf.core.app import App, ModuleGraph
from daqconf.core.daqmodule import DAQModule
from daqconf.core.conf_utils import Endpoint, Direction, Queue

# Time to waait on pop()
QUEUE_POP_WAIT_MS = 100
# local clock speed Hz
CLOCK_SPEED_HZ = 50000000


def generate(
    HOST="localhost"
):

    modules = []
    queues = []

    modules += [DAQModule(name="nic_reader", plugin="NICReceiver",
                          conf=nrc.Conf(eal_arg_list='')
        )]

    mgraph = ModuleGraph(modules, queues=queues)

    dpdk_app = App(modulegraph=mgraph, host=HOST, name="dpdk_reader")
    return dpdk_app
