#!/usr/bin/env python3

import json
import os
import rich.traceback
from rich.console import Console
from os.path import exists, join
from daqconf.core.system import System
from daqconf.core.conf_utils import make_app_command_data
from daqconf.core.metadata import write_metadata_file
from daqconf.core.config_file import generate_cli_from_schema

console = Console()

# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io
moo.io.default_load_path = get_moo_model_path()

import click

# Add -h as default help option
CONTEXT_SETTINGS = dict(help_option_names=['-h', '--help'])
@click.command(context_settings=CONTEXT_SETTINGS)
@generate_cli_from_schema('dpdklibs/confgen.jsonnet', 'dpdklibs_gen')
@click.argument('json_dir', type=click.Path())
@click.option('--debug', default=False, is_flag=True, help="Switch to get a lot of printout and dot files")
def cli(config, json_dir, debug):

    if exists(json_dir):
        raise RuntimeError(f"Directory {json_dir} already exists")

    config_data = config[0]
    config_file = config[1]

    # Get our config objects
    moo.otypes.load_types('dpdklibs/confgen.jsonnet')
    import dunedaq.dpdklibs.confgen as confgen
    moo.otypes.load_types('daqconf/confgen.jsonnet')
    import dunedaq.daqconf.confgen as daqconfgen

    boot = daqconfgen.boot(**config_data.boot)
    dpdklibs = confgen.dpdklibs(**config_data.dpdklibs)

    # Validate options
    if dpdklibs.only_sender and dpdklibs.only_reader:
        raise RuntimeError('Both options only_sender and only_reader can not be specified at the same time')

    if dpdklibs.sender_boards % dpdklibs.sender_cores:
        raise RuntimeError(f'sender_boards has to be divisible by sender_cores ({dpdklibs.sender_boards} is not divisible by {dpdklibs.sender_cores}')

    enable_sender, enable_receiver = True, True
    if dpdklibs.only_sender:
        enable_receiver = False
    if dpdklibs.only_reader:
        enable_sender = False

    console.log('Loading dpdklibs config generator')
    from dpdklibs import sender_confgen
    from dpdklibs import reader_confgen

    the_system = System()
   
    # add app
    if enable_sender:
        the_system.apps["dpdk-sender"] = sender_confgen.generate_dpdk_sender_app(
            HOST=dpdklibs.host_sender,
            NUMBER_OF_CORES=dpdklibs.sender_cores,
            NUMBER_OF_IPS_PER_CORE=dpdklibs.sender_boards // dpdklibs.sender_cores,
            TIME_TICK_DIFFERENCE=dpdklibs.sender_time_tick_difference,
        )
    if enable_receiver:
        the_system.apps["dpdk-reader"] = reader_confgen.generate_dpdk_reader_app(
            HOST=dpdklibs.host_reader,
            EAL_ARGS=dpdklibs.eal_args,
        )

    # Arrange per-app command data into the format used by util.write_json_files()
    app_command_datas = {
        name : make_app_command_data(the_system, app, name)
        for name,app in the_system.apps.items()
    }

    # Make boot.json config
    from daqconf.core.conf_utils import make_system_command_datas, write_json_files

    system_command_datas = make_system_command_datas(
        boot,
        the_system,
        verbose=False
    )

    write_json_files(app_command_datas, system_command_datas, json_dir, verbose=True)

    console.log(f"dpdklibs app config generated in {json_dir}")
    
    write_metadata_file(json_dir, "dpdklibs_confgen", config_file)

if __name__ == '__main__':
    try:
        cli(show_default=True, standalone_mode=True)
    except Exception as e:
        console.print_exception()
