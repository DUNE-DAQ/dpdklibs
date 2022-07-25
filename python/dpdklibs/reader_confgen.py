# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io

moo.io.default_load_path = get_moo_model_path()

# Load configuration types
import moo.otypes

moo.otypes.load_types("dpdklibs/nicreader.jsonnet")

moo.otypes.load_types("readoutlibs/sourceemulatorconfig.jsonnet")
moo.otypes.load_types("readoutlibs/readoutconfig.jsonnet")
moo.otypes.load_types("readoutlibs/recorderconfig.jsonnet")

# Import new types
import dunedaq.dpdklibs.nicreader as nrc
import dunedaq.readoutlibs.sourceemulatorconfig as sec
import dunedaq.readoutlibs.readoutconfig as rconf
import dunedaq.readoutlibs.recorderconfig as bfs


from daqconf.core.app import App, ModuleGraph
from daqconf.core.daqmodule import DAQModule
from daqconf.core.conf_utils import Endpoint, Direction, Queue

# Time to wait on pop()
QUEUE_POP_WAIT_MS = 100
# local clock speed Hz
CLOCK_SPEED_HZ = 50000000


def generate(
    HOST='localhost',
    ENABLE_SOFTWARE_TPG=False,
    NUMBER_OF_GROUPS=1,
    NUMBER_OF_LINKS_PER_GROUP=4,
    NUMBER_OF_DATA_PRODUCERS=1*4,
    BASE_SOURCE_IP="10.73.139.",
    DESTINATION_IP="10.73.139.17",
    FRONTEND_TYPE='tde',
    EAL_ARGS='',

):

    modules = []
    queues = []

    links = []
    rxcores = []
    lid=0
    last_ip=100
    for group in range(NUMBER_OF_GROUPS):
        offset=0
        qlist=[]
        for src in range(NUMBER_OF_LINKS_PER_GROUP):
            links.append( nrc.Link(id=lid, ip=BASE_SOURCE_IP+str(last_ip), rx_q=lid, lcore=group+1) )
            qlist.append(lid)
            lid+=1
            last_ip+=1
        offset+=NUMBER_OF_LINKS_PER_GROUP
        rxcores.append( nrc.LCore(lcore_id=group+1, rx_qs=qlist) )

    modules += [DAQModule(name="nic_reader", plugin="NICReceiver",
                          conf=nrc.Conf(eal_arg_list=EAL_ARGS, dest_ip=DESTINATION_IP, rx_cores=rxcores, ip_sources=links)
        )]

    queues += [Queue(f"nic_reader.output_{idx}",f"datahandler_{idx}.raw_input",f'{FRONTEND_TYPE}_link_{idx}', 100000) for idx in range(NUMBER_OF_DATA_PRODUCERS)]

    for idx in range(NUMBER_OF_DATA_PRODUCERS):
        if ENABLE_SOFTWARE_TPG:
            queues += [Queue(f"datahandler_{idx}.tp_out",f"sw_tp_handler_{idx}.raw_input",f"sw_tp_link_{idx}",100000 )]                

        modules += [DAQModule(name=f"datahandler_{idx}", plugin="DataLinkHandler", conf=rconf.Conf(
                    readoutmodelconf=rconf.ReadoutModelConf(
                        source_queue_timeout_ms=QUEUE_POP_WAIT_MS,
                        fake_trigger_flag=1,
                        region_id=0,
                        element_id=idx,
                        timesync_connection_name = f"timesync_dlh_{idx}",
                        timesync_topic_name = "Timesync",
                    ),
                    latencybufferconf=rconf.LatencyBufferConf(
                        latency_buffer_size=1000,
                        region_id=0,
                        element_id=idx,
                    ),
                    rawdataprocessorconf=rconf.RawDataProcessorConf(
                        region_id=0,
                        element_id=idx,
                        enable_software_tpg=ENABLE_SOFTWARE_TPG,
                        error_counter_threshold=100,
                        error_reset_freq=10000,
                    ),
                    requesthandlerconf=rconf.RequestHandlerConf(
                        latency_buffer_size=1000,
                        pop_limit_pct=0.8,
                        pop_size_pct=0.1,
                        region_id=0,
                        element_id=idx,
                        output_file=f"output_{idx}.out",
                        stream_buffer_size=8388608,
                        enable_raw_recording=True,
                    ),
                ), extra_commands={"record": rconf.RecordingParams(duration=10)})]

    modules += [DAQModule(name="timesync_consumer", plugin="TimeSyncConsumer")]
    modules += [DAQModule(name="fragment_consumer", plugin="FragmentConsumer")]

    mgraph = ModuleGraph(modules, queues=queues)

    for idx in range(NUMBER_OF_DATA_PRODUCERS):
        mgraph.connect_modules(f"datahandler_{idx}.timesync_output", "timesync_consumer.input_queue", "timesync_q")
        mgraph.connect_modules(f"datahandler_{idx}.fragment_queue", "fragment_consumer.input_queue", "data_fragments_q", 100)
        mgraph.add_endpoint(f"requests_{idx}", f"datahandler_{idx}.request_input", Direction.IN)
        mgraph.add_endpoint(f"requests_{idx}", None, Direction.OUT) # Fake request endpoint

    dpdk_app = App(modulegraph=mgraph, host=HOST, name="dpdk_reader")
    return dpdk_app
