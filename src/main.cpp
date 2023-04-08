// Copyright (c) 2023 Trevor Makes

#include "util.hpp"

#include <avr/io.h>
#include <stdint.h>

#ifdef __AVR_ATmega328P__

// PORTB [ x x DIN LED_G LED_R SEL - DOUT ]
// NOTE Din is also built-in LED; each march pass blinks LED
constexpr uint8_t DIN = bit_mask(5); // output
constexpr uint8_t LED_G = bit_mask(4); // output
constexpr uint8_t LED_R = bit_mask(3); // output
constexpr uint8_t MODE_SEL = bit_mask(2); // input, pullups
constexpr uint8_t A8 = bit_mask(1); // output
constexpr uint8_t DOUT = bit_mask(0); // input

// PORTC [ x x CAS RAS WE RE ERR - ]
constexpr uint8_t ERR = bit_mask(1); // output
constexpr uint8_t RE = bit_mask(2); // output, active-low (test only, not used by DRAM)
constexpr uint8_t WE = bit_mask(3); // output, active-low
constexpr uint8_t RAS = bit_mask(4); // output, active-low
constexpr uint8_t CAS = bit_mask(5); // output, active-low

// Active-low control signals on PORTC
constexpr uint8_t CTRL_DEFAULT = ERR | RE | WE | RAS | CAS; // all high
constexpr uint8_t CTRL_REFRESH = CTRL_DEFAULT & ~RAS; // pull RAS low
constexpr uint8_t CTRL_READ_ROW = CTRL_DEFAULT & ~RAS & ~RE; // pull RAS and RE low
constexpr uint8_t CTRL_READ_COL = CTRL_READ_ROW & ~CAS; // pull RAS, RE, and CAS low
constexpr uint8_t CTRL_WRITE_ROW = CTRL_DEFAULT & ~RAS & ~WE; // pull RAS and WE low
constexpr uint8_t CTRL_WRITE_COL = CTRL_WRITE_ROW & ~CAS; // pull RAS, CAS, and WE low
constexpr uint8_t CTRL_ERROR = CTRL_DEFAULT & ~ERR; // pull ERR low

enum Direction { UP, DN };
enum Read { R0 = 0, R1 = DOUT, RX };
enum Write { W0, W1, WX };
enum Bit { Bit0, Bit1, BitX };
enum Chip { DRAM_4164, DRAM_41256 };

// Configure output pins
void config() {
  PORTB = MODE_SEL; // input w/ pull-up
  DDRB = DIN | LED_G | LED_R | A8; // outputs
  PORTC = CTRL_DEFAULT; // pull-ups first
  DDRC = CTRL_DEFAULT; // outputs, active-low
  DDRD = 0xFF; // A0-A7 outputs
}

bool is_measure_mode() {
  return (PINB & MODE_SEL) == 0;
}

void pass() {
  // Set green LED only if red LED is clear
  if ((PORTB & LED_R) == 0) PORTB |= LED_G;
}

void fail() {
  // Pulse error pin
  PORTC = CTRL_ERROR;
  // Set red LED, clear green LED
  PORTB |= LED_R;
  PORTB &= ~LED_G;
}

// Required startup procedure per DRAM datasheets
void init_dram() {
  // Delay 500us for bias generator
  // Others only ask for 100us, but Intel specifies 500us!
  // 250 * 32 * 62.5ns = 500us
  OCR2A = 250; // count to 250
  TCCR2A = bit_mask(WGM21); // CTC mode (count to OCR2A)
  TCCR2B = bit_mask(CS21, CS20); // set 32 prescaler (starts timer)
  while ((TIFR2 & bit_mask(OCF2A)) == 0) {} // wait for timer

  // 8 RAS cycle "wake-up" on any row
  for (uint8_t i = 8; i != 0; --i) {
    PORTC = CTRL_REFRESH;
    delay_cycles<2>();
    PORTC = CTRL_DEFAULT;
  }
}

// Set upper address bit
template <Bit BIT>
void set_a8() {
  if (BIT == Bit0) {
    PORTB &= ~A8;
  } else if (BIT == Bit1) {
    PORTB |= A8;
  }
}

// Perform read cycle at `address`
template <Bit ROW_A8 = BitX, Bit COL_A8 = BitX>
Read read(uint8_t row, uint8_t col) {
  // Strobe row address
  PORTD = row;
  set_a8<ROW_A8>();
  PORTC = CTRL_READ_ROW;
  // Strobe col address
  PORTD = col;
  set_a8<COL_A8>();
  PORTC = CTRL_READ_COL;
  // Delay 2 for tCAC > 120ns, +1 for AVR read latency
  delay_cycles<3>();
  // Validate data is expected value
  Read result = Read(PINB & DOUT);
  // Reset control signals
  PORTC = CTRL_DEFAULT;
  return result;
}

// Perform write cycle at `address`
template <Bit ROW_A8 = BitX, Bit COL_A8 = BitX>
void write(uint8_t row, uint8_t col) {
  // Strobe row address
  PORTD = row;
  set_a8<ROW_A8>();
  PORTC = CTRL_WRITE_ROW;
  // Strobe col address
  PORTD = col;
  set_a8<COL_A8>();
  PORTC = CTRL_WRITE_COL;
  // Delay for tCAS > 120 (OUT + NOP)
  delay_cycles();
  // Reset control signals
  PORTC = CTRL_DEFAULT;
}

// Set Din to `WRITE` parameter
template <Write WRITE>
void set_data() {
  if (WRITE == W0) {
    PORTB &= ~DIN; // set data 0
  } else { // W1, WX
    PORTB |= DIN; // set data 1
  }
}

// Detect 41256 by writing and reading at different values of A8
bool is_41256() {
  // Write 1 to lower bank
  set_data<W1>();
  write<Bit0, Bit0>(0, 0);
  // Write 0 to upper bank
  set_data<W0>();
  write<Bit1, Bit1>(0, 0);
  // If lower bank still reads 1, it's a 256
  return read<Bit0, Bit0>(0, 0) == R1;
}

[[noreturn]]
void measure_rac() {
  uint8_t address = 0;

  // Write alternating bits along diagonal
  do {
    // Toggle data
    PINB |= DIN;
    // Use same byte for row and col (diagonal)
    PORTD = address;
    PORTC = CTRL_WRITE_ROW;
    PORTC = CTRL_WRITE_COL;
    // Delay for CAS strobe width
    ++address;
    PORTC = CTRL_DEFAULT;
  } while (address != 0);

  // Read forever along diagonal
  uint8_t blinks = 2;
  uint16_t phase = 0;
  for (;;) {
    // Toggle input capture edge and reset flag
    TCCR1B ^= bit_mask(ICES1);
    TIFR1 |= bit_mask(ICF1);
    // Start input capture timer
    TCCR1B |= bit_mask(CS10);
    // Use same byte for row and col (diagonal)
    // This is the fastest we can toggle CAS after RAS, stressing row access time
    PORTD = address;
    PORTC = CTRL_READ_ROW;
    PORTC = CTRL_READ_COL;
    // Delay for read access time
    // Probe RAS and DOUT with scope
    delay_cycles<2>();
    ++address;
    // Test input capture flag
    if ((TIFR1 & bit_mask(ICF1)) != 0) {
      // All chips tested at 5 counts, so use this as median
      // Faster chips get 1 blink, slower chips get 3 blinks
      // TODO clean this up, account for CPU clock rate
      uint8_t count = ICR1L;
      if (count > 5) {
        blinks = 3;
      } else if (count < 5) {
        blinks = 1;
      }
      TIFR1 |= bit_mask(ICF1);
    } else {
      fail();
    }
    PORTC = CTRL_DEFAULT;
    // Stop input capture timer
    TCCR1B &= ~bit_mask(CS10);
    TCNT1 = 0;

    // Blink green LED between passes
    if (address == 0) {
      if ((phase & 0xFF) == 0) {
        if ((phase >> 8 & 0x03) == 0 && (phase >> 10 & 0x03) < blinks) {
          PORTB |= LED_G;
        } else if ((phase >> 8 & 0x03) == 0x02) {
          PORTB &= ~LED_G;
        }
      }
      ++phase;
    }
  }
}

#else
#error Must define I/O for current chip; see ifdef __AVR_ATmega328P__ above
#endif

// Loop over the 8-bit x 8-bit address range, up or down
// Read then write (both optional) once at each address along the way
// NOTE use the lower byte as the row so a refresh is done at each step
template <Direction DIR, Read READ, Write WRITE, Bit ROW_A8 = BitX, Bit COL_A8 = BitX>
void march_once() {
  if (ROW_A8 == COL_A8 && ROW_A8 != BitX) {
    // Optimization for non-changing A8 value
    set_a8<ROW_A8>();
    march_once<DIR, READ, WRITE>();
  } else {
    uint16_t address = 0;
    do {
      if (DIR == DN) --address;
      const uint8_t col = address >> 8;
      const uint8_t row = address & 0xFF;
      if (READ != RX && read<ROW_A8, COL_A8>(row, col) != READ) fail();
      if (WRITE != WX) write<ROW_A8, COL_A8>(row, col);
      if (DIR == UP) ++address;
    } while (address != 0);
  }
}

// Perform one step of march algorithm
template <Chip CHIP, Direction DIR, Read READ, Write WRITE>
void march_step() {
  // Data is same for all writes, so set Din once outside loop
  set_data<WRITE>();

  if (CHIP == DRAM_41256) {
    if (DIR == UP) {
      // Increment A8 bits
      march_once<UP, READ, WRITE, Bit0, Bit0>();
      march_once<UP, READ, WRITE, Bit1, Bit0>();
      march_once<UP, READ, WRITE, Bit0, Bit1>();
      march_once<UP, READ, WRITE, Bit1, Bit1>();
    } else {
      // Decrement A8 bits
      march_once<DN, READ, WRITE, Bit1, Bit1>();
      march_once<DN, READ, WRITE, Bit0, Bit1>();
      march_once<DN, READ, WRITE, Bit1, Bit0>();
      march_once<DN, READ, WRITE, Bit0, Bit0>();
    }
  } else {
    march_once<DIR, READ, WRITE>();
  }
}

// Default unpecified write to WX
template <Chip CHIP, Direction DIR, Read READ>
void march_step() {
  march_step<CHIP, DIR, READ, WX>();
}

// Default unpecified read to RX
template <Chip CHIP, Direction DIR, Write WRITE>
void march_step() {
  march_step<CHIP, DIR, RX, WRITE>();
}

// Run march C- algorithm in a loop
// LED turns green after first success, but stays red after first failure
template <Chip CHIP>
void march() {
  for (;;) {
    march_step<CHIP, UP, W0>();
    march_step<CHIP, UP, R0, W1>();
    march_step<CHIP, UP, R1, W0>();
    march_step<CHIP, DN, R0, W1>();
    march_step<CHIP, DN, R1, W0>();
    march_step<CHIP, DN, R0>();
    pass();
  }
}

int main() {
  config();
  init_dram();

  if (is_measure_mode()) {
    // Loop forever
    measure_rac();
  }

  if (is_41256()) {
    march<DRAM_41256>();
  } else {
    march<DRAM_4164>();
  }
}
