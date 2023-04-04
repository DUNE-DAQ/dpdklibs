#ifndef _IPV4_ADDR_H_
#define _IPV4_ADDR_H_

#include <string>
#include <vector>
#include <sstream>

namespace dunedaq::dpdklibs {

  struct IpAddr {
    IpAddr(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) : 
      addr_bytes{byte0, byte1, byte2, byte3} {
    }
    IpAddr(std::string ip_address) {
      std::vector<uint8_t> bytes;
      std::istringstream f(ip_address);
      std::string s;    
      while (getline(f, s, '.')) {
	//std::cout << s << std::endl;
	bytes.push_back(std::stoi(s));
      }
      
      for (int i = 0; i < bytes.size(); ++i) {
	addr_bytes[i] = bytes[i];
      }
    }
    //uint32_t get(){} 
  protected:
    uint8_t addr_bytes[4];
  };

  // Use IpAddr2 to un-protect the byte info in IpAddr
  struct IpAddr2 : public IpAddr {

    IpAddr2(std::string ip) : IpAddr(ip)
    {}
    
    uint8_t get_byte(int i) {
      return addr_bytes[i];
    }
  };
}

#endif  /* _IPV4_ADDR_H_ */

