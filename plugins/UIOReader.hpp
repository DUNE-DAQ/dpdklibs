/**
 * @file UIOReader.hpp Generic UIO reader DAQ Module.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef FLXLIBS_PLUGINS_UIOREADER_HPP_
#define FLXLIBS_PLUGINS_UIOREADER_HPP_

#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "appfwk/cmd/Structs.hpp"

#include "dpdklibs/uioreader/Structs.hpp"

// From appfwk
#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSink.hpp"

#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq::dpdklibs {

class UIOReader : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief UIOReader Constructor
   * @param name Instance name for this UIOReader instance
   */
  explicit UIOReader(const std::string& name);

  UIOReader(const UIOReader&) = delete;            ///< UIOReader is not copy-constructible
  UIOReader& operator=(const UIOReader&) = delete; ///< UIOReader is not copy-assignable
  UIOReader(UIOReader&&) = delete;                 ///< UIOReader is not move-constructible
  UIOReader& operator=(UIOReader&&) = delete;      ///< UIOReader is not move-assignable

  void init(const data_t& args) override;

private:
  // Types
  using module_conf_t = dunedaq::dpdklibs::uioreader::Conf;

  // Commands
  void do_configure(const data_t& args);
  void do_start(const data_t& args);
  void do_stop(const data_t& args);
  void get_info(opmonlib::InfoCollector& ci, int level);

};

} // namespace dunedaq::dpdklibs

#endif // FLXLIBS_PLUGINS_UIOREADER_HPP_
