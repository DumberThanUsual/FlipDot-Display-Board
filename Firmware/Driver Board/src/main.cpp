#include <Arduino.h>
#include <SPI.h>

#define MODULE_WIDTH 5
#define MODULE_HEIGHT 7

#define RCLK 0
#define SRCLR 6

#define ADDR_0 1
#define ADDR_1 2
#define ADDR_2 3

#ifdef MILLIS_USE_TIMERA0 // Uses timer B0
  #error "This sketch takes over TCA0 - please use a different timer for millis"
#endif

#define DUTCY_CYCLE_RATIO 100

// Configurables
uint8_t address = 0;
bool vertical = true;

uint32_t registerFrames[35] = {0};
uint32_t registerBuffer = 0;  

uint8_t stateBuffer[5] = {0b01111111};
uint8_t frameBuffer[5] = {0};

// Shift register contol offsets
static const int rowHigh[7] = {17, 18, 19, 20, 3, 2, 1};
static const int rowLow[7] = {26, 25, 27, 28, 9, 10, 11};

static const int colHigh[5] = {7, 6, 5, 22, 23};
static const int colLow[5] = {15, 14, 13, 30, 31};

// Timing configuration
int dead_time = 10;
int saturationTime = 500;   //Flip time spent passing current through coils - 1us resolution

// Loop indexing
bool counterRunning = false;
int index = 0;

uint8_t display_config = 0;

int incomingByte = 0; // for incoming serial data
uint8_t selectedRegister = 0;
bool moduleActive = false;
bool frameBufferWrite = true;
bool fullRedraw = true;

void shift32(uint32_t registerFrame) {  // Shift 32 bits to registers in 8 bit chunks
  uint8_t shift = 32U; 
  do {
      shift -= 8U;
      SPI.transfer((uint8_t)(registerFrame >> shift));
  } while ( shift ) ;
}

  void clockRegisters() {  // Cycle RCLK pin
    digitalWrite(RCLK, HIGH);
    digitalWrite(RCLK, LOW);
  }

uint32_t gen_register_state(uint8_t segmentX, uint8_t segmentY, bool segmentValue) {
  uint32_t register_state = 0;
  if (segmentValue) {
    register_state = register_state | ((uint32_t)1 << colLow[segmentX]);
    register_state = register_state & ~((uint32_t)1 << colHigh[segmentX]);

    register_state = register_state | ((uint32_t)1 << rowHigh[segmentY]);
    register_state = register_state & ~((uint32_t)1 << rowLow[segmentY]);
  } 
  else {
    register_state = register_state | ((uint32_t)1 << colHigh[segmentX]);
    register_state = register_state & ~((uint32_t)1 << colLow[segmentX]);

    register_state = register_state | ((uint32_t)1 << rowLow[segmentY]);
    register_state = register_state & ~((uint32_t)1 << rowHigh[segmentY]);
  } 
  return register_state;
}

void genStates() {
  for (int sweep = 0; sweep < 5; sweep++) {
    for (int step = 0; step < 7; step++) {
      bool currentValue = bitRead(stateBuffer[sweep], step);
      bool segmentValue = bitRead(frameBuffer[sweep], step);
      if (currentValue != segmentValue || fullRedraw) {
        registerFrames[sweep * 7 + step] = gen_register_state(sweep, step, segmentValue);
        bitWrite(stateBuffer[sweep], step, segmentValue);
      } else {
        registerFrames[sweep * 7 + step] = 0;
      }
    }
  }
}

#define RASTER_MODE_HORIZONTAL false
#define RASTER_MODE_VERTICAL true
bool raster_mode = RASTER_MODE_HORIZONTAL;

#define DATA_JUSTIFICATION_LSB false
#define DATA_JUSTIFICATION_MSB true
bool data_justification = DATA_JUSTIFICATION_LSB;

#define HORIZONTAL_DIRECTION_LSB_LEFT false
#define HORIZONTAL_DIRECTION_MSB_LEFT true
bool horizontal_direction = HORIZONTAL_DIRECTION_LSB_LEFT;

#define VERTICAL_DIRECTION_LSB_BOTTOM false
#define VERTICAL_DIRECTION_MSB_BOTTOM true
bool vertical_direction = VERTICAL_DIRECTION_LSB_BOTTOM;

