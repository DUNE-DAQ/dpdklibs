/*
 * This file is 100% generated.  Any manual edits will likely be lost.
 *
 * This contains struct and other type definitions for shema in 
 * namespace dunedaq::dpdklibs::nicreader.
 */
#ifndef DUNEDAQ_DPDKLIBS_NICREADER_STRUCTS_HPP
#define DUNEDAQ_DPDKLIBS_NICREADER_STRUCTS_HPP

#include <cstdint>

#include <vector>
#include <string>

namespace dunedaq::dpdklibs::nicreader {

    // @brief A count of more things
    using BigCount = int64_t;


    // @brief 
    using Choice = bool;

    // @brief PCIe address string
    using pci = std::string;

    // @brief mac string
    using mac = std::string;

    // @brief ipv4 string
    using ipv4 = std::string;

    // @brief Count of things
    using Count = uint32_t; // NOLINT


    // @brief An ID of a thingy
    using Identifier = int32_t;


    // @brief Source GeoID Information
    struct SrcGeoInfo 
    {

        // @brief Detector ID
        Identifier det_id = 0;

        // @brief Crate ID
        Identifier crate_id = 0;

        // @brief Slot ID
        Identifier slot_id = 0;
    };

    // @brief A stream map
    struct StreamMap 
    {

        // @brief Source ID
        Identifier source_id = 0;

        // @brief Stream ID
        Identifier stream_id = 0;
    };

    // @brief A list of streams
    using SrcStreamsMapping = std::vector<dunedaq::dpdklibs::nicreader::StreamMap>;

    // @brief Source field
    struct Source 
    {

        // @brief ID of a source
        Identifier id = 0;

        // @brief Source IP address
        ipv4 ip_addr = "192.168.0.1";

        // @brief Assigned CPU lcore
        Identifier lcore = 0;

        // @brief Assigned RX queue of interface
        Identifier rx_q = 0;

        // @brief Source information
        SrcGeoInfo src_info = {0, 0, 0};

        // @brief Source streams mapping
        SrcStreamsMapping src_streams_mapping = {};
    };

    // @brief A list of sources
    using Sources = std::vector<dunedaq::dpdklibs::nicreader::Source>;

    // @brief Source field
    struct StatsReporting 
    {

        // @brief Expected sequence ID increase per packet in a stream
        BigCount expected_seq_id_step = 1;

        // @brief Expected timestamp increase per packet in a stream
        BigCount expected_timestamp_step = -999;

        // @brief Expected packet size
        BigCount expected_packet_size = 7243;

        // @brief Analyze only every (1/analyze_nth_packet) packet
        Count analyze_nth_packet = 1;
    };

    // @brief Configuration an Ethernet interface through DPDK RTE
    struct Interface 
    {

        // @brief PCIe address of the interface
        pci pci_addr = "0000:00:00.0";

        // @brief MAC address of the interface
        mac mac_addr = "AA:BB:CC:DD:EE:FF";

        // @brief IP address of interface
        ipv4 ip_addr = "192.168.0.1";

        // @brief FlowAPI enabled
        Choice with_flow_control = true;

        // @brief Promiscuous mode enabled
        Choice promiscuous_mode = false;

        // @brief MTU of interface
        Count mtu = 9000;

        // @brief Size of a single RX ring
        Count rx_ring_size = 1024;

        // @brief Size of a single TX ring
        Count tx_ring_size = 1024;

        // @brief Number of total MBUFs
        Count num_mbufs = 8191;

        // @brief MBUF cache size
        Count mbuf_cache_size = 256;

        // @brief RX burst size
        Count burst_size = 256;

        // @brief LCore loop sleep in microseconds - 0 to disable
        Count lcore_sleep_us = 10;

        // @brief A list of expected sources
        Sources expected_sources = {};

        // @brief Defines how stats are reported
        StatsReporting stats_reporting_cfg = {1, -999, 7243, 1};
    };

    // @brief A list of interfaces to use
    using IfaceList = std::vector<dunedaq::dpdklibs::nicreader::Interface>;

    // @brief A string field
    using String = std::string;

    // @brief Generic UIO reader DAQ Module Configuration
    struct Conf 
    {

        // @brief List of interfaces to configure
        IfaceList ifaces = {};

        // @brief A string with EAL arguments
        String eal_arg_list = "daq_application";
    };

    // @brief A float number
    using Float = float;


} // namespace dunedaq::dpdklibs::nicreader

#endif // DUNEDAQ_DPDKLIBS_NICREADER_STRUCTS_HPP