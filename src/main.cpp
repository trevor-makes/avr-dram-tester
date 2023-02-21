#include <avr/io.h>
#include <avr/interrupt.h>

#include <stdint.h>

#ifndef __AVR_ATmega328P__
#error Port I/O hard-coded for ATmega328P
#endif

// Din-|B5 |USB| B4|-Grn LED
//    -|   |___| B3|-Red LED
//    -|         B2|-
//    -|C0       B1|-
//    -|C1       B0|-Dout
//    -|C2       D7|-A7
//  WE-|C3       D6|-A6
// RAS-|C4       D5|-A5
// CAS-|C5       D4|-A4
//    -|         D3|-A3
//    -|         D2|-A2
//  5V-|5V      GND|-GND
//    -|           |-
// GND-|GND ...  D0|-A0
//    -|    ...  D1|-A1
//         Nano

//  CAS ---\/----- A0
//   A0 ---/\/---- A1
//   A1 ----/\/--- A2
//   A2 -----/\--- CAS

// (A8) -|1 \/16|-GND
//   Din-|2   15|-CAS
//    WE-|3   14|-Dout
//   RAS-|4   13|-A6
//    A0-|5   12|-A3
//    A2-|6   11|-A4
//    A1-|7   10|-A5
//    5V-|8    9|-A7
//   4164 (4128/41256)

// PORTB bits
// NOTE Din is also built-in LED; each march pass blinks LED
constexpr uint8_t DIN = 1 << 5; // output
constexpr uint8_t LED_G = 1 << 4; // output
constexpr uint8_t LED_R = 1 << 3; // output
constexpr uint8_t DOUT = 1 << 0; // input

// PORTC bits
constexpr uint8_t WE = 1 << 3; // output, active-low
constexpr uint8_t RAS = 1 << 4; // output, active-low
constexpr uint8_t CAS = 1 << 5; // output, active-low

// Active-low control signals on PORTC
constexpr uint8_t CTRL_DEFAULT = WE | RAS | CAS; // all high
constexpr uint8_t CTRL_REFRESH = WE | CAS; // pull RAS low
constexpr uint8_t CTRL_READ_ROW = WE | CAS; // pull RAS low
constexpr uint8_t CTRL_READ_COL = WE; // pull RAS and CAS low
constexpr uint8_t CTRL_WRITE_ROW = CAS; // pull RAS and WE low
constexpr uint8_t CTRL_WRITE_COL = 0; // pull RAS, CAS, WE low

enum class Direction { UP, DOWN };

// Insert N cycle delay
template <uint8_t N = 1>
void nop() {
  __asm__ __volatile__ ("nop");
  nop<N - 1>();
}

// Base case of recursive template
template <>
inline void nop<0>() {}

uint8_t row(uint16_t address) {
  return address >> 8;
}

uint8_t col(uint16_t address) {
  return address & 0xFF;
}

[[noreturn]]
void block() {
  // Disconnect address and control
  DDRC = 0;
  PORTC = 0;
  DDRD = 0;
  PORTD = 0;
  // Loop forever
  for (;;) {}
}

[[noreturn]]
void pass() {
  // Set green LED, disconnect data
  PORTB = LED_G;
  DDRB = LED_G;
  block();
}

[[noreturn]]
void fail() {
  // Set red LED, disconnect data
  PORTB = LED_R;
  DDRB = LED_R;
  block();
}

template <uint8_t EXPECTED>
void read(uint16_t address) {
  // Strobe row address
  PORTD = row(address);
  PORTC = CTRL_READ_ROW;
  // Strobe col address
  PORTD = col(address);
  PORTC = CTRL_READ_COL;
  // Delay 2 for tCAC > 120ns, 1 for AVR read latency
  nop<3>();
  // Validate data is expected value
  static_assert(DOUT == 1, "DOUT assumed to be pin B0");
  if ((PINB & DOUT) != EXPECTED) {
    // Block forever with red LED
    fail();
  }
  // Reset control signals
  PORTC = CTRL_DEFAULT;
}

// TODO take template param for up/down?
void write(uint16_t address) {
  // Strobe row address
  PORTD = row(address);
  PORTC = CTRL_WRITE_ROW;
  // Strobe col address
  PORTD = col(address);
  PORTC = CTRL_WRITE_COL;
  // Delay for tCAS > 120 (OUT + NOP)
  nop();
  // Reset control signals
  PORTC = CTRL_DEFAULT;
}

// TODO maybe pulse another bit in PORTD to measure refresh frequency
void refresh() {
  static uint8_t refresh_row = 0;
  // Strobe row address
  PORTD = refresh_row;
  PORTC = CTRL_REFRESH; // pull RAS low
  // Delay for tRAS > 200ns
  ++refresh_row; // LDS, SUBI, STS
  // Reset control signals
  PORTC = CTRL_DEFAULT;
}

// TODO add separate param for write value
// TODO add states for Wx and Rx -> one function instead of march_w, march_rw, march_r
template <Direction DIR, uint8_t EXPECTED>
void march_rw() {
  // Data is same for all writes, so set Din outside of loop
  // NOTE maybe set DIN high on R0,Wx pass so LED blinks 3rd time
  if (EXPECTED) {
    PORTB &= ~DIN;
  } else {
    PORTB |= DIN;
  }

  // Loop over full address range, up or down
  // TODO nested for loops for 9-bit row/col (41128/41256)
  uint16_t address = 0;
  do {
    if (DIR == Direction::DOWN) --address;

    read<EXPECTED>(address);
    write(address);
    refresh();

    if (DIR == Direction::UP) ++address;
  } while (address != 0);
}

[[noreturn]]
void test() {
  // Set green LED out of phase with red LED
  PINB |= LED_G;

  for (;;) {
    // Write to all addresses
    uint16_t address = 0;
    do {
      write(address);
      ++address;
    } while (address != 0);

    // Toggle data pin
    PINB |= DIN;

    // Toggle green/red LEDs when data is high
    if ((PORTB & DIN) == DIN) {
      PINB |= LED_G;
      PINB |= LED_R;
    }
  }
}

int main() {
  // Configure output pins
  DDRB = DIN | LED_G | LED_R; // outputs
  PORTC = WE | CAS | RAS; // pull-ups first
  DDRC = WE | CAS | RAS; // outputs, active-low
  DDRD = 0xFF; // A0-A7 outputs

  test();

  // March C- algorithm
  // TODO march_w<UP, 0>(); or march<UP, W0> or march<UP, Rx, W0>
  march_rw<Direction::UP, 0>(); // march<UP, R0, W1>
  march_rw<Direction::UP, 1>(); // march<UP, R1, W0>
  march_rw<Direction::DOWN, 0>(); // march<DOWN, R0, W1>
  march_rw<Direction::DOWN, 1>(); // march<DOWN, R1, W0>
  // TODO march_r<DOWN, 0> or march<DOWN, R0> or march<DOWN, R0, Wx>

  // Block forever with green LED
  pass();
}
