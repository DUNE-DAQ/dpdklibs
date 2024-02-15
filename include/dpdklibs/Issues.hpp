/**
 * @file DPDKIssues.hpp DPDK system related ERS issues
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_DPDKISSUES_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_DPDKISSUES_HPP_

#include <ers/Issue.hpp>

#include <string>

namespace dunedaq {
ERS_DECLARE_ISSUE(dpdklibs,
                  FailedToSetupInterface,
                  "Interface [" << ifaceid << "] setup failed: " << error,
                  ((int)ifaceid)((int)error)
                );

ERS_DECLARE_ISSUE(dpdklibs,
                  InvalidEALPort,
                  "Interface [" << ifaceid << "] is not a valid port: ",
                  ((int)ifaceid)
                );

ERS_DECLARE_ISSUE(dpdklibs,
                  FailedToRetrieveInterfaceInfo,
                  "Failed to retrieve device info for interfce [" << ifaceid << "]: " << error,
                  ((int)ifaceid)((int)error)
                );

ERS_DECLARE_ISSUE(dpdklibs,
                  FailedToResetInterface,
                  "Failed to reset interfce [" << ifaceid << "]: " << error,
                  ((int)ifaceid)((int)error)
                );

ERS_DECLARE_ISSUE(dpdklibs,
                  FailedToConfigureInterface,
                  "Failed to configure interface [" << ifaceid << "], stage " << stage << " : " << error,
                  ((int)ifaceid)((std::string)stage)((int)error)
                );

}

#endif /* DPDKLIBS_INCLUDE_DPDKLIBS_DPDKISSUES_HPP_ */
