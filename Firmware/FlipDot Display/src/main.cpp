#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

#include <menu.h>
#include <menuIO/serialOut.h>
#include <menuIO/serialIn.h>
#include <menuIO/chainStream.h>

#include "Font5x7Fixed.h"
#include "Font4x5Fixed.h"
#include "Font4x7Fixed.h"
#include "Font3x5FixedNum.h"

#define MODULE_WIDTH 5
#define MODULE_HEIGHT 7
#define DISPLAY_WIDTH 35
#define DISPLAY_HEIGHT 7

#define SCROLL_CANVAS_HEIGHT 8
#define SCROLL_CANVAS_WIDTH 36


#define HOME_ROWS 1

using namespace Menu;

#define MAX_DEPTH 3

int chooseTest=-1;//some variable used by your code (not necessarily an int)
CHOOSE(chooseTest,chooseMenu,"Test",doNothing,noEvent,noStyle
  ,VALUE("First",1,doNothing,noEvent)
  ,VALUE("Second",2,doNothing,noEvent)
  ,VALUE("Third",3,doNothing,noEvent)
  ,VALUE("Last",-1,doNothing,noEvent)
);

bool wirelessState = true;
CHOOSE(wirelessState, wirelessMenu, "WiFi", doNothing, noEvent, noStyle
  ,VALUE("Disabled",false,doNothing,noEvent)
  ,VALUE("Enabled",true,doNothing,noEvent)
);

int flipDuration=5;//some variable used by your code (not necessarily an int)
CHOOSE(flipDuration,flipDurationMenu,"Flip time",doNothing,noEvent,noStyle
  ,VALUE("5 ms",5,doNothing,noEvent)
  ,VALUE("50 ms",50,doNothing,noEvent)
  ,VALUE("100 ms",100,doNothing,noEvent)
  ,VALUE("500 ms",500,doNothing,noEvent)
);

MENU(configMenu,"Config",doNothing,noEvent,noStyle
  ,SUBMENU(wirelessMenu)
  ,SUBMENU(flipDurationMenu)
  ,OP("Clock",doNothing,enterEvent)
);

MENU(mainMenu, "Launcher", Menu::doNothing, Menu::noEvent, Menu::wrapStyle
  ,SUBMENU(chooseMenu)
  ,SUBMENU(configMenu)
);

serialIn serial(Serial);
MENU_INPUTS(in, &serial);

MENU_OUTPUTS(out,MAX_DEPTH
  ,SERIAL_OUT(Serial)
  ,NONE//must have 2 items at least
);

NAVROOT(nav,mainMenu,MAX_DEPTH,in,out);

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
      canvas->setFont(&Font4x5Fixed);
      canvas->setCursor(27, 6);
      canvas->print("AM");
    }
};

clockFace clockface;

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

class horizontalScroller;

void renderToDisplay();

class verticalScroller {
  private:
    navNode* node;
    horizontalScroller* parentScroller;

    struct {
      int startingPos = 0;
      int targetPos=-1;
    } animation;
  public:
    GFXcanvas1 evenBuffer;
    GFXcanvas1 oddBuffer;
    int position;
    int evenBufferPos;
    int oddBufferPos;


TaskHandle_t animatorHandle;

    static void verticalAnimator(void* pvParameters) {
      verticalScroller* scroller = (verticalScroller*)pvParameters;
      bool animating = false;
      while (true) { // TODO optimise lol
      Serial.println("Animator loop");
        if (scroller->position < scroller->animation.targetPos) {
          scroller->position ++;
          animating = true;
          if (scroller->position % SCROLL_CANVAS_HEIGHT == 1) {
            if ((scroller->position / SCROLL_CANVAS_HEIGHT) % 2) {
              scroller->drawBuffer(&(scroller->evenBuffer), (scroller->position / SCROLL_CANVAS_HEIGHT) + 1);
              scroller->evenBufferPos = (scroller->position / SCROLL_CANVAS_HEIGHT) + 1;
            }
            else {
              scroller->drawBuffer(&(scroller->oddBuffer), (scroller->position / SCROLL_CANVAS_HEIGHT) + 1);
              scroller->oddBufferPos = (scroller->position / SCROLL_CANVAS_HEIGHT) + 1;
            }
          }
        }
        else if (scroller->position > scroller->animation.targetPos) {
          scroller->position --;
          animating = true;
          if (scroller->position % SCROLL_CANVAS_HEIGHT == 7) {
            if ((scroller->position / SCROLL_CANVAS_HEIGHT) % 2) {
              scroller->drawBuffer(&(scroller->oddBuffer), (scroller->position / SCROLL_CANVAS_HEIGHT));
              scroller->oddBufferPos = (scroller->position / SCROLL_CANVAS_HEIGHT);
            }
            else {
              scroller->drawBuffer(&(scroller->evenBuffer), (scroller->position / SCROLL_CANVAS_HEIGHT) );
              scroller->evenBufferPos = (scroller->position / SCROLL_CANVAS_HEIGHT);
            }
          }
        }
        else {
          if (animating) {
            animating = false;
            scroller->animation.startingPos=scroller->position;
            renderToDisplay();
            Serial.println("Animation complete");
          }
        }

        if (animating) {
          scroller->updatetest();
          float remainingDistance = abs(scroller->animation.targetPos - scroller->position);
          delayMicroseconds(constrain(50000.0/remainingDistance, 100, 5000000));
        }
        else {
          vTaskDelay(10/portTICK_PERIOD_MS );
        }
      }
    }

