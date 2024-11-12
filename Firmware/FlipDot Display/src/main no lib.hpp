#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "Font5x7Fixed.h"
#include "Font4x5Fixed.h"

#define MODULE_WIDTH 5
#define MODULE_HEIGHT 7

#define MODULES 7

int rowHigh[7] = {7, 6, 5, 20, 21, 22, 23};
int rowLow[7] = {13, 14, 15, 28, 29, 30, 31};

int colHigh[5] = {1, 2, 3, 18, 17};
int colLow[5] = {9, 10, 11, 26, 25};

uint32_t registerFrames[7][35] = {0};
uint32_t registerBuffer = 0;

GFXcanvas1 frameBuffer(37, 7);

class clockFace {
  public:
    void drawToCancvas(GFXcanvas1* canvas) {
      canvas->setFont(&Font5x7Fixed);
      canvas->fillScreen(false);
      canvas->setCursor(0, 7);
      canvas->print("04:20");
    }
};

void onItemSelect(String name) {
  Serial.println(name);
}

class menuItem {
  public:
    String name = "Setup";
    void (*test)(String);
    menuItem(String a, void (*aaa)(String)) {
      test = aaa;
      name = a;
    }

    void onSelect() {
      //test(name);
    }

    void writeToCanvas(GFXcanvas1* canvas) {
      canvas->setFont(&Font4x5Fixed);
      canvas->fillScreen(false);
      canvas->setCursor(0, 7);
      canvas->print(name);
    }
};


menuItem testMenu[6] = {menuItem("testing", &onItemSelect), menuItem("this", &onItemSelect), menuItem("haiii", &onItemSelect), menuItem("scroller", &onItemSelect), menuItem("thing", &onItemSelect), menuItem("", &onItemSelect)};

class menu {
  public:
    menu() {
      
    }
}

// Compositor coordinates
int menuPadding = 1;
int canvasHeight = MODULE_HEIGHT + menuPadding;

int displayX = 0;
int displayY = 0;

int activeCol = 0;
int activeRow = 0;

int inactiveCol = 0;
int inactiveRow = 0;

int selectionCol = 0;
int selectionRow = 0;
int selectionPath[10] = {0};

int animationStartingRow = 0;

// Compositor buffers
GFXcanvas1 activeWindow(37, canvasHeight);
GFXcanvas1 inactiveWindow(37, canvasHeight);

int calculateDelay(int step, int minDelay = 50, int maxDelay = 100, int acceleration = 10) {
  int delay = maxDelay - (step*acceleration);
  if (delay < minDelay) {
    return minDelay;
  }
  else {
    return delay;
  }
}

// Load items into active window buffer
void redrawWindows() {
  testMenu[activeRow].writeToCanvas(&activeWindow);
  testMenu[inactiveRow].writeToCanvas(&inactiveWindow);
}

// Modify register buffer to flip single segment
void registerSet(int segmentX, int segmentY, bool segmentValue) {
  if (segmentValue) {
    registerBuffer = registerBuffer | (1 << colLow[segmentX]);
    registerBuffer = registerBuffer & ~(1 << colHigh[segmentX]);

    registerBuffer = registerBuffer | (1 << rowHigh[segmentY]);
    registerBuffer = registerBuffer & ~(1 << rowLow[segmentY]);
  } 
  else {
    registerBuffer = registerBuffer | (1 << colHigh[segmentX]);
    registerBuffer = registerBuffer & ~(1 << colLow[segmentX]);

    registerBuffer = registerBuffer | (1 << rowLow[segmentY]);
    registerBuffer = registerBuffer & ~(1 << rowHigh[segmentY]);
  }
}

// Generate register states to update module
void genStates(int module) {
  for (int sweep = 0; sweep < 5; sweep++) {
    for (int step = 0; step < 7; step++) {
      bool segmentValue = frameBuffer.getPixel((6-module)*5 + sweep, step);
      int segmentX = sweep;
      int segmentY = step;
      registerSet (segmentX, segmentY, segmentValue);
      registerFrames[module][sweep*7 + step] = registerBuffer;
      registerBuffer = 0;
    }
  } 
  
}

// Shift 32 bits to registers in 8 bit chunks
void shift32(uint32_t registerFrame) {
  SPI.transfer((registerFrame & 0xff000000) >> 24);
  SPI.transfer((registerFrame & 0x00ff0000) >> 16);
  SPI.transfer((registerFrame & 0x0000ff00) >> 8);
  SPI.transfer((registerFrame & 0x000000ff));
}

// Cycle SCLK pin
void clockRegisters() {
  digitalWrite(5, HIGH);
  delayMicroseconds(1);
  digitalWrite(5, LOW);
}

