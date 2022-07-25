/**
 * @file IPV4Address.hpp IPV4Address string to byte converter struct
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_UDP_IPV4ADDRESS_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_UDP_IPV4ADDRESS_HPP_

#include <string>
#include <vector>
#include <sstream>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

struct IpAddr {
  IpAddr(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) 
    : addr_bytes{byte0, byte1, byte2, byte3} 
  { }

  IpAddr(std::string ip_address) 
  {
    std::vector<uint8_t> bytes;
    std::istringstream f(ip_address);
    std::string s;    
    while (getline(f, s, '.')) {
      bytes.push_back(std::stoi(s));
    }
    IpAddr(bytes[0], bytes[1], bytes[2], bytes[3]);
  }

protected:
  uint8_t addr_bytes[4];
};

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif  // DPDKLIBS_INCLUDE_DPDKLIBS_UDP_IPV4ADDRESS_HPP_