    verticalScroller(navNode *nodePointer, horizontalScroller *parentPointer)
    : evenBuffer(SCROLL_CANVAS_WIDTH, SCROLL_CANVAS_HEIGHT)
    , oddBuffer(SCROLL_CANVAS_WIDTH, SCROLL_CANVAS_HEIGHT)
      {

      Serial.println("New buffer object");
      node = nodePointer;
      parentScroller = parentPointer;
      position = (node->sel + HOME_ROWS) * SCROLL_CANVAS_HEIGHT;

      evenBuffer.setFont(&Font5x7Fixed);
      evenBuffer.setCursor(0, 7);
      evenBuffer.print("even");

      oddBuffer.setFont(&Font5x7Fixed);
      oddBuffer.setCursor(0, 7);
      oddBuffer.print("odd");

      xTaskCreatePinnedToCore (
        verticalAnimator,
        "verticalAnimator",
        1000,
        this,
        1,
        &animatorHandle,
        0
      );
    }

    ~verticalScroller() {
      Serial.println("destroying scroller");
      vTaskDelete(animatorHandle);
    }

    void verticalAnimation(int toY) {
      animation.targetPos = toY;
    }

    void drawBuffer(GFXcanvas1* buffer, int row) {
      if (row < HOME_ROWS) {
        clockface.drawToCancvas(buffer);
      }
      else {
        buffer->setFont(&Font4x5Fixed);
        buffer->fillScreen(false);
        buffer->setCursor(1, 5);
        buffer->print(node->data()  [row-HOME_ROWS]->getText());
      }
    }

    void goTo (int row) {
      if (row * SCROLL_CANVAS_HEIGHT == animation.targetPos) {
        return;
      }
      verticalAnimation(row * SCROLL_CANVAS_HEIGHT);
    }

    void updatetest();

    void updatePos() {
      goTo(node->sel + 1);
    }

    bool getPixel(int pixelX, int pixelY) {
      if ((position + pixelY) >= (evenBufferPos*SCROLL_CANVAS_HEIGHT) && (position + pixelY) < ((evenBufferPos + 1)*SCROLL_CANVAS_HEIGHT)) {
        return evenBuffer.getPixel(pixelX, ((position + pixelY) - (evenBufferPos*SCROLL_CANVAS_HEIGHT)));
      }
      else {
        return oddBuffer.getPixel(pixelX, ((position + pixelY) - (oddBufferPos*SCROLL_CANVAS_HEIGHT)));
      }
    }
};

class horizontalScroller {
  private:
  navRoot* root;

    struct {
      int startingPos = 0;
      int targetPos=0;
    } animation;

  public:
    verticalScroller evenScroller;
    verticalScroller oddScroller;

    int position;
    int evenScrollerPos;
    int oddScrollerPos;
    bool animationCompleted = true;


