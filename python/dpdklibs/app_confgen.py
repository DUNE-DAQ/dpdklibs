# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io

moo.io.default_load_path = get_moo_model_path()

# Load configuration types
import moo.otypes

moo.otypes.load_types("cmdlib/cmd.jsonnet")
moo.otypes.load_types("rcif/cmd.jsonnet")
moo.otypes.load_types("appfwk/cmd.jsonnet")
moo.otypes.load_types("appfwk/app.jsonnet")
moo.otypes.load_types("dpdklibs/nicreader.jsonnet")

# Import new types
import dunedaq.cmdlib.cmd as basecmd  # AddressedCmd,
import dunedaq.rcif.cmd as rccmd  # AddressedCmd,
import dunedaq.appfwk.app as app  # AddressedCmd,
import dunedaq.appfwk.cmd as cmd  # AddressedCmd,
import dunedaq.dpdklibs.nicreader as nir

from appfwk.utils import mcmd, mrccmd, mspec

import json
import math

def generate(
        RUN_NUMBER=333
):

    # Define modules and queues
    queue_bare_specs = (
        [
            app.QueueSpec(inst="dummy_q", kind="FollySPSCQueue", capacity=10),
        ]
    )

    # Only needed to reproduce the same order as when using jsonnet
    queue_specs = app.QueueSpecs(sorted(queue_bare_specs, key=lambda x: x.inst))

    mod_specs = (
        [
            mspec(
                "nicreader",
                "NICReader",
                [
                    app.QueueInfo(
                        name="dummy_queue",
                        inst="dummy_q",
                        dir="output",
                    )
                ]
            )
        ]
    )

    init_specs = app.Init(queues=queue_specs, modules=mod_specs)

    jstr = json.dumps(init_specs.pod(), indent=4, sort_keys=True)
    print(jstr)

    initcmd = rccmd.RCCommand(
        id=basecmd.CmdId("init"),
        entry_state="NONE",
        exit_state="INITIAL",
        data=init_specs,
    )

    confcmd = mrccmd(
        "conf",
        "INITIAL",
        "CONFIGURED",
        [
            (
                "nicreader",
                nir.Conf(
                    card_id=0,
                    arg_list=(
                        "-n",
                        "4",
                        "-l",
                        "0-3"
                    )
                )
            )
        ]
    )

    jstr = json.dumps(confcmd.pod(), indent=4, sort_keys=True)
    print(jstr)

    startpars = rccmd.StartParams(run=RUN_NUMBER)
    startcmd = mrccmd(
        "start",
        "CONFIGURED",
        "RUNNING",
        [
            ("nicreader", startpars),
        ],

    )

    jstr = json.dumps(startcmd.pod(), indent=4, sort_keys=True)
    print("=" * 80 + "\nStart\n\n", jstr)

    stopcmd = mrccmd(
        "stop",
        "RUNNING",
        "CONFIGURED",
        [
            ("nicreader", None),
        ],
    )
    jstr = json.dumps(stopcmd.pod(), indent=4, sort_keys=True)
    print("=" * 80 + "\nStop\n\n", jstr)

    scrapcmd = mrccmd("scrap", "CONFIGURED", "INITIAL", [("", None)])

    jstr = json.dumps(scrapcmd.pod(), indent=4, sort_keys=True)
    print("=" * 80 + "\nScrap\n\n", jstr)

    # Create a list of commands
    cmd_seq = [initcmd, confcmd, startcmd, stopcmd, scrapcmd]

    # Print them as json (to be improved/moved out)
    jstr = json.dumps([c.pod() for c in cmd_seq], indent=4, sort_keys=True)
    return jstr

if __name__ == '__main__':
    # Add -h as default help option
    CONTEXT_SETTINGS = dict(help_option_names=['-h', '--help'])

    import click

    @click.command(context_settings=CONTEXT_SETTINGS)
    @click.option('-r', '--run-number', default=333)
    @click.argument('json_file', type=click.Path(), default='dpdk-app.json')
    def cli(run_number, json_file):
        """
          JSON_FILE: Input raw data file.
          JSON_FILE: Output json configuration file.
        """
        with open(json_file, 'w') as f:
            f.write(generate(
                RUN_NUMBER = run_number,
            ))

        print(f"'{json_file}' generation completed.")

cli()