/**
 * @file CreateSource.hpp Specific SourceConcept creator.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_CREATESOURCE_HPP_
#define DPDKLIBS_SRC_CREATESOURCE_HPP_

#include "SourceConcept.hpp"
#include "SourceModel.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"

#include "fdreadoutlibs/DUNEWIBEthTypeAdapter.hpp"
#include "fdreadoutlibs/TDEEthTypeAdapter.hpp"
#include "fdreadoutlibs/DAPHNEEthTypeAdapter.hpp"
#include "fdreadoutlibs/DAPHNEEthStreamTypeAdapter.hpp"

#include <memory>
#include <string>

namespace dunedaq {

DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter, "WIBEthFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::TDEEthTypeAdapter, "TDEEthFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::DAPHNEEthTypeAdapter, "DAPHNEEthFrame")

namespace dpdklibs {

std::shared_ptr<SourceConcept>
createSourceModel(const appmodel::RawDataCallbackConf* conf)
{

  auto datatype = conf->get_data_type();
  TLOG() << "Choosing specializations for SourceModel for output connection "
         << " [uid:" << conf->UID() << " , data_type:" << datatype << ']';

  if (datatype.find("WIBEthFrame") != std::string::npos) {
    // Create Model
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::DUNEWIBEthTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_config(conf);

    // Return with setup model
    return source_model;

  } else if (datatype.find("TDEEthFrame") != std::string::npos) {

    // Create Model
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::TDEEthTypeAdapter>>();

    // Disable DAQ protocol checks
    source_model->disable_daq_protocol_checks();

    // For callback acquisition later (lazy)
    source_model->set_sink_config(conf);
    
    return source_model;

  } else if (datatype.find("DAPHNEEthFrame") != std::string::npos) {
    // WIB2 specific char arrays
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::DAPHNEEthTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_config(conf);
    
    return source_model;
  } else if (datatype.find("DAPHNEEthStreamFrame") != std::string::npos) {
    // WIB2 specific char arrays
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::DAPHNEEthStreamTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_config(conf);
    
    return source_model;
  }  
    
  return nullptr;
}

} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_CREATESOURCE_HPP_