    static void horizontalAnimator(void* pvParameters) {
      horizontalScroller* scroller = (horizontalScroller*)pvParameters;
      scroller->animationCompleted = false;
      while (true) { // TODO optimise lol
        if (scroller->position < scroller->animation.targetPos) {
          scroller->position ++;
          if (scroller->position % SCROLL_CANVAS_WIDTH == 1) {
            if ((scroller->position / SCROLL_CANVAS_WIDTH) % 2) {
              //scroller->drawBuffer(&(scroller->evenBuffer), (scroller->position / SCROLL_CANVAS_WIDTH) + 1); //LOAD NEST SCROLLER
              scroller->evenScrollerPos = (scroller->position / SCROLL_CANVAS_WIDTH) + 1;
            }
            else {
              //scroller->drawBuffer(&(scroller->oddBuffer), (scroller->position / SCROLL_CANVAS_WIDTH) + 1); //LOAD NEST SCROLLER
              scroller->oddScrollerPos = (scroller->position / SCROLL_CANVAS_WIDTH) + 1;
              Serial.println("Update odd buffer");
              scroller->updateScroller(false);
            }
          }
          
        }
        else if (scroller->position > scroller->animation.targetPos) {
          scroller->position --;
          if (scroller->position % SCROLL_CANVAS_WIDTH == SCROLL_CANVAS_WIDTH-1) {
            if ((scroller->position / SCROLL_CANVAS_WIDTH) % 2) {
              //scroller->drawBuffer(&(scroller->oddBuffer), (scroller->position / SCROLL_CANVAS_WIDTH)); //LOAD PREV SCROLLER
              scroller->oddScrollerPos = (scroller->position / SCROLL_CANVAS_WIDTH);
            }
            else {
              //scroller->drawBuffer(&(scroller->evenBuffer), (scroller->position / SCROLL_CANVAS_WIDTH) );//LOAD PREV SCROLLER
              scroller->evenScrollerPos = (scroller->position / SCROLL_CANVAS_WIDTH);
            }
          }
        }
        else {
          break;
        }

        scroller->updatetest();

        float remainingDistance = abs(scroller->animation.targetPos - scroller->position);

        if (scroller->animation.targetPos - scroller->position == 0) {
          scroller->animationCompleted  = true;
        }
        else {
          delayMicroseconds(constrain(100000.0/remainingDistance, 0, 5000000));
        }
      }
      vTaskDelete(NULL);
    }

    horizontalScroller(navRoot *rootPointer): evenScroller(&nav.path[0], this), oddScroller(&nav.path[1], this) { //TODO pass this to scrollers for later callback function
      root = rootPointer;
      evenScrollerPos = 0;
      oddScrollerPos = 1;
      evenScroller = verticalScroller(&nav.path[0], this);
    }

    void updateScroller(bool even = true) {
      if (even) {
        //evenScroller = verticalScroller(&nav.path[evenScrollerPos], this);
      }
      else { 
        oddScroller = verticalScroller(&nav.path[oddScrollerPos], this);
      }
    }

    void onScrollerUpdate() {
      if (animationCompleted) {
        renderToDisplay();
      }
    }

    void updatetest() {
      renderToDisplay();
    }

    void horizontalAnimation(int toX) {
      animation.targetPos = toX;
      animation.startingPos = position;
      xTaskCreatePinnedToCore (
        horizontalAnimator,
        "horizontalAnimator",
        1000,
        this,
        1,
        NULL,
        0
      );
    }

    void goTo (int col) {
      if (col * SCROLL_CANVAS_WIDTH == animation.targetPos) {
        return;
      }
      horizontalAnimation(col * SCROLL_CANVAS_WIDTH);
    }

    void updatePos() {
      goTo(root->level);
      evenScroller.updatePos();
      oddScroller.updatePos();
    }

    bool getPixel(int pixelX, int pixelY) {
      if ((position + pixelX) >= (evenScrollerPos*SCROLL_CANVAS_WIDTH) && (position + pixelX) < ((evenScrollerPos + 1)*SCROLL_CANVAS_WIDTH)) {
        return evenScroller.getPixel(((position + pixelX) - (evenScrollerPos*SCROLL_CANVAS_WIDTH)), pixelY);
      }
      else {
        return oddScroller.getPixel(((position + pixelX) - (oddScrollerPos*SCROLL_CANVAS_WIDTH)), pixelY);
      }
    }

};

void verticalScroller::updatetest() {
  parentScroller->onScrollerUpdate();
}

horizontalScroller testScroller(&nav);

void renderToDisplay() {
  for (int FBY = 0; FBY < 8; FBY ++) {
    for (int FBX = 0; FBX < 36; FBX ++) {
      frameBuffer.writePixel(FBX, FBY, testScroller.getPixel(FBX, FBY));
    }
  }
  updateDisplay();
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

/*
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
*/

bool suspended = false;

result idle(menuOut &o, idleEvent e) {
  switch(e) {
    case idleStart:
      Serial.println("suspending menu!");
      suspended = true;
      break;
    case idleEnd:
      Serial.println("resuming menu.");
      suspended = false;
      break;
    default:break;
  }
  return proceed;
}

void setup() {
  pinMode(5, OUTPUT);

  for (int i = 0; i < 8; i++) {
    shift32(0x00); 
  }

  SPI.begin();
  Serial.begin(112500);

  nav.idleTask=idle;

  while(!Serial);
  Serial.println("Menu 4.x");
  Serial.println("Use keys + - * /");
  Serial.println("to control the menu navigation");

  nav.inputBurst=1;

  renderToDisplay();
}


void loop() {
  /*
  */
  nav.poll();
  if (!suspended){
    testScroller.updatePos();
  } else {
    testScroller.goTo(0);
    testScroller.evenScroller.goTo(0);
  }
  //Serial.println(nav.node().target[0][0].getText());
  //Serial.println(nav.path[nav.level].sel);
}