// dvargas 2022

#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <atomic>
#include <chrono>
#include <random>
#include <vector>

#include "readoutlibs/utils/RateLimiter.hpp"

const int rate_12bits = 3054;  // in Hz = 3.054 in ms
const int rate_16bits = 2290; // in Hz = 2.290 in ms
const uint64_t toptime = 0x222211111111;

using namespace dunedaq::readoutlibs;


int main(int argc, char *argv[]){
  uint64_t timestamp = 0;

  // using the RateLimiter in Hz
  auto limiter = RateLimiter(rate_16bits); 

  // initializing the RateLimiter
  limiter.init();
  for (uint64_t i = 0; i < toptime; i++){
    while(i < toptime){
      timestamp = timestamp + 1;
      limiter.limit();
    }
  }
  return 0;
  //return (uint64_t)timestamp;
}