void increment_direction (uint8_t &val, bool direction) {
  if (direction) {
    val ++;
  } else {
    val --;
  }
}

bool continue_iterate (uint8_t current, uint8_t target, bool direction) {
  if (direction) {
    return (current <= target);
  } else {
    return (current >= target);
  }
}

void handle_register_write(uint8_t reg, uint8_t val) {
  uint8_t normalised_scan = 0; // LSB justified, LSB bottom/left pixel data

  uint8_t data_start_bit;
  uint8_t data_end_bit;
  bool data_iteration_direction;
  if (raster_mode == RASTER_MODE_VERTICAL) {
    if (vertical_direction == VERTICAL_DIRECTION_LSB_BOTTOM) {
      data_start_bit = 0;
      data_end_bit = 6;
    } else {
      data_start_bit = 6;
      data_end_bit = 0;
    }
  } else {
    uint8_t data_offset;
    if(data_justification == DATA_JUSTIFICATION_LSB) {
      data_offset = 0;
    } else {
      data_offset = 2;
    }
    if (horizontal_direction == HORIZONTAL_DIRECTION_LSB_LEFT) {
      data_start_bit = 0 + data_offset;
      data_end_bit = 4 + data_offset;
    } else {
      data_start_bit = 4 + data_offset;
      data_end_bit = 0 + data_offset;
    }
  }
  data_iteration_direction = (data_end_bit > data_start_bit);
  for (
    uint8_t input_bit = data_start_bit, output_index = 0; 
    continue_iterate(input_bit, data_end_bit, data_iteration_direction) && output_index < 8; 
    increment_direction(input_bit, data_iteration_direction), output_index++
  ) {
    bitWrite(normalised_scan, output_index, bitRead(val, input_bit));
  }

  if (raster_mode == RASTER_MODE_VERTICAL) {
    if (horizontal_direction == HORIZONTAL_DIRECTION_LSB_LEFT) {  
      frameBuffer[reg] = normalised_scan;
    } else {
      frameBuffer[4 - reg] = normalised_scan;
    }
  } else {
    for(int i = 0; i < 5; i ++) {
      uint8_t buffer_index;
      if (horizontal_direction == HORIZONTAL_DIRECTION_LSB_LEFT) {
        buffer_index = i;
      } else {  
        buffer_index = 4-i;
      }

      if (vertical_direction == VERTICAL_DIRECTION_LSB_BOTTOM) {
        bitWrite(frameBuffer[buffer_index], reg, bitRead(normalised_scan, i));
      } else {
        bitWrite(frameBuffer[buffer_index], 6-reg, bitRead(normalised_scan, i));
      }
    }
  }
}


void setup() {
  _PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, CLKCTRL_PEN_bm);  // Set 10 MHz clock

  pinMode(RCLK, OUTPUT);
  pinMode(SRCLR, OUTPUT);

  pinMode(ADDR_0, INPUT_PULLUP);
  pinMode(ADDR_1, INPUT_PULLUP);
  pinMode(ADDR_2, INPUT_PULLUP);

  address = digitalRead(ADDR_0) | digitalRead(ADDR_1) << 1 | digitalRead(ADDR_2) << 2;

  digitalWrite(SRCLR, HIGH);
  SPI.begin();
  Serial.begin(115200);

  takeOverTCA0();
  //TCA0.SINGLE.CTRLB = (TCA_SINGLE_WGMODE_NORMAL_gc); //Normal mode counter - default
  TCA0.SINGLE.CTRLESET = TCA_SINGLE_DIR_DOWN_gc;
  TCA0.SINGLE.INTCTRL = (TCA_SINGLE_CMP0_bm | TCA_SINGLE_OVF_bm); // enable compare channel 0 and overflow interrupts

  TCA0.SINGLE.PER = (saturationTime + dead_time) * 10; //count from top
  TCA0.SINGLE.CMP0 = saturationTime * 10; //compare at midpoint

  //TCA0.SINGLE.CTRLA = TCA_SINGLE_ENABLE_bm; // enable the timer 100ns increments per step at 10MHz clock
}


