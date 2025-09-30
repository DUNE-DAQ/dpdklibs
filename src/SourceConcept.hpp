/**
 * @file SourceConcept.hpp SourceConcept for constructors and
 * forwarding command args. Enforces the implementation to
 * queue in UDP JUMBO frames to be translated to TypeAdapters and
 * send them to corresponding sinks.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_SOURCECONCEPT_HPP_
#define DPDKLIBS_SRC_SOURCECONCEPT_HPP_

//#include "DefaultParserImpl.hpp"

#include "opmonlib/MonitorableObject.hpp"
#include "appfwk/DAQModule.hpp"
//#include "packetformat/detail/block_parser.hpp"
#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>

namespace dunedaq {
  namespace dpdklibs {

    class SourceConcept : public opmonlib::MonitorableObject
    {
    public:
      SourceConcept() {}
      virtual ~SourceConcept() {}

      SourceConcept(const SourceConcept&) = delete;            ///< SourceConcept is not copy-constructible
      SourceConcept& operator=(const SourceConcept&) = delete; ///< SourceConcept is not copy-assginable
      SourceConcept(SourceConcept&&) = delete;                 ///< SourceConcept is not move-constructible
      SourceConcept& operator=(SourceConcept&&) = delete;      ///< SourceConcept is not move-assignable

      //  virtual void init(const nlohmann::json& args) = 0;
      virtual void set_sink(const std::string& sink_name, bool callback_mode) = 0;
      virtual void acquire_callback() = 0;
      //  virtual void conf(const nlohmann::json& args) = 0;
      //  virtual void start(const nlohmann::json& args) = 0;
      //  virtual void stop(const nlohmann::json& args) = 0;

      // Meant to process an incoming raw byte buffer and extract complete frames of arbitrary types in specialized models.
      virtual void handle_daq_frame(char* buffer, std::size_t size) = 0;

      void set_sink_name(const std::string& sink_name) 
      { 
        m_sink_name = sink_name; 
      }

      // Disables DAQEth protocol on this source 
      void disable_daq_protocol_checks() {
        m_daq_protocol_ensured = false;   
      }

      // Sink or destination related
      std::string m_sink_name;

      // Features
      bool m_daq_protocol_ensured { true };

    };

  } // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_SOURCECONCEPT_HPP_