void updateDisplay(int saturationTime = 2, int flipTime = 5) {
  
  for (int module = 6; module >= 0; module --) {
    genStates(module);
  }

  for (int registerFrame = 0; registerFrame < 35; registerFrame ++) {
    for (int module = 6; module >= 0; module--) {
      shift32(registerFrames[module][registerFrame]);
    }
    clockRegisters();
    delayMicroseconds(saturationTime);
    for (int i = 0; i < 8; i++) {
      shift32(0x00); 
    }
    clockRegisters();
    delayMicroseconds(flipTime - saturationTime);
  }
  for (int i = 0; i < 8; i++) {
    shift32(0x00); 
  }
  clockRegisters();
}

/*
void updateDisplay() {
  
  for (int module = 6; module >= 0; module --) {
    genStates(module);
  }

  for (int registerFrame = 0; registerFrame < 35; registerFrame ++) {
    for (int module = 6; module >= 0; module--) {
      shift32(0x00); 
      shift32(0x00); 
      shift32(0x00); 
      shift32(0x00); 
      shift32(0x00); 
      shift32(0x00); 
      shift32(0x00); 
      shift32(registerFrames[module][registerFrame]);
      for (int i = 0; i < module; i++) { 
        shift32(0x00); 
      }
      clockRegisters();
      delayMicroseconds(saturationTime);
    }
  }
  for (int i = 0; i < 8; i++) {
    shift32(0x00); 
  }
  clockRegisters();
}
*/


// Generate next frame in window transition animation
// Direction: true - vertical transition, false - horizontal transition
// Returns delay in ms until next frame, 0ms if animation is comlpete

int animateTransition(bool direction = true) {
  int targetPos= selectionRow * (canvasHeight);
  activeRow = displayY/canvasHeight;
  inactiveRow = (displayY/canvasHeight) + 1;
  redrawWindows();

  if (targetPos - displayY > 0) {
    displayY ++;
    activeRow = displayY/canvasHeight;
    inactiveRow = (displayY/canvasHeight) + 1;
    redrawWindows();
  }
  else if (targetPos - displayY < 0) {
    displayY --;
    activeRow = displayY/canvasHeight;
    inactiveRow = (displayY/canvasHeight) + 1;
    redrawWindows();
  } else {
    return 0;
  }

  float remainingDistance = abs(targetPos - displayY);

  for (int FBY = 0; FBY < 8; FBY ++) {
    for (int FBX = 0; FBX < 36; FBX ++) {
      bool pixelValue = false;
      int scanLine = displayY + FBY;
      if ((scanLine - (activeRow*canvasHeight)) <= canvasHeight) {
        pixelValue = activeWindow.getPixel(FBX, ((displayY + FBY) - (activeRow*canvasHeight)));
      }
      else {
        pixelValue = inactiveWindow.getPixel(FBX, ((displayY + FBY) - (inactiveRow*canvasHeight)));
      }
      frameBuffer.drawPixel(FBX, FBY, pixelValue);
    }
  }

  frameBuffer.drawLine(34, 0, 34, 6, false);
  frameBuffer.writePixel(34, activeRow, true);

  

  if (targetPos - displayY == 0) {
    updateDisplay(500, 750);
    testMenu[selectionRow].onSelect();
    return 0;
  }else {
    updateDisplay();
    return  constrain(50.0/remainingDistance, 1, 5000);
  }

  
}

void setup() {
  pinMode(5, OUTPUT);
  SPI.begin();
  Serial.begin(112500);

  while(!Serial);
  Serial.println("Menu 4.x");
  Serial.println("Use keys + - * /");
  Serial.println("to control the menu navigation");

  frameBuffer.fillScreen(false);
  updateDisplay();
  delay(1000);
  frameBuffer.fillScreen(true);
  updateDisplay();
  delay(1000);
}

void loop() {
  selectionRow = 0;
  int animationDelay = 1000;
  while (animationDelay != 0) {
    animationDelay = animateTransition();
    delay(animationDelay);
  }
  delay(500);
  nav.poll();
  selectionRow = 1;
  animationDelay = 1000;
  while (animationDelay != 0) {
    animationDelay = animateTransition();
    delay(animationDelay);
  }
  delay(500);
nav.poll();
  selectionRow = 3;
  animationDelay = 1000;
  while (animationDelay != 0) {
    animationDelay = animateTransition();
    delay(animationDelay);
  }
  delay(500);
  nav.poll();
  selectionRow = 4;
  animationDelay = 1000;
  while (animationDelay != 0) {
    animationDelay = animateTransition();
    delay(animationDelay);
  }
  delay(1000);
  nav.poll();
}