void loop() {

  // SERIAL PROTOCOL:
  // Module address and register selection:
  // 0b1AAARRRR
  // AAA - ADDRESS, RRRR = register
  // Register write:
  // 0b0VVVVVVV
  // VVVVVVV - Value

  // Registers 0 - 6: Framebuffer

  // Register 7: Framebuffer write with full redraw on selection

  // Register 8: Display configuration - assumes vertical display and defaults to 0:
  // - Bit 0: raster mode
  //   - 0: 5-bit horizontal registers
  //   - 1: 7-bit vertical registers - uses registers 0-4
  // - Bit 1: Data justification in 5-bit register mode
  //   - 0: LSB justification
  //   - 1: MSB justification
  // - Bit 2: Horizontal direction
  //   - 0: LSB represents leftmost pixel
  //   - 1: MSB represents leftmost pixel
  // - Bit 3: Vertical direction
  //   - 0: LSB represents bottom pixel
  //   - 1: MSB represents bottom pixel

  // Register 9: Display framerate - sets time current is developed per pixel
  //                                                          Higer framerate means lower time and requires higher supply voltage
  //                                                          Duty cycle per pixel is fixed at 1%
  //                                                          Limited to 5 ms

  while (Serial.available()) {
    incomingByte = Serial.read();
    if (bitRead(incomingByte, 7)) {
      if ((incomingByte & 0b01110000) >> 4 == address) {

        moduleActive = true;
        selectedRegister = incomingByte & 0b00001111;

        if (selectedRegister == 7) {
          fullRedraw = true;
          moduleActive = false;
        }
      }
      else {
        moduleActive = false;
      }
    }
    else if (moduleActive) {
      if (selectedRegister <= 6) {
        handle_register_write(selectedRegister, incomingByte);
        selectedRegister ++;
        if (selectedRegister > 6) {
          selectedRegister = 0;
        }
      }
      if (selectedRegister == 8) {
        if(bitRead(incomingByte, 0)) {
          raster_mode = RASTER_MODE_VERTICAL;
        }
        else {
          raster_mode = RASTER_MODE_HORIZONTAL;
        }
        if(bitRead(incomingByte, 1)) {
          data_justification = DATA_JUSTIFICATION_MSB;
        }
        else {
          data_justification = DATA_JUSTIFICATION_LSB;
        }
        if(bitRead(incomingByte, 2)) {
          horizontal_direction = HORIZONTAL_DIRECTION_MSB_LEFT;
        }
        else {
          horizontal_direction = HORIZONTAL_DIRECTION_LSB_LEFT;
        }
        if(bitRead(incomingByte, 3)) {
          vertical_direction = VERTICAL_DIRECTION_MSB_BOTTOM;
        }
        else {
          vertical_direction = VERTICAL_DIRECTION_LSB_BOTTOM;
        }
      }
      if (selectedRegister == 9 ) {
        int us_per_flip = (1000000/DUTCY_CYCLE_RATIO)/incomingByte;
        us_per_flip -= 2*dead_time;
        us_per_flip = constrain(us_per_flip, 1, 5000);
        saturationTime = us_per_flip;
        TCA0.SINGLE.PER = (saturationTime + dead_time) * 10; //count from top
        TCA0.SINGLE.CMP0 = saturationTime * 10; //compare at midpoint
      }
    }
  }

  if (!counterRunning){
    genStates();
    fullRedraw = false;
    TCA0.SINGLE.CNT = (saturationTime  + dead_time)*10;
    counterRunning = true;
    index = 0;
    TCA0.SINGLE.CTRLA = TCA_SINGLE_ENABLE_bm;
  }
}


ISR(TCA0_OVF_vect) {    // on overflow, shift out 0 and enter recovery time
  if (index >= DUTCY_CYCLE_RATIO) {
    TCA0.SINGLE.CTRLA = 0;
    counterRunning = false;
  }
  shift32(0x00); 
  clockRegisters();
  TCA0.SINGLE.INTFLAGS  = TCA_SINGLE_OVF_bm; // Always remember to clear the interrupt flags, otherwise the interrupt will fire continually!
}


ISR(TCA0_CMP0_vect) {    // on compare, get next pixel and set state
  if (index >= 35) {
    shift32(0);
    clockRegisters();
  } else {
    shift32(registerFrames[index]);
    clockRegisters();
  }
  index ++;
  TCA0.SINGLE.INTFLAGS  = TCA_SINGLE_CMP0_bm; // Always remember to clear the interrupt flags, otherwise the interrupt will fire continually!
}