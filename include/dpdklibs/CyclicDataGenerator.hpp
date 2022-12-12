#ifndef CYCLICDATAGENERATOR_HPP_
#define CYCLICDATAGENERATOR_HPP_

#include <stdlib.h>
#include <string.h>

namespace dunedaq {
namespace dpdklibs {
namespace cyclicdatagenerator {

class CyclicDataGenerator
{
public:
  CyclicDataGenerator(size_t offset = 0);
  CyclicDataGenerator(char* pattern, size_t offset = 0);
  ~CyclicDataGenerator();

  char get_next();
  void get_next_n(char* dst, size_t n);
  char get_prev();
  void get_prev_n(char* dst, size_t n);

private:
  const char* pattern;
  const size_t slice_len;
  size_t current_slice_index = 0;
};

} // namespace cyclicdatagenerator
} // namespace dpdklibs
} // namespace dunedaq

#endif
