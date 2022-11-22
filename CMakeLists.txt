cmake_minimum_required(VERSION 3.12)
project(dpdklibs VERSION 0.0.2)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(appfwk REQUIRED)
find_package(logging REQUIRED)
find_package(daqdataformats REQUIRED)
find_package(detdataformats REQUIRED)
find_package(readoutlibs REQUIRED)
find_package(fdreadoutlibs REQUIRED)
#find_package(dpdk REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

daq_codegen( nicsender.jsonnet nicreader.jsonnet TEMPLATES Structs.hpp.j2 Nljs.hpp.j2 )
daq_codegen( *info.jsonnet DEP_PKGS opmonlib TEMPLATES opmonlib/InfoStructs.hpp.j2 opmonlib/InfoNljs.hpp.j2 )
daq_codegen( confgen.jsonnet DEP_PKGS daqconf TEMPLATES Structs.hpp.j2)

##############################################################################
# Dependency sets
set(DUNEDAQ_DEPENDENCIES readoutlibs::readoutlibs daqdataformats::daqdataformats detdataformats::detdataformats fdreadoutlibs::fdreadoutlibs)

# Provide override functionality for DPDK dependencies
option(WITH_DPDK_AS_PACKAGE "DPDK externals as a dunedaq package" OFF)

if(WITH_DPDK_AS_PACKAGE)
  set(DPDK_LIBRARIES
    # dpdk::dpdk
  )
else()
  # From env
  set(DPDK_LIBRARIES
    $ENV{DPDK_LIB}/librte_eal.so
    $ENV{DPDK_LIB}/librte_ethdev.so
    $ENV{DPDK_LIB}/librte_mbuf.so
    $ENV{DPDK_LIB}/librte_mempool.so
    $ENV{DPDK_LIB}/librte_jobstats.so
    $ENV{DPDK_LIB}/librte_timer.so
    logging::logging
  )
endif()

##############################################################################
# Main library
daq_add_library(*.cpp udp/*.cpp arp/*.cpp LINK_LIBRARIES appfwk::appfwk ${DPDK_LIBRARIES} ${DUNEDAQ_DEPENDENCIES})

if(WITH_DPDK_AS_PACKAGE)
  # All good
else()
  # Hack target
  target_include_directories(dpdklibs PUBLIC $ENV{DPDK_INC})
endif()

##############################################################################
# Plugins
daq_add_plugin(NICSender duneDAQModule LINK_LIBRARIES appfwk::appfwk dpdklibs)
daq_add_plugin(NICReceiver duneDAQModule LINK_LIBRARIES appfwk::appfwk dpdklibs)

##############################################################################
# Integration tests
daq_add_application(dpdklibs_test_eal test_eal_app.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_LIBRARIES})
daq_add_application(dpdklibs_test_frame_transmitter test_frame_transmitter.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_LIBRARIES})
daq_add_application(dpdklibs_test_frame_receiver test_frame_receiver.cxx TEST LINK_LIBRARIES dpdklibs ${DPDK_LIBRARIES})

target_compile_options(dpdklibs PUBLIC ${DPDK_CFLAGS})
target_include_directories(dpdklibs PUBLIC ${DPDK_INCLUDE_DIRS})

##############################################################################
# Installation
daq_install()
