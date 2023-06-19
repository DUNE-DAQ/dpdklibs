## Test Frame Receiver Guide

The test frame receiver is a test app made to receive packets and validate their format and rates.

# instructions

the test frame receiver needs to run inside a dunedaq workarea. Additionally, a location to store json configuration files will be needed.

To run the test frame receiver, a configuration file is needed. This is different from machine to machine, and should be generated again each time the number of links or the source ips of the data change.

To generate a configuration file run
```dpdklibs_generate_tfr_settings path/to/conf/dir/my_conf_name.json```
this will generate settings where each lcore has one queue (accepts data from 1 ip source).
if you want each lcore to have X queues, run
```dpdklibs_generate_tfr_settings path/to/conf/dir/my_conf_name.json -q X```
where X is the number of queues per lcore
You can also create a configuration file manually.
the script to generate the conf file always ends with a segmentation fault. this is to be expected. You will know if it run to completion and you got the expected error if the program says `APP SUCCESSFULLY COMPLETE. EXPECT ERROR`.


After creating a configuration file, run the test frame receiver using
`dpdklibs_test_frame_receiver path/to/conf/dir/my_conf_name.json`
you can add several extra arguments, like
- `-p` gives detailed per stream reports. 
- `-i X` initiates the Xth iface instead of the 0th one (default). the iface to be initiated should exist in the conf file
- `-t X` reports back every X seconds instead of every second.
- `-m X` makes the master lcore different than the default one (0). NOTE: Be careful not to have your master lcore also appear in the settings, the app does NOT check, and it will fail
- `--check-time` checks the timestamp of the packets as well as the id.
- `-s X` checks the payload size against an expected value X

# the configuration file
the configuration file is structured as follows:
```
{
	iface:{
		lcore:{
			queue: src_ip,
			queue: src_ip,
			...
		}
		lcore:{...},
		...
	},
	iface:{...},
	...
}
```
So, an example configuration file would be:
```
{
    "0": {
        "1": {
            "0": "10.73.139.38",
            "1": "10.73.139.39"
        },
        "2": {
            "2": "10.73.139.40",
            "3": "10.73.139.41"
        },
        "3": {
            "4": "10.73.139.42",
            "5": "10.73.139.43"
        }
    }
}
```
note that: 
- all numbers are in `""`
- the queue numbers, the lcore numbers, and the ip addresses do NOT repeat
- the 0th lcore isn't there, since it's going to be used as the main lcore
