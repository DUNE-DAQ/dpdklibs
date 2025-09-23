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

#include <memory>
#include <string>

namespace dunedaq {

DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter, "WIBEthFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::TDEEthTypeAdapter, "TDEEthFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::DAPHNEEthTypeAdapter, "DAPHNEEthFrame")

namespace dpdklibs {

std::shared_ptr<SourceConcept>
createSourceModel(const std::string& conn_uid, bool callback_mode)
{

  auto datatypes = dunedaq::iomanager::IOManager::get()->get_datatypes(conn_uid);
  if (datatypes.size() != 1) {
    ers::error(dunedaq::datahandlinglibs::GenericConfigurationError(ERS_HERE,
      "Multiple output data types specified! Expected only a single type!"));
  }
  std::string raw_dt{ *datatypes.begin() };
  TLOG() << "Choosing specializations for SourceModel for output connection "
         << " [uid:" << conn_uid << " , data_type:" << raw_dt << ']';

  if (raw_dt.find("WIBEthFrame") != std::string::npos) {
    // Create Model
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::DUNEWIBEthTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_name(conn_uid);

    // Setup sink (acquire pointer from QueueRegistry)
    source_model->set_sink(conn_uid, callback_mode);


    // Return with setup model
    return source_model;

  } else if (raw_dt.find("TDEEthFrame") != std::string::npos) {
    // WIB2 specific char arrays
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::TDEEthTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_name(conn_uid);
  
    // Setup sink (acquire pointer from QueueRegistry)
    source_model->set_sink(conn_uid, callback_mode);
    
    return source_model;

  } else if (raw_dt.find("DAPHNEEthFrame") != std::string::npos) {
    // WIB2 specific char arrays
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::DAPHNEEthTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_name(conn_uid);
  
    // Setup sink (acquire pointer from QueueRegistry)
    source_model->set_sink(conn_uid, callback_mode);
    
    return source_model;
  }
  
    
  return nullptr;
}

} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_CREATESOURCE_HPP_
