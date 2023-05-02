/**
 * @file Conversions_test.cxx 
 *
 * Test the converter functions written for strings vs. Big Endians vs. Little Endians, etc...
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "dpdklibs/ipv4_addr.hpp"
#include "dpdklibs/udp/IPV4UDPPacket.hpp"

#define BOOST_TEST_MODULE Conversions_test // NOLINT

#include "TRACE/trace.h"
#include "boost/test/unit_test.hpp"

#include <string>

using namespace dunedaq::dpdklibs;

BOOST_AUTO_TEST_SUITE(Conversions_test)

BOOST_AUTO_TEST_CASE(IpAddressConversions)
{
  std::string ipaddr_str = "10.20.30.40"; // non-const b/c of IpAddr constructor

  IpAddr ipaddr_object(ipaddr_str);

  BOOST_REQUIRE_EQUAL(ipaddr_object.addr_bytes[0], 10);
  BOOST_REQUIRE_EQUAL(ipaddr_object.addr_bytes[2], 30);
  
}

BOOST_AUTO_TEST_SUITE_END()
