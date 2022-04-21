// dvargas 2022

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <atomic>
#include <chrono>
#include <random>
#include <vector>
#include <string>

#include "readoutlibs/utils/RateLimiter.hpp"
using namespace dunedaq::readoutlibs;
using namespace std;

static constexpr int tot_adc_16bits_samples = 4474;
static constexpr int tot_adc_12bits_samples = 5965;
static constexpr int tot_adc_sets = 746;
static constexpr int adc_set_samples = 8;
static constexpr int bits_per_adc = 12;
static constexpr int bits_per_word = 3 * sizeof(uint32_t);
static constexpr int num_adc_words = tot_adc_12bits_samples * bits_per_adc / bits_per_word;
static constexpr int rate_12bits = 328;  // in Hz = 3.054 in ms
static constexpr int rate_16bits = 437;  // in Hz = 2.290 in ms

struct TDEHeader
{
  uint32_t version : 6, det_id : 6, crate : 10, slot : 4, link : 6;
  uint64_t timestamp : 64;
  uint64_t TAItime : 64;
  uint32_t tde_header : 10, tde_errors : 22;
};

struct Sample 
{
  uint16_t sample : 12, reserved : 4;
};

struct Word
{
  uint32_t sample_0 : 12, sample_1 : 12, sample_2_0 : 8;
  uint32_t sample_2_1 : 4, sample_3 : 12, sample_4 : 12, sample_5_0 : 4;
  uint32_t sample_5_1 : 8, sample_6 : 12, sample_7 : 12;
};

struct ADCData16
{
  Sample samples_info16bits[tot_adc_16bits_samples];
};

struct ADCData12
{
  uint32_t samples_info12bits[num_adc_words];
};

int main()
{
  TDEHeader tdeheader;
  ADCData16 adcdata16;
  ADCData12 adcdata12;

  FILE *fp_16, *fp_12;

  bool killswitch { true };
  uint16_t new_adc_val = 0x9;
  uint64_t toptime = 0x23;
  uint64_t time = 0;

  // Seting the header
  tdeheader.version = 0x1;
  tdeheader.det_id = 0x2e;
  tdeheader.crate = 0x339;
  tdeheader.slot = 0x7;
  tdeheader.link = 0x23;
  tdeheader.TAItime = 0x345;
  tdeheader.tde_header = 0x61;
  tdeheader.tde_errors = 0xfc5;

  for(int j = 0; j < tot_adc_16bits_samples; j++)
  { 
    adcdata16.samples_info16bits[j].sample = new_adc_val; 
  }
  
  for(int k = 0; k < tot_adc_12bits_samples; k++)
  { 
    if (k < 0 || k >= tot_adc_12bits_samples)
      throw std::out_of_range("ADC sample index out of range");
    if (new_adc_val >= (1 << bits_per_adc))
      throw std::out_of_range("ADC bits value out of range");

    int word_index = bits_per_adc * k / bits_per_word;
    assert(word_index < num_adc_words);
    int first_bit_position = (bits_per_adc * k) % bits_per_word;
    int bits_in_first_word = std::min(bits_per_adc, bits_per_word - first_bit_position);
    adcdata12.samples_info12bits[word_index] |= (new_adc_val << first_bit_position);
    
    if (bits_in_first_word < bits_per_adc) 
    {
      assert(word_index + 1 < num_adc_words);
      adcdata12.samples_info12bits[word_index + 1] |= new_adc_val >> bits_in_first_word;
    }
  }

  // using the RateLimiter in Hz
  auto limiter = RateLimiter(rate_16bits); 
  limiter.init();

  cout << "Seting timestamp" << endl;
  while (killswitch) 
  {
    for(uint64_t i = 0; i < toptime; i++)
    {
      time += i;
      tdeheader.timestamp = time;
      limiter.limit();

      std::string str = std::to_string(i);
      std::string filename16 = "/eos/home-d/dvargas/Salidadune/filesTDE16bits/binary_16bits_" + str + ".bit";
      std::string filename12 = "/eos/home-d/dvargas/Salidadune/filesTDE12bits/binary_12bits_" + str + ".bit";

      cout << "Crating file binary_16bits_" << i << endl;
      fp_16 = fopen(filename16.c_str(), "wb");
      if (fp_16 == NULL) { cout << "Could not open output file fp_16" << endl; }
      fwrite(&tdeheader, sizeof(tdeheader), 1, fp_16);
      fwrite(&adcdata16, sizeof(adcdata16), 1, fp_16);
      fclose(fp_16);
      cout << "Closing file number binary_16bits_" << i << endl;

      cout << "Crating file binary_12bits_" << i << endl;
      fp_12 = fopen(filename12.c_str(), "wb");
      if (fp_12 == NULL) { cout << "Could not open output file fp_12" << endl; }
      fwrite(&tdeheader, sizeof(tdeheader), 1, fp_12);
      fwrite(&adcdata12, sizeof(adcdata12), 1, fp_12);
      fclose(fp_12);
      cout << "Closing file number binary_12bits_" << i << endl;
    }

    killswitch = false;

  }

  cout << "Finish while stamenet" << endl;

  return 0;
}

