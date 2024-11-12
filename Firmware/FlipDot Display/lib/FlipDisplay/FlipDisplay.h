/*

  FlipDisplay.h - Library for controlling flip display.

*/

#ifndef FlipDisplay_h
#define FlipDisplay_h

#include "Arduino.h"

class FlipDisplay 
{
  public:
    Display5x7();
    void begin();
    void dot();
    void dash();
  private:
    int _pin;
};

#endif