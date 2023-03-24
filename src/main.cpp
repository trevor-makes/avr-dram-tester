// Copyright (c) 2023 Trevor Makes

#include <avr/io.h>
#include <stdint.h>

// Insert N cycle delay
template <uint8_t N = 1>
void delay() {
  __asm__ __volatile__ ("nop");
  delay<N - 1>();
}

// Base case of recursive template
template <>
inline void delay<0>() {}

#ifdef __AVR_ATmega328P__

// PORTB [ x x DIN LED_G LED_R SEL - DOUT ]
// NOTE Din is also built-in LED; each march pass blinks LED
constexpr uint8_t DIN = 1 << 5; // output
constexpr uint8_t LED_G = 1 << 4; // output
constexpr uint8_t LED_R = 1 << 3; // output
constexpr uint8_t MODE_SEL = 1 << 2; // input, pullups
constexpr uint8_t A8 = 1 << 1; // output
constexpr uint8_t DOUT = 1 << 0; // input

// PORTC [ x x CAS RAS WE RE ERR - ]
constexpr uint8_t ERR = 1 << 1; // output
constexpr uint8_t RE = 1 << 2; // output, active-low (test only, not used by DRAM)
constexpr uint8_t WE = 1 << 3; // output, active-low
constexpr uint8_t RAS = 1 << 4; // output, active-low
constexpr uint8_t CAS = 1 << 5; // output, active-low

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
  TCCR2A = 1 << WGM21; // CTC mode (count to OCR2A)
  TCCR2B = 1 << CS21 | 1 << CS20; // set 32 prescaler (starts timer)
  while ((TIFR2 & (1 << OCF2A)) == 0) {} // wait for timer

  // 8 RAS cycle "wake-up" on any row
  for (uint8_t i = 8; i != 0; --i) {
    PORTC = CTRL_REFRESH;
    delay<2>();
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
  delay<3>();
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
  delay();
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
  for (;;) {
    // Use same byte for row and col (diagonal)
    // This is the fastest we can toggle CAS after RAS, stressing row access time
    PORTD = address;
    PORTC = CTRL_READ_ROW;
    PORTC = CTRL_READ_COL;
    // Delay for read access time
    // Probe RAS and DOUT with scope
    delay<2>();
    ++address;
    if ((PINB & DOUT) != (address & 1)) fail();
    PORTC = CTRL_DEFAULT;
  }
}

#else
#error Must define I/O for current chip; see ifdef __AVR_ATmega328P__ above
#endif

// Perform one step of march algorithm
template <Direction DIR, Read READ, Write WRITE>
void march() {
  // Data is same for all writes, so set Din once outside loop
  set_data<WRITE>();

  // Loop over full address range, up or down
  // Read then write (both optional) once at each address along the way
  // NOTE use the lower byte as the row so a refresh is done at each step
  uint16_t address = 0;
  do {
    if (DIR == DN) --address;
    const uint8_t col = address >> 8;
    const uint8_t row = address & 0xFF;
    if (READ != RX && read(row, col) != READ) fail();
    if (WRITE != WX) write(row, col);
    if (DIR == UP) ++address;
  } while (address != 0);
}

// Default unpecified write to WX
template <Direction DIR, Read READ>
void march() {
  march<DIR, READ, WX>();
}

// Default unpecified read to RX
template <Direction DIR, Write WRITE>
void march() {
  march<DIR, RX, WRITE>();
}

int main() {
  config();
  init_dram();

  if (is_measure_mode()) {
    // Loop forever
    measure_rac();
  }

  // Run march C- algorithm in a loop
  // LED turns green after first success, but stays red after first failure
  for (;;) {
    march<UP, W0>();
    march<UP, R0, W1>();
    march<UP, R1, W0>();
    march<DN, R0, W1>();
    march<DN, R1, W0>();
    march<DN, R0>();
    pass();
  }
}
