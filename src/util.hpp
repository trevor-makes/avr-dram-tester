// Copyright (c) 2023 Trevor Makes

#pragma once

#include <stdint.h>

// Insert N cycle delay
template <uint8_t N = 1>
void delay_cycles() {
  __asm__ __volatile__ ("nop");
  delay_cycles<N - 1>();
}

// Base case of recursive template
template <>
inline void delay_cycles<0>() {}

// Convert bit index to mask
template <typename N>
constexpr uint8_t bit_mask(N n) { return 1 << n; }

// Convert multiple bit indices to mask
template <typename N, typename... ARGS>
constexpr uint8_t bit_mask(N n, ARGS... args) { return bit_mask(n) | bit_mask(args...); }
