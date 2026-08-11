/*
 * This file is 100% generated.  Any manual edits will likely be lost.
 *
 * This contains functions struct and other type definitions for shema in 
 * namespace dunedaq::dpdklibs::nicsender to be serialized via nlohmann::json.
 */
#ifndef DUNEDAQ_DPDKLIBS_NICSENDER_NLJS_HPP
#define DUNEDAQ_DPDKLIBS_NICSENDER_NLJS_HPP

// My structs
#include "dpdklibs/nicsender/Structs.hpp"


#include <nlohmann/json.hpp>

namespace dunedaq::dpdklibs::nicsender {

    using data_t = nlohmann::json;
    
    inline void to_json(data_t& j, const Core& obj) {
        j["lcore_id"] = obj.lcore_id;
        j["src_ips"] = obj.src_ips;
    }
    
    inline void from_json(const data_t& j, Core& obj) {
        if (j.contains("lcore_id"))
            j.at("lcore_id").get_to(obj.lcore_id);    
        if (j.contains("src_ips"))
            j.at("src_ips").get_to(obj.src_ips);    
    }
    
    inline void to_json(data_t& j, const Conf& obj) {
        j["card_id"] = obj.card_id;
        j["eal_arg_list"] = obj.eal_arg_list;
        j["frontend_type"] = obj.frontend_type;
        j["number_of_cores"] = obj.number_of_cores;
        j["number_of_ips_per_core"] = obj.number_of_ips_per_core;
        j["burst_size"] = obj.burst_size;
        j["rate"] = obj.rate;
        j["core_list"] = obj.core_list;
        j["time_tick_difference"] = obj.time_tick_difference;
    }
    
    inline void from_json(const data_t& j, Conf& obj) {
        if (j.contains("card_id"))
            j.at("card_id").get_to(obj.card_id);    
        if (j.contains("eal_arg_list"))
            j.at("eal_arg_list").get_to(obj.eal_arg_list);    
        if (j.contains("frontend_type"))
            j.at("frontend_type").get_to(obj.frontend_type);    
        if (j.contains("number_of_cores"))
            j.at("number_of_cores").get_to(obj.number_of_cores);    
        if (j.contains("number_of_ips_per_core"))
            j.at("number_of_ips_per_core").get_to(obj.number_of_ips_per_core);    
        if (j.contains("burst_size"))
            j.at("burst_size").get_to(obj.burst_size);    
        if (j.contains("rate"))
            j.at("rate").get_to(obj.rate);    
        if (j.contains("core_list"))
            j.at("core_list").get_to(obj.core_list);    
        if (j.contains("time_tick_difference"))
            j.at("time_tick_difference").get_to(obj.time_tick_difference);    
    }
    
} // namespace dunedaq::dpdklibs::nicsender

#endif // DUNEDAQ_DPDKLIBS_NICSENDER_NLJS_HPP