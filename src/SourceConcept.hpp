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

#include "appfwk/DAQModule.hpp"
//#include "packetformat/detail/block_parser.hpp"
#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>

namespace dunedaq {
namespace dpdklibs {

class SourceConcept
{
public:
  SourceConcept()
    //: m_parser_impl()
    : m_card_id(0)
    , m_port_id(0)
    , m_source_ip(0)
    , m_link_tag(0)
    , m_source_str("")
    , m_source_tid("")
  {
    //m_parser = std::make_unique<felix::packetformat::BlockParser<DefaultParserImpl>>(m_parser_impl);
  }
  virtual ~SourceConcept() {}

  SourceConcept(const SourceConcept&) = delete;            ///< SourceConcept is not copy-constructible
  SourceConcept& operator=(const SourceConcept&) = delete; ///< SourceConcept is not copy-assginable
  SourceConcept(SourceConcept&&) = delete;                 ///< SourceConcept is not move-constructible
  SourceConcept& operator=(SourceConcept&&) = delete;      ///< SourceConcept is not move-assignable

  virtual void init(const nlohmann::json& args) = 0;
  virtual void set_sink(const std::string& sink_name) = 0;
  virtual void conf(const nlohmann::json& args) = 0;
  virtual void start(const nlohmann::json& args) = 0;
  virtual void stop(const nlohmann::json& args) = 0;
  virtual void get_info(opmonlib::InfoCollector& ci, int level) = 0;

  virtual bool handle_payload(char* message, std::size_t size) = 0;
  //virtual bool queue_in_block_address(uint64_t block_addr) = 0; // NOLINT

  //DefaultParserImpl& get_parser() { return std::ref(m_parser_impl); }

  void set_ids(int card, int pid, int sip, int lid)
  {
    m_card_id = card;
    m_port_id = pid;
    m_source_ip = sip;
    m_link_tag = lid;

    std::ostringstream lidstrs;
    lidstrs << "Source["
            << "cid:" << std::to_string(m_card_id) << "|"
            << "pid:" << std::to_string(m_port_id) << "|"
            << "sip:" << std::to_string(m_source_ip) << "]"
            << "tag:" << std::to_string(m_link_tag) << "]";
    m_source_str = lidstrs.str();

    std::ostringstream tidstrs;
    tidstrs << "spt-" << std::to_string(m_card_id) << "-" << std::to_string(m_port_id) << "-" << std::to_string(m_link_tag);
    m_source_tid = tidstrs.str();

    m_opmon_str =
      "slink_" + std::to_string(m_card_id) + "_" + std::to_string(m_port_id) + "_" + std::to_string(m_link_tag);
  }

protected:
  // Block Parser
  //DefaultParserImpl m_parser_impl;
  //std::unique_ptr<felix::packetformat::BlockParser<DefaultParserImpl>> m_parser;

  int m_card_id;
  int m_port_id;
  int m_source_ip;
  int m_link_tag;
  std::string m_source_str;
  std::string m_opmon_str;
  std::string m_source_tid;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_t0;

private:
};

} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_SOURCECONCEPT_HPP_
