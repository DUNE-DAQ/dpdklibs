/*
 * This file is 100% generated.  Any manual edits will likely be lost.
 *
 * This contains struct and other type definitions for shema in 
 * namespace dunedaq::dpdklibs::nicsender.
 */
#ifndef DUNEDAQ_DPDKLIBS_NICSENDER_STRUCTS_HPP
#define DUNEDAQ_DPDKLIBS_NICSENDER_STRUCTS_HPP

#include <cstdint>

#include <vector>
#include <string>

namespace dunedaq::dpdklibs::nicsender {

    // @brief A count of more things
    using BigCount = int64_t;


    // @brief 
    using Choice = bool;

    // @brief An ID of a thingy
    using Identifier = int32_t;


    // @brief A string field
    using String = std::string;

    // @brief Count of things
    using Count = uint32_t; // NOLINT


    // @brief A float number
    using Float = float;


    // @brief A list of ips
    using Ips = std::vector<dunedaq::dpdklibs::nicsender::String>;

    // @brief 
    struct Core 
    {

        // @brief ID of lcore
        Identifier lcore_id = 0;

        // @brief Source IP that will be in the headers sent by this lcore
        Ips src_ips = {};
    };

    // @brief A list of cores
    using CoreList = std::vector<dunedaq::dpdklibs::nicsender::Core>;

    // @brief Generic UIO sender DAQ Module Configuration
    struct Conf 
    {

        // @brief Physical card identifier (in the same host)
        Identifier card_id = 0;

        // @brief A string with EAL arguments
        String eal_arg_list = "";

        // @brief The frontend type (wib, wib2, daphne, tde)
        String frontend_type = "";

        // @brief Number of cores that will be used for sending
        Count number_of_cores = 1;

        // @brief Number of cores that will be used for sending
        Count number_of_ips_per_core = 1;

        // @brief Burst size used when sending
        BigCount burst_size = 1;

        // @brief Rate used for the sender
        Float rate = 1.0;

        // @brief Core lists used for sending
        CoreList core_list = {};

        // @brief How many ticks between timestamps
        BigCount time_tick_difference = 0;
    };

} // namespace dunedaq::dpdklibs::nicsender

#endif // DUNEDAQ_DPDKLIBS_NICSENDER_STRUCTS_HPP