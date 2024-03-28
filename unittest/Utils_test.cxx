/**
 * @file Utils_test.cxx 
 *
 * Test various utility functions in this package
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "dpdklibs/udp/Utils.hpp"

#define BOOST_TEST_MODULE Utils_test // NOLINT

#include "TRACE/trace.h"
#include "boost/test/unit_test.hpp"

#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>

using namespace dunedaq::dpdklibs;

BOOST_AUTO_TEST_SUITE(Utils_test)

BOOST_AUTO_TEST_CASE(GetEthernetPackets)
{

  const std::string tmp_filename = "/tmp/deleteme.txt";

  // Construct a fake ethernet packet where the only contents which
  // matter are the ether_type (sanity-checked in
  // udp::get_ethernet_packets) and ipv4_packet_length (used by
  // udp::get_ethernet_packets to determine the next packet). For the
  // IPv4 packet length, while two bytes are allocated in the IPv4
  // header to describe the size of the IPv4 packet, for the sake of
  // simplicity we're only using the lower byte.
  
  const uint8_t ipv4_packet_length = 28; // For the purposes of this test, do not make the length longer than 255!
  const uint8_t ipv4_packet_length_position = 17; // Comes after a 14-byte ethernet header and three bytes of IPv4 header
  const uint8_t ether_type = 0x08;

  std::vector<uint8_t> fake_ethernet_packet = {
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					ether_type, 0x00, 
					0x00, 0x00,
					0x00, ipv4_packet_length,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  BOOST_REQUIRE_EQUAL(fake_ethernet_packet.size(), ipv4_packet_length + sizeof(rte_ether_hdr)); // This is really a test-of-the-test
  
  std::ofstream tmpfile(tmp_filename);
  BOOST_REQUIRE(tmpfile.is_open());
  
  for (auto& byte : fake_ethernet_packet) {
    tmpfile << byte;
  }
  tmpfile.close();

  std::vector<char> buffervec;
  udp::add_file_contents_to_vector(tmp_filename, buffervec);
  BOOST_REQUIRE_EQUAL(fake_ethernet_packet.size(), buffervec.size());
  
  std::vector<std::pair<const void*, int>> packets = udp::get_ethernet_packets(buffervec);

  BOOST_REQUIRE_EQUAL(packets.size(), 1);
  BOOST_REQUIRE_EQUAL(packets[0].second, fake_ethernet_packet.size());

  std::vector<uint8_t> fake_ethernet_packet2 = fake_ethernet_packet;
  const uint8_t payload_size = 100; // Notice I'm careful not to make the IPv4 packet exceed 255 bytes here
  fake_ethernet_packet2.resize(fake_ethernet_packet2.size() + payload_size, 0xA);
  fake_ethernet_packet2.at(ipv4_packet_length_position) = fake_ethernet_packet2.at(ipv4_packet_length_position) + payload_size;
  BOOST_REQUIRE_EQUAL(ipv4_packet_length + payload_size, fake_ethernet_packet2.at(ipv4_packet_length_position));

  tmpfile.open(tmp_filename, std::ofstream::app);
  BOOST_REQUIRE(tmpfile.is_open());

  for (auto& byte : fake_ethernet_packet2) {
    tmpfile << byte;
  }
  tmpfile.close();

  buffervec.resize(0);
  udp::add_file_contents_to_vector(tmp_filename, buffervec);
  BOOST_REQUIRE_EQUAL(buffervec.size(), fake_ethernet_packet.size() + fake_ethernet_packet2.size());
  
  packets = udp::get_ethernet_packets(buffervec);

  BOOST_REQUIRE_EQUAL(packets.size(), 2);
  BOOST_REQUIRE_EQUAL(packets[0].second, fake_ethernet_packet.size());
  BOOST_REQUIRE_EQUAL(packets[1].second, fake_ethernet_packet2.size());

  BOOST_REQUIRE_EQUAL(buffervec.at( buffervec.size() - 1 ), 0xA);

  // Now some checks that get_ethernet_packets can catch when something goes wrong

  auto orig_ipv4_packet_size = buffervec.at(ipv4_packet_length_position);
  buffervec.at(ipv4_packet_length_position) = 0; // The first packet will now state a length of zero bytes, shorter than is allowed in the function (and in basic logic)
  
  BOOST_CHECK_THROW(udp::get_ethernet_packets(buffervec), dunedaq::dpdklibs::BadPacketHeaderIssue);

  // Restore the valid size of the packet, but now give the ethernet header an invalid ethertype
  buffervec.at(ipv4_packet_length_position) = orig_ipv4_packet_size;
  buffervec.at(6 + 6) = 0xAA; // 6 + 6 to account for the two 6-byte MAC addresses past which the ether type byte lives.
  BOOST_CHECK_THROW(udp::get_ethernet_packets(buffervec), dunedaq::dpdklibs::BadPacketHeaderIssue);

  buffervec.at(6 + 6) = ether_type;
  BOOST_CHECK_NO_THROW(udp::get_ethernet_packets(buffervec)); // Just a quick check that we got things to return to normal

}


BOOST_AUTO_TEST_SUITE_END()
