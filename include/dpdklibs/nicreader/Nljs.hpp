/*
 * This file is 100% generated.  Any manual edits will likely be lost.
 *
 * This contains functions struct and other type definitions for shema in 
 * namespace dunedaq::dpdklibs::nicreader to be serialized via nlohmann::json.
 */
#ifndef DUNEDAQ_DPDKLIBS_NICREADER_NLJS_HPP
#define DUNEDAQ_DPDKLIBS_NICREADER_NLJS_HPP

// My structs
#include "dpdklibs/nicreader/Structs.hpp"


#include <nlohmann/json.hpp>

namespace dunedaq::dpdklibs::nicreader {

    using data_t = nlohmann::json;
    
    inline void to_json(data_t& j, const SrcGeoInfo& obj) {
        j["det_id"] = obj.det_id;
        j["crate_id"] = obj.crate_id;
        j["slot_id"] = obj.slot_id;
    }
    
    inline void from_json(const data_t& j, SrcGeoInfo& obj) {
        if (j.contains("det_id"))
            j.at("det_id").get_to(obj.det_id);    
        if (j.contains("crate_id"))
            j.at("crate_id").get_to(obj.crate_id);    
        if (j.contains("slot_id"))
            j.at("slot_id").get_to(obj.slot_id);    
    }
    
    inline void to_json(data_t& j, const StreamMap& obj) {
        j["source_id"] = obj.source_id;
        j["stream_id"] = obj.stream_id;
    }
    
    inline void from_json(const data_t& j, StreamMap& obj) {
        if (j.contains("source_id"))
            j.at("source_id").get_to(obj.source_id);    
        if (j.contains("stream_id"))
            j.at("stream_id").get_to(obj.stream_id);    
    }
    
    inline void to_json(data_t& j, const Source& obj) {
        j["id"] = obj.id;
        j["ip_addr"] = obj.ip_addr;
        j["lcore"] = obj.lcore;
        j["rx_q"] = obj.rx_q;
        j["src_info"] = obj.src_info;
        j["src_streams_mapping"] = obj.src_streams_mapping;
    }
    
    inline void from_json(const data_t& j, Source& obj) {
        if (j.contains("id"))
            j.at("id").get_to(obj.id);    
        if (j.contains("ip_addr"))
            j.at("ip_addr").get_to(obj.ip_addr);    
        if (j.contains("lcore"))
            j.at("lcore").get_to(obj.lcore);    
        if (j.contains("rx_q"))
            j.at("rx_q").get_to(obj.rx_q);    
        if (j.contains("src_info"))
            j.at("src_info").get_to(obj.src_info);    
        if (j.contains("src_streams_mapping"))
            j.at("src_streams_mapping").get_to(obj.src_streams_mapping);    
    }
    
    inline void to_json(data_t& j, const StatsReporting& obj) {
        j["expected_seq_id_step"] = obj.expected_seq_id_step;
        j["expected_timestamp_step"] = obj.expected_timestamp_step;
        j["expected_packet_size"] = obj.expected_packet_size;
        j["analyze_nth_packet"] = obj.analyze_nth_packet;
    }
    
    inline void from_json(const data_t& j, StatsReporting& obj) {
        if (j.contains("expected_seq_id_step"))
            j.at("expected_seq_id_step").get_to(obj.expected_seq_id_step);    
        if (j.contains("expected_timestamp_step"))
            j.at("expected_timestamp_step").get_to(obj.expected_timestamp_step);    
        if (j.contains("expected_packet_size"))
            j.at("expected_packet_size").get_to(obj.expected_packet_size);    
        if (j.contains("analyze_nth_packet"))
            j.at("analyze_nth_packet").get_to(obj.analyze_nth_packet);    
    }
    
    inline void to_json(data_t& j, const Interface& obj) {
        j["pci_addr"] = obj.pci_addr;
        j["mac_addr"] = obj.mac_addr;
        j["ip_addr"] = obj.ip_addr;
        j["with_flow_control"] = obj.with_flow_control;
        j["promiscuous_mode"] = obj.promiscuous_mode;
        j["mtu"] = obj.mtu;
        j["rx_ring_size"] = obj.rx_ring_size;
        j["tx_ring_size"] = obj.tx_ring_size;
        j["num_mbufs"] = obj.num_mbufs;
        j["mbuf_cache_size"] = obj.mbuf_cache_size;
        j["burst_size"] = obj.burst_size;
        j["lcore_sleep_us"] = obj.lcore_sleep_us;
        j["expected_sources"] = obj.expected_sources;
        j["stats_reporting_cfg"] = obj.stats_reporting_cfg;
    }
    
    inline void from_json(const data_t& j, Interface& obj) {
        if (j.contains("pci_addr"))
            j.at("pci_addr").get_to(obj.pci_addr);    
        if (j.contains("mac_addr"))
            j.at("mac_addr").get_to(obj.mac_addr);    
        if (j.contains("ip_addr"))
            j.at("ip_addr").get_to(obj.ip_addr);    
        if (j.contains("with_flow_control"))
            j.at("with_flow_control").get_to(obj.with_flow_control);    
        if (j.contains("promiscuous_mode"))
            j.at("promiscuous_mode").get_to(obj.promiscuous_mode);    
        if (j.contains("mtu"))
            j.at("mtu").get_to(obj.mtu);    
        if (j.contains("rx_ring_size"))
            j.at("rx_ring_size").get_to(obj.rx_ring_size);    
        if (j.contains("tx_ring_size"))
            j.at("tx_ring_size").get_to(obj.tx_ring_size);    
        if (j.contains("num_mbufs"))
            j.at("num_mbufs").get_to(obj.num_mbufs);    
        if (j.contains("mbuf_cache_size"))
            j.at("mbuf_cache_size").get_to(obj.mbuf_cache_size);    
        if (j.contains("burst_size"))
            j.at("burst_size").get_to(obj.burst_size);    
        if (j.contains("lcore_sleep_us"))
            j.at("lcore_sleep_us").get_to(obj.lcore_sleep_us);    
        if (j.contains("expected_sources"))
            j.at("expected_sources").get_to(obj.expected_sources);    
        if (j.contains("stats_reporting_cfg"))
            j.at("stats_reporting_cfg").get_to(obj.stats_reporting_cfg);    
    }
    
    inline void to_json(data_t& j, const Conf& obj) {
        j["ifaces"] = obj.ifaces;
        j["eal_arg_list"] = obj.eal_arg_list;
    }
    
    inline void from_json(const data_t& j, Conf& obj) {
        if (j.contains("ifaces"))
            j.at("ifaces").get_to(obj.ifaces);    
        if (j.contains("eal_arg_list"))
            j.at("eal_arg_list").get_to(obj.eal_arg_list);    
    }
    
} // namespace dunedaq::dpdklibs::nicreader

#endif // DUNEDAQ_DPDKLIBS_NICREADER_NLJS_HPP