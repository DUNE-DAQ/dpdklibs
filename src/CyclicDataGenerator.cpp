#include "dpdklibs/CyclicDataGenerator.hpp"

#define DEFAULT_CYCLIC_PATTERN "abcdefghijklmnopqrstuvwxyz"

using namespace dunedaq::dpdklibs::cyclicdatagenerator;

CyclicDataGenerator::CyclicDataGenerator(size_t offset) : pattern(DEFAULT_CYCLIC_PATTERN), slice_len(strlen(DEFAULT_CYCLIC_PATTERN)) {
    this->current_slice_index = offset % slice_len;
}

CyclicDataGenerator::CyclicDataGenerator(char *pattern, size_t offset) : pattern(pattern), slice_len(strlen(pattern)) {
    this->current_slice_index = offset % slice_len;
}

char CyclicDataGenerator::get_next() {
    size_t i = current_slice_index;

    if (current_slice_index + 1 == slice_len) {
        current_slice_index = 0;
    } else {
        current_slice_index++;
    }

    return pattern[i];
}

void CyclicDataGenerator::get_next_n(char *dst, size_t n) {
    char buffer[n];

    for (size_t i = 0; i < n; i++) { 
        buffer[i] = get_next();
    }

    strcpy(dst, buffer);
}

char CyclicDataGenerator::get_prev() {
    size_t i = current_slice_index;

    if (current_slice_index == 0) {
        current_slice_index = slice_len - 1;
    } else {
        current_slice_index--;
    }

    return pattern[i];
}

void CyclicDataGenerator::get_prev_n(char *dst, size_t n) {
    char buffer[n];

    for (size_t i = 0; i < n; i++) {
        buffer[i] = get_prev();
    }

    strcpy(dst, buffer);
}

CyclicDataGenerator::~CyclicDataGenerator() {}
