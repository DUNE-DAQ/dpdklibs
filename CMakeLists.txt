cmake_minimum_required(VERSION 3.12)
project(dpdklibs VERSION 0.0.1)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(appfwk REQUIRED)
find_package(logging REQUIRED)
find_package(daqdataformats REQUIRED)
find_package(readoutlibs REQUIRED)
#find_package(dpdk REQUIRED)

daq_codegen(nicreader.jsonnet TEMPLATES Structs.hpp.j2 Nljs.hpp.j2 )
daq_codegen( *info.jsonnet DEP_PKGS opmonlib TEMPLATES opmonlib/InfoStructs.hpp.j2 opmonlib/InfoNljs.hpp.j2 )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse3")

##############################################################################
# Dependency sets
set(DUNEDAQ_DEPENDENCIES readoutlibs::readoutlibs daqdataformats::daqdataformats)

# Provide override functionality for DPDK dependencies
option(WITH_DPDK_AS_PACKAGE "DPDK externals as a dunedaq package" OFF)

if(WITH_DPDK_AS_PACKAGE)
  set(DPDK_DEPENDENCIES 
    # dpdk::dpdk
  )
else()
  # From env
  set(DPDK_DEPENDENCIES 
    $ENV{DPDK_LIB}/librte_eal.so
          $ENV{DPDK_LIB}/librte_ethdev.so
          $ENV{DPDK_LIB}/librte_mbuf.so
          $ENV{DPDK_LIB}/librte_mempool.so
          $ENV{DPDK_LIB}/librte_jobstats.so
          $ENV{DPDK_LIB}/librte_timer.so
  )
endif()

##############################################################################
# Main library
daq_add_library(*.cpp LINK_LIBRARIES appfwk::appfwk ${DPDK_DEPENDENCIES} ${DUNEDAQ_DEPENDENCIES})

if(WITH_DPDK_AS_PACKAGE)
  # All good
else()
  # Hack target
  target_include_directories(dpdklibs PUBLIC $ENV{DPDK_INC})
endif()

##############################################################################
# Plugins
daq_add_plugin(NICReader duneDAQModule LINK_LIBRARIES appfwk::appfwk dpdklibs)

##############################################################################
# Integration tests
daq_add_application(dpdklibs_test_eal test_eal_app.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_DEPENDENCIES})
daq_add_application(dpdklibs_test_receiving_payloads test_receiving_payloads.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_DEPENDENCIES})
daq_add_application(dpdklibs_test_forward_with_stats test_forward_with_stats.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_DEPENDENCIES})
daq_add_application(dpdklibs_test_basic_transmitter test_basic_transmitter.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_DEPENDENCIES})

##############################################################################
# Applications
#daq_add_application(flxlibs_emu_confgen emu_confgen.cxx LINK_LIBRARIES flxlibs)

##############################################################################
# Installation
daq_install()
