#include <Arduino.h>          

#define FRAMERATE 30U

void setup() {
  Serial2.begin(115200);
  Serial.begin(115200);
  Serial.println("HELP");
  for (uint8_t module = 0; module < 8; module ++) {
    Serial2.write(0b10000000 | (module << 4) | 8U);
    Serial2.write(0b00001000);
    Serial2.write(0b10000000 | (module << 4) | 9U);
    Serial2.write(FRAMERATE);
  }
}

bool tick = false;

bool getPixel() {
  if (tick) {
    return 0b01111111;
  } 
  return 0b00000000;
  return tick;
}

void loop() {
  for (int y = 0; y < 7; y ++) {
    for (int module = 0; module < 8; module ++) {
      Serial2.write(0b10000000 | (module << 4) | y);
      if (tick) {
        Serial2.write(0b01111111);
      } else {
        Serial2.write(0b00000000);
      } 
    }
      delay(66);
  }

  tick = !tick;
  //delay(500);
}
  