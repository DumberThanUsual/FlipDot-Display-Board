#include <Arduino.h>

#include <vector>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>

#include <time.h>
#include "time_zones.h"

#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "Font5x7Fixed.h"
#include "Font4x5Fixed.h"
#include "Font4x7Fixed.h"
#include "Font3x5FixedNum.h"

//OLED SIPLAY TESTAA
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include "rtc.h"

Adafruit_SSD1306 oled(128, 32, &Wire, -1);

#define MODULES 8

#define MODULE_WIDTH 5
#define MODULE_HEIGHT 7

#define DISPLAY_WIDTH MODULES*MODULE_WIDTH
#define DISPLAY_HEIGHT MODULE_HEIGHT

#define INPUT_UP 32
#define INPUT_DOWN 27
#define INPUT_LEFT 33
#define INPUT_RIGHT 25
#define INPUT_CENTER 26

#define SCROLLER_MAX_LENGTH 7

bool fullRedraw = true; //TODO = build into display tree

class TimeService {
  
  public:
    TimeService() {
    }

    static void cbSyncTime(struct timeval *tv)  { // callback function to show when NTP was synchronized
      Serial.println(F("NTP time synched"));
    }

    void begin() {
      configTime(0, 0, "pool.ntp.org");
    }

    bool syncNetworkTime() {
      struct tm timeinfo;
      if (WiFi.status() == WL_CONNECTED) {
        if(!getLocalTime(&timeinfo)){
          return false;
        }
        
      }
      else {
        return false;
      }
    }

    bool syncFromRTC() {
      return false;
    }
};

TimeService timeService;

enum InputEventType {
    UP_SINGLE
  , DOWN_SINGLE
  , LEFT_SINGLE
  , RIGHT_SINGLE
  , CENTER_SINGLE
};

class BufferProducer {

  bool bufferValid = false;

  public:

  bool visibility = false;
  bool focus = false;

    BufferProducer* getProducer() {
      return this;
    }

    void invalidateBuffer() {
      bufferValid = false;
    }

    bool getBufferValidity() {
      return bufferValid;
    }

    virtual bool getPixel(int, int) = 0;

    virtual bool ensureBufferValidity(bool) = 0;

    virtual bool handleInput(InputEventType) = 0;

    void visible () {
      if (!visibility) {
        enterVisibility();
      }
      visibility = true;
    }

    void invisible() {
      if (visibility) {
        exitVisibility();
      }
      visibility = false;
    }

    void focussed() {
      if (!focus) {
        enterFocus();
      }
      focus = true;
      visibility = true;
    }

    void unfocussed() {
      if (focus) {
        exitFocus();
      }
      focus = false;
      visibility = false;
    }

    virtual void enterVisibility() {};

    virtual void enterFocus() {};

    virtual void exitVisibility() {};

    virtual void exitFocus() {};
};

class BufferConsumer {
  BufferProducer* bufferProducer;

  public:
    BufferConsumer() {

    }

    bool getPixel(int x, int y) {
      if (bufferProducer) {
        return bufferProducer->getPixel(x, y);
      }
      else {
        return false;
      }
    }

    void bindToProducer(BufferProducer* buffer) {
      bufferProducer = buffer;
    }

    void releaseProducer() {
      // TODO:tell producer
      bufferProducer = nullptr;
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      if(bufferProducer) {
        return bufferProducer->ensureBufferValidity(includeInacive);
      }
      return false;
    }

    bool handleInput(InputEventType inputEventType) {
      if (bufferProducer) {
        return bufferProducer->handleInput(inputEventType);
      }
      else {
        return false;
      }
    }

    void visible() {
      if (bufferProducer) {
        bufferProducer->visible();
      }
    }

    void focussed() {
      if (bufferProducer) {
        bufferProducer->focussed();
      }
    }

    void invisible() {
      if (bufferProducer) {
        bufferProducer->invisible();
      }
    }

    void unfocussed() {
      if (bufferProducer) {
        bufferProducer->unfocussed();
      }
    }
};

class ClockFace: public BufferProducer {
  public:
  GFXcanvas1 buffer;

  //testshit
  struct tm timeinfo;
  bool timeValid = false;
  TaskHandle_t timekeepingTask;

  public:
    ClockFace()
    : buffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      buffer.setFont(&Font5x7Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(0, 7);
      buffer.print("--:--");
      buffer.setFont(&Font4x5Fixed);
      buffer.setCursor(27, 6);
      buffer.print("-M");
      xTaskCreatePinnedToCore (
        TKT,
        "Timekeeping task",
        1000,
        this,
        0,
        &timekeepingTask,
        1
      );
    }

    bool getPixel(int x, int y) {
      return buffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      if(!getBufferValidity()) {
        if(!timeValid){
          buffer.setFont(&Font5x7Fixed);
          buffer.fillScreen(false);
          buffer.setCursor(0, 7);
          buffer.print("--:--");
          buffer.setFont(&Font4x5Fixed);
          buffer.setCursor(27, 6);
          buffer.print("-M");
        } else {
          buffer.setFont(&Font5x7Fixed);
          buffer.fillScreen(false);
          buffer.setCursor(0, 7);
          buffer.print(&timeinfo, "%I:%M");
          buffer.setFont(&Font4x5Fixed);
          buffer.setCursor(27, 6);
          buffer.print(&timeinfo, "%p");
        }
      }
      return true;
    }
    static void TKT(void* pvParameters) {
      ClockFace* clockFace = (ClockFace*)pvParameters;
      for(;;) {
        if(WiFi.status() == WL_CONNECTED) {
          if(getLocalTime(&clockFace->timeinfo)) {
            clockFace->timeValid = true;
            clockFace->invalidateBuffer();
          }
          else {
            clockFace->timeValid = false;
          }
        }
        else {
          clockFace->timeValid = false;
        }
        vTaskDelay(10000/portTICK_PERIOD_MS);
      }
      vTaskDelete( NULL );
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }
};

int surfaceNumber = 0;

class StaticBuffer: public BufferProducer {
  GFXcanvas1 buffer;

  public:
    StaticBuffer(String surfaceText = "")
    : buffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      if (surfaceText == "") {
      buffer.print("Surface ");
      buffer.print(surfaceNumber);
      }
      else {
        buffer.print(surfaceText);
      }
      surfaceNumber++;
    }

    void setText(String surfaceText) {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      buffer.print(surfaceText);
    }

    bool getPixel(int x, int y) {
      return buffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }
};

class TextSurface: public BufferProducer {
  GFXcanvas1 textBuffer;
  
  public:
    TextSurface(String surfaceText = "")
    : textBuffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      textBuffer.setFont(&Font4x5Fixed);
      textBuffer.fillScreen(false);
      textBuffer.setCursor(1, 5);
      if (surfaceText == "") {
      textBuffer.print("Surface ");
      textBuffer.print(surfaceNumber);
      }
      else {
        textBuffer.print(surfaceText);
      }
      surfaceNumber++;
    }

    void setText(String surfaceText) {
      textBuffer.setFont(&Font4x5Fixed);
      textBuffer.fillScreen(false);
      textBuffer.setCursor(1, 5);
      textBuffer.print(surfaceText);
    }

    bool getPixel(int x, int y) {
      return textBuffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }
};

class TestCountingBuffer: public BufferProducer {
  TaskHandle_t countingTask;

  GFXcanvas1 buffer; 

  int counter = 0;

  public:
    TestCountingBuffer()
    : buffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      xTaskCreatePinnedToCore (
        countTask,
        "Counting buffer task",
        1000,
        this,
        1,
        &countingTask,
        1
      );
    }

    bool getPixel(int x, int y) {
      return buffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      if (!getBufferValidity()) {
        buffer.setFont(&Font4x5Fixed);
        buffer.fillScreen(false);
        buffer.setCursor(1, 5);
        buffer.print(counter);
      }
      return true;
    }
    
    static void countTask(void* pvParameters) {
      TestCountingBuffer* countingbuffer = (TestCountingBuffer*)pvParameters;
      for(;;) {
        vTaskDelay(100/portTICK_PERIOD_MS );
        countingbuffer->counter ++;
        countingbuffer->invalidateBuffer();
      }
    }

    bool handleInput(InputEventType inputEventType) {
      if (inputEventType == CENTER_SINGLE) {
        counter = 0;
        invalidateBuffer();
        return true;
      }
    return false;
    }
};

class SurfaceScroller: public BufferProducer {
  public:
    BufferProducer* scrollSurfaces[SCROLLER_MAX_LENGTH];

    BufferConsumer evenSurface;
    BufferConsumer oddSurface;

    int evenSurfacePosition = 0;
    int oddSurfacePosition = 1;

    bool vertical;
    int scrollLength;

    int pixelPosition = 0;
    int targetPixelPosition = 0;

    TaskHandle_t animatorTask;

    SurfaceScroller(BufferProducer* surfaces[SCROLLER_MAX_LENGTH] = {nullptr}, bool isVertical = true) 
    : vertical(isVertical) 
    {
      for (int i = 0; i < SCROLLER_MAX_LENGTH; i++) { // TODO: use macro to set array size
        scrollSurfaces[i] = surfaces[i];
      }
      if (vertical) {
        scrollLength = DISPLAY_HEIGHT;
      }
      else {
        scrollLength = DISPLAY_WIDTH;
      }

      evenSurface.bindToProducer(*scrollSurfaces);
      oddSurface.bindToProducer(*(scrollSurfaces + 1));

      xTaskCreatePinnedToCore (
        scrollerAnimator,
        "scrollerAnimatedTask",
        1000,
        this,
        1,
        &animatorTask,
        1
      );
    }

    void rebuildFrame(int pos) {
      if (pos % 2) {
        oddSurface.invisible();
        oddSurface.bindToProducer(scrollSurfaces[pos]);
        oddSurfacePosition = pos;
        if (visibility) {
          oddSurface.visible();
        }
      }
      else {
        evenSurface.invisible();
        evenSurface.bindToProducer(scrollSurfaces[pos]);
        evenSurfacePosition = pos;
        if (visibility) {
          evenSurface.visible();
        }
      }
    }

    BufferConsumer* getActiveConsumer() {
      if ((targetPixelPosition/scrollLength) % 2){
        return &oddSurface;
      }
      else {
        return &evenSurface;
      }
    }

    BufferConsumer* getInactiveConsumer() {
      if ((targetPixelPosition/scrollLength) % 2){
        return &evenSurface;
      }
      else {
        return &oddSurface;
      }
    }

    static void scrollerAnimator (void* pvParameters) {
      SurfaceScroller* scroller = (SurfaceScroller*)pvParameters;
      bool animating = false;
      for (;;) {
        if (scroller->pixelPosition < scroller->targetPixelPosition) {

          if (scroller->pixelPosition % scroller->scrollLength == 0) {
            scroller->getInactiveConsumer()->unfocussed();
          }

          scroller->pixelPosition ++;
          animating = true;
          if (scroller->pixelPosition % scroller->scrollLength == 1) {
            scroller->rebuildFrame((scroller->pixelPosition / scroller->scrollLength) + 1);
          }
        }
        else if (scroller->pixelPosition > scroller->targetPixelPosition) {

          if (scroller->pixelPosition % scroller->scrollLength == 0) {
            scroller->getInactiveConsumer()->unfocussed();
          }

          scroller->pixelPosition --;
          animating = true;
          if (scroller->pixelPosition % scroller->scrollLength == scroller->scrollLength - 1) {
            scroller->rebuildFrame((scroller->pixelPosition / scroller->scrollLength));
          }
        }
        if (scroller->targetPixelPosition == scroller->pixelPosition) {
            if (animating) {
              animating = false;
              fullRedraw = true;
              scroller->getActiveConsumer()->focussed();
              scroller->getInactiveConsumer()->invisible();
          }
        }
        if (animating) {
          scroller->invalidateBuffer();
          float remainingDistance = abs(scroller->targetPixelPosition - scroller->pixelPosition);
          vTaskDelay(constrain(100.0/pow(remainingDistance, 1), 10, 200)/portTICK_PERIOD_MS);
        }
        else {
          vTaskDelay(10/portTICK_PERIOD_MS );
        }
      }
    }

    void setPosition(int toPosition) {
      rebuildFrame(toPosition);
      pixelPosition = toPosition * DISPLAY_HEIGHT;
      targetPixelPosition = pixelPosition;
    }

    bool getPixel(int x, int y) {
      int* positionOffset;
      if (vertical) {
        positionOffset = &y;
      }
      else {
        positionOffset = &x;
      }

      if ((pixelPosition + *positionOffset) >= (evenSurfacePosition*scrollLength) && (pixelPosition + *positionOffset) < ((evenSurfacePosition + 1)*scrollLength)) {
        if (vertical) {
          return evenSurface.getPixel(x, ((pixelPosition + y) - (evenSurfacePosition*DISPLAY_HEIGHT)));
        }
        else {
          return evenSurface.getPixel(((pixelPosition + x) - (evenSurfacePosition*DISPLAY_WIDTH)), y);
        }
      }
      else {
        if (vertical) {
          return oddSurface.getPixel(x, ((pixelPosition + y) - (oddSurfacePosition*DISPLAY_HEIGHT)));
        }
        else {
          return oddSurface.getPixel(((pixelPosition + x) - (oddSurfacePosition*DISPLAY_WIDTH)), y);
        }
      }
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      return evenSurface.ensureBufferValidity(includeInacive)
        &&  oddSurface.ensureBufferValidity(includeInacive);
    }

    bool handleInput(InputEventType inputEventType) {
      if (getActiveConsumer()->handleInput(inputEventType)) {
        return true;
      }
      else {
        if (vertical){
          switch (inputEventType) {
            case UP_SINGLE:
              targetPixelPosition -= DISPLAY_HEIGHT;
              break;
            case DOWN_SINGLE:
              if (scrollSurfaces[(targetPixelPosition / DISPLAY_HEIGHT) + 1]) {
                targetPixelPosition += DISPLAY_HEIGHT;
              }
              break;
            default:
              return false;
              break;
          }
          targetPixelPosition = constrain(targetPixelPosition, 0, DISPLAY_HEIGHT * 7);
        }
        else {
          switch (inputEventType) {
            case LEFT_SINGLE:
              targetPixelPosition += DISPLAY_WIDTH;
              break;
            case RIGHT_SINGLE:
              if (scrollSurfaces[(targetPixelPosition / DISPLAY_WIDTH) + 1]) {
                targetPixelPosition -= DISPLAY_WIDTH;
              }
              break;
            default:
              return false;
              break;
          }
          targetPixelPosition = constrain(targetPixelPosition, 0, DISPLAY_WIDTH * 7);
        }
      }
      return true;
    }

    void enterVisibility () {
      evenSurface.visible();
      oddSurface.visible();
    }

    void enterFocus () {
      if (pixelPosition % scrollLength == 0) {
        getActiveConsumer()->focussed();
      }
    }

    void exitFocus() {
      for (int i = 0; i < SCROLLER_MAX_LENGTH; i++) {
        if (scrollSurfaces[i]) {
          scrollSurfaces[i]->unfocussed();
        }
      }
    }

    void exitVisibility() {
      for (int i = 0; i < SCROLLER_MAX_LENGTH; i++) {
        if (scrollSurfaces[i]) {
          scrollSurfaces[i]->invisible();
        }
      }
    }
};

class SubMenu: public SurfaceScroller {
  public:
    
    BufferProducer* itemCovers[SCROLLER_MAX_LENGTH];
    BufferProducer* itemContents[SCROLLER_MAX_LENGTH];

    SurfaceScroller menuSelector;

    BufferProducer* nullptrArray[SCROLLER_MAX_LENGTH] = {nullptr};

    int activeContentIndex = 0;

    SubMenu(BufferProducer* menuItems[SCROLLER_MAX_LENGTH][2]) 
    : SurfaceScroller(nullptrArray, false)
    , menuSelector(nullptrArray)
    {
      for (int i = 0; i < SCROLLER_MAX_LENGTH; i ++) {
        itemCovers[i] = menuItems[i][0];
        itemContents[i] = menuItems[i][1];
        menuSelector.scrollSurfaces[i] = itemCovers[i];
      }
      scrollSurfaces[0] = menuSelector.getProducer();
      rebuildFrame(0);
      menuSelector.rebuildFrame(0);
    }

    bool handleInput(InputEventType inputEventType) {
      if (targetPixelPosition != 0) {
        if (itemContents[activeContentIndex]->handleInput(inputEventType)){
          return true;
        }
        else {
          switch(inputEventType) {
            case RIGHT_SINGLE:
              targetPixelPosition = 0;
              return true;
            default:
              return false;
          }
        }
      }
      else {
        switch(inputEventType) {
          case LEFT_SINGLE:
            activeContentIndex = menuSelector.targetPixelPosition / DISPLAY_HEIGHT;
            if (itemContents[activeContentIndex]) {
              scrollSurfaces[1] = itemContents[activeContentIndex];
              rebuildFrame(1);
              targetPixelPosition = DISPLAY_WIDTH;
              return true;
            }
            else {
              return false;
            }
          default:
            return menuSelector.handleInput(inputEventType);
        }
      }
    }
};

class Launcher: public SubMenu {
  public:
    Launcher(BufferProducer* menuItems[SCROLLER_MAX_LENGTH][2])
    : SubMenu(menuItems)
    {
      visible();
    }

    bool handleInput(InputEventType inputEventType) {
      if (targetPixelPosition != 0) {
        if (itemContents[activeContentIndex]->handleInput(inputEventType)){
          return true;
        }
        else if (inputEventType == RIGHT_SINGLE) {
            targetPixelPosition = 0;
            menuSelector.setPosition(0);
            return true;
        }
      }
      else {
        if (inputEventType == RIGHT_SINGLE) {
          menuSelector.targetPixelPosition = 0;
          return true;
        }
      }

      return SubMenu::handleInput(inputEventType);
    }
};

class FocusTester: public BufferProducer {
  GFXcanvas1 buffer;
  public:
    FocusTester()
    : buffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {

    }
    
    bool getPixel(int x, int y) {
      return buffer.getPixel(x, y);
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      return true;
    }

    void enterVisibility() {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      buffer.print("Visible");
    }

    void enterFocus() {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      buffer.print("Focussed");
    }

    void exitFocus() {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      buffer.print("Unfocussed");
    }

    void exitVisibility() {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      buffer.print("inVisible");
    }
};

class FlipDisplay {

  GFXcanvas1 stateBuffer;

  TaskHandle_t renderTask;

  public:

   BufferConsumer frameBuffer;

    FlipDisplay()
    : frameBuffer()
    , stateBuffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      
    }

    void begin() {
      xTaskCreatePinnedToCore (
        renderer,
        "Flip Display Renderer",
        1000,
        this,
        1,
        &renderTask,
        1
      );
    }

    /*
    On display update call:
    - Check validity of required producers
    - Redraw branches with invalid producers, ending in framebuffer consumer
    - Draw framebuffer to display
    */ 

    void updateDisplay(bool fullRedraw = false) {  //TODO: replace enture display update functionality
      frameBuffer.ensureBufferValidity();

      for (int module = 0; module < MODULES; module ++) {
        Serial2.write(0b10000000 | (module << 4));
        for (int x = 0; x < MODULE_WIDTH; x ++) {
          uint8_t colBuf = 0;
          for (int y = 0; y < 7; y ++) {
            colBuf |= frameBuffer.getPixel(x + module * 5, y) << y;
          }
          Serial2.write(colBuf);
        }
        if (fullRedraw) {
          Serial2.write(0b10000110 | (module << 4));
        }
        else {
          Serial2.write(0b10000101 | (module << 4));
        }
      }
    }

    static void renderer(void* pvParameters) {
      FlipDisplay* flipDisplay = (FlipDisplay*)pvParameters;
      for (;;){
        flipDisplay->updateDisplay(fullRedraw);
        fullRedraw = false;

        for (int x = 0; x < 40; x ++) {
          for (int y = 0; y < 7; y ++) {
            oled.drawPixel(x*3, y*3+1, flipDisplay->frameBuffer.getPixel(x, y));
            oled.drawPixel(x*3+1, y*3, flipDisplay->frameBuffer.getPixel(x, y));
            oled.drawPixel(x*3+1, y*3+1, flipDisplay->frameBuffer.getPixel(x, y));
            oled.drawPixel(x*3+1, y*3+2, flipDisplay->frameBuffer.getPixel(x, y));
            oled.drawPixel(x*3+2, y*3+1, flipDisplay->frameBuffer.getPixel(x, y));
          }
        }


        vTaskDelay(16/portTICK_PERIOD_MS ); // 60 FPS: 16ms/frame
      }
    }

    void handleInput(InputEventType InputEventType) {
      frameBuffer.handleInput(InputEventType);
    }
};

#define MAX_SCROLLER_SIZE 10

class SurfaceScrollerImproved: public BufferProducer {
  private:
    
    TaskHandle_t animatorTask;

    StaticBuffer emptyBuffer;

    BufferProducer* activeBuffer;
    BufferProducer* inactiveBuffer;

    int scrollDistance = 0;

  public:

    struct ScrollInstruction {
      BufferProducer* buffer;
      int distance;
      bool direction;
    };

  private:

    std::vector<ScrollInstruction> instructionBuffer;

    int getRemainingDistance() {
      int remainingDistance = 0;
      bool scrollDirection = instructionBuffer.front().direction;

      int searchIndex = 0;

      while(searchIndex < instructionBuffer.size()) {
        remainingDistance += instructionBuffer[searchIndex].distance;
        searchIndex ++;
        if (instructionBuffer[searchIndex].direction != scrollDirection) {
          break;
        }
      }
      
      return remainingDistance;
    }

    int offset = 0;
    bool vertical;

    void setFrame(BufferProducer* buffer) {
      if (inactiveBuffer) {
        inactiveBuffer->exitFocus();
        inactiveBuffer->exitVisibility();
      }

      if (activeBuffer) {
        activeBuffer->exitFocus();
        activeBuffer->exitVisibility();
      }
      activeBuffer = buffer;

      activeBuffer->enterVisibility();
      activeBuffer->enterFocus();

      offset = 0;
      instructionBuffer.clear();
    }

    static void scrollerAnimator (void* pvParameters) {
      SurfaceScrollerImproved* scroller = (SurfaceScrollerImproved*)pvParameters;
      for(;;) {   
        bool animating = false;

        if (scroller->instructionBuffer.size() > 0 && scroller->instructionBuffer[0].buffer) {
          animating = true;
          bool instructionComplete = false;
          scroller->offset = 0;
          int remainingDistance = scroller->getRemainingDistance();

          ScrollInstruction workingScrollInstruction = scroller->instructionBuffer[0];
          scroller->inactiveBuffer = workingScrollInstruction.buffer;
          scroller->inactiveBuffer->enterVisibility();
          if(!animating){
            scroller->activeBuffer->exitFocus();
          }
          while(!instructionComplete) {
            if(workingScrollInstruction.direction) {
              scroller->offset ++;
            }
            else {
              scroller->offset --;
            }
            remainingDistance = scroller->getRemainingDistance();

            vTaskDelay(constrain(100.0/pow(remainingDistance, 1), 50, 200)/portTICK_PERIOD_MS);

            if(abs(scroller->offset) == workingScrollInstruction.distance) {

              scroller->activeBuffer->exitVisibility();
              scroller->activeBuffer = workingScrollInstruction.buffer;
              if(remainingDistance == 0) {
                scroller->activeBuffer->enterFocus();
              }

              scroller->inactiveBuffer = &(scroller->emptyBuffer);

              instructionComplete = true;

            }
          }
          scroller->instructionBuffer.erase(scroller->instructionBuffer.begin());
          animating = false;
        }
        else {
          vTaskDelay(10/portTICK_PERIOD_MS );
        }
      }
    }

  public:

    SurfaceScrollerImproved(bool isVertical = true) 
    : vertical(isVertical)
    , emptyBuffer("Inactive")
    {
      activeBuffer = &emptyBuffer;
      setFrame(&emptyBuffer);
      inactiveBuffer = &emptyBuffer;
      xTaskCreatePinnedToCore (
        scrollerAnimator,
        "scrollerAnimatedTask",
        10000,
        this,
        1,
        &animatorTask,
        1
      );
    }

    bool getPixel(int x, int y) {
      if(instructionBuffer.size() == 0) {
        return activeBuffer->getPixel(x, y);
      }

      int* positionOffset;
      if (vertical) {
        positionOffset = &y;
      }
      else {
        positionOffset = &x;
      }

      if ((offset + *positionOffset) >= 0 && (offset + *positionOffset) < instructionBuffer[0].distance) {
        if (vertical) {
          return activeBuffer->getPixel(x, (offset + y));
        }
        else {
          return activeBuffer->getPixel((offset + x), y);
        }
      }
      else {
        if (vertical) {
          if (offset >= 0) {
            return inactiveBuffer->getPixel(x, (offset + y)-instructionBuffer[0].distance);
          }
          else {
            return inactiveBuffer->getPixel(x, (offset + y)+instructionBuffer[0].distance);
          }
        }
        else {
          return inactiveBuffer->getPixel((offset + x), y);
        }
      }
    }

    bool ensureBufferValidity(bool includeInacive = false) {
      bool activeBufferResult = true;
      bool inactiveBufferResult = true;
      if (activeBuffer) {
        activeBufferResult = activeBuffer->ensureBufferValidity(includeInacive);
      }
      if (inactiveBuffer) {
        inactiveBufferResult = inactiveBuffer->ensureBufferValidity(includeInacive);
      }
      return activeBufferResult && inactiveBufferResult;  
    }

    void enterVisibility () {
      if(instructionBuffer.size() > 0) {
        inactiveBuffer->enterVisibility();
      }
      activeBuffer->enterVisibility();
    }

    void enterFocus () {
      if(instructionBuffer.size() == 0) {
        activeBuffer->enterVisibility();
      }
    }

    void exitFocus() {
      if(instructionBuffer.size() == 0) {
        activeBuffer->exitFocus();
      }
    }

    void exitVisibility() {
      if(instructionBuffer.size() > 0) {
        inactiveBuffer->exitVisibility();
      }
      activeBuffer->exitVisibility();
    }

    void addScrollInstrction(ScrollInstruction nextScrollInstruction) {
      instructionBuffer.push_back(nextScrollInstruction);
    }

    bool handleInput(InputEventType inputEventType) {
      return activeBuffer->handleInput(inputEventType);
    }
};

class ActivityStack;

class Application {
  public:

    class Activity: public BufferProducer {
      Application* parentApplication;
      TaskHandle_t* parentApplicationTask;
      
      public:

        Activity(Application* parentApplicationPointer) 
        : parentApplication(parentApplicationPointer)
        {
          return;
        }

        void (ActivityStack::*onFinish)(Activity* thisActivity);

        virtual bool getPixel(int x, int y) {
          return false;
        }

        virtual bool ensureBufferValidity(bool includeInacive = false);

        bool handleInput(InputEventType inputEventType) {
          return false;
        }

    };

  virtual BufferProducer* open() = 0;

};

#define MAX_ACTIVITY_STACK_SIZE

class StackSwitcher: public SurfaceScrollerImproved {
  public:
    void stackUpdate();
};

class ActivityStack {

  public:
    void(StackSwitcher::*stackUpdate)(ActivityStack* thisStack);

    std::vector<Application::Activity*> activityStack;

    ActivityStack(Application::Activity* rootActivity) {
      addActivity(rootActivity);
    }

    void finishActivity(Application::Activity* finishingActivity) {
      
    }
    
    void addActivity(Application::Activity* newActivity) {
      activityStack.push_back(newActivity);
      newActivity->onFinish = &ActivityStack::finishActivity;

    }

    Application::Activity* getTopActivity() {
      return activityStack.back();
    }
};

class StackSwitcher: public SurfaceScrollerImproved {

  ActivityStack* currentStack;
  std::vector<ActivityStack*> openStacks;

  public:
    StackSwitcher()
    : SurfaceScrollerImproved()
    {

    }

    StackSwitcher(Application::Activity* rootActivity)
    : SurfaceScrollerImproved()
    {
      createStack(rootActivity);
    }

    ActivityStack* createStack(Application::Activity* rootActivity) {
      openStacks.push_back(new ActivityStack(rootActivity));
      openStacks.back()->stackUpdate = &StackSwitcher::stackUpdate;
      return openStacks.back();
    }

    void stackUpdate(ActivityStack* thisStack) {
      if (thisStack == currentStack) {
        ScrollInstruction scrollToActivity;
        scrollToActivity.buffer = currentStack->getTopActivity();
        scrollToActivity.direction = true;
        scrollToActivity.distance = DISPLAY_HEIGHT;
        addScrollInstrction(scrollToActivity);
      }
    }

    
};

// countdown timer app:
// TODO: USE UNIX EPOCH AND NTP TIMEKEEPING LOL

class CountdownTimer: public Application {

  uint32_t timer = -1;

  TaskHandle_t countdownTaskHandle;

  void TimerSet(uint32_t seconds) {
    if (timer == -1) {
      timer = seconds;
    }
  }

  /*
  class TimerSetActivity: public Activity {
    SurfaceScrollerImproved hourSelection;
    SurfaceScrollerImproved minuteSelection;
    SurfaceScrollerImproved secondSelection;

    int scrollerSelection;


    public:
      TimerSetActivity() 
      : Activity(nullptr)
      , hourSelection(24)
      , minuteSelection(12)
      , secondSelection(12)
      {
        scrollerSelection = 0;

        
      }

      void handleInput() {

      }

  };
  */

  class CountdownActivity: public Activity {
    TextSurface countdown;
    uint32_t timer;

    CountdownTimer* parentApplication;

    public: 
      CountdownActivity(CountdownTimer* parentApplicationPointer) 
      : Activity(parentApplicationPointer)
      , parentApplication(parentApplicationPointer)
      , countdown("--:--:--")
      {

      }

      String intToFormattedStr(int number) {
        if (number > 9) {
          return String(number);
        }
        else {
          return "0" + String(number);
        }
      }

      void updateCountdown() {
        String seconds = intToFormattedStr(timer % 60);
        String minutes = intToFormattedStr((timer/60)%60);
        String hours = intToFormattedStr(timer/(60*60));

        countdown.setText(hours + ":" + minutes + ":" + seconds);
      }

      void updateTimer(uint32_t seconds) {
        timer = seconds;
        updateCountdown();
      }

      void ensureBufferValidity() {
        timer = parentApplication->timer;
        updateCountdown();
      }
  };

  static void countdownTask(void* pvParameters) {
    CountdownTimer* timer = (CountdownTimer*)pvParameters;
    while (timer->timer > 0) {
      timer -= 1;
      vTaskDelay(1000/portTICK_PERIOD_MS);
    }
  }

  public:

    enum activities {
      unspecified,
      setup,
      countdown,
      alarm
    };

    CountdownTimer() {
      xTaskCreatePinnedToCore (
        countdownTask,
        "Countdown timer task",
        1000,
        this,
        1,
        &countdownTaskHandle,
        1
      );
    }

    ~CountdownTimer() {
      vTaskDelete(countdownTaskHandle);
    }

    Activity* getDefaultActivity() {
      if (timer == -1) {
        // RETURN Setup screen
      }
      else {
        // RETURN counter
      }
    }

    BufferProducer* open() {
      return getDefaultActivity();
    }
};




FlipDisplay display;

// Test menu

StaticBuffer SBA("test 1");
FocusTester SBB;//("test 2");
StaticBuffer SBC("test 3");
StaticBuffer SBD("woo 1");
StaticBuffer SBE("woo 2");
StaticBuffer SBF("woo 3");

BufferProducer* testBuffersaa[7][2] = {
    {SBA.getProducer(), SBD.getProducer()}
  , {SBB.getProducer(), SBE.getProducer()}
  , {SBC.getProducer(), SBF.getProducer()}
};

SubMenu testMenu(testBuffersaa);
// Settings menus:

// Settings/Wifi menu

StaticBuffer settings_wifi_cover_1("WiFi 1");
StaticBuffer settings_wifi_contents_1("hiii");

BufferProducer* settings_wifi_menu_surfaces[7][2] = {
    {settings_wifi_cover_1.getProducer(), settings_wifi_contents_1.getProducer()}
};

SubMenu settings_wifi_menu(settings_wifi_menu_surfaces);

// Settings menu

StaticBuffer settings_wifi_menu_cover("WiFi");

BufferProducer* settings_menu_surfaces[7][2] = {
    {settings_wifi_menu_cover.getProducer(), settings_wifi_menu.getProducer()}
};

SubMenu settings_menu(settings_menu_surfaces);

// Launcher items: 

ClockFace SB0;
StaticBuffer SB1("item 1");
StaticBuffer SB2("test menu");
StaticBuffer SB3("Settings");
StaticBuffer SB4("contents 1");
StaticBuffer SB5("contents 2");
StaticBuffer SB6("contents 3");

BufferProducer* scrollerBuffers[7][2] = {
    {SB0.getProducer(), nullptr}
  , {SB1.getProducer(), SB4.getProducer()}
  , {SB2.getProducer(), testMenu.getProducer()}
  , {SB3.getProducer(), settings_menu.getProducer()}
};

Launcher scroller(scrollerBuffers);

StaticBuffer updateScreen("Updating");


StaticBuffer test1("test 1");
StaticBuffer test2("test 2");
SurfaceScrollerImproved testScroller;

void setup() {
  display.begin();

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay();
  oled.display();

  Serial.begin(115200);
  Serial2.begin(115200);
  //while(!Serial);
  //while(!Serial2);

  display.frameBuffer.bindToProducer(&testScroller);

  pinMode(INPUT_CENTER, INPUT_PULLUP);
  pinMode(INPUT_LEFT, INPUT_PULLUP);
  pinMode(INPUT_RIGHT, INPUT_PULLUP);
  pinMode(INPUT_UP, INPUT_PULLUP);
  pinMode(INPUT_DOWN, INPUT_PULLUP);

  delay(1000);

  /*

  WiFi.setHostname("Flipdot Display");
  ArduinoOTA.setHostname("Flipdot Display");
  WiFi.mode(WIFI_STA);  
  WiFi.begin("TALKTALK21516E", "YJ7P49A4");

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA
    .onStart([]() {
      display.frameBuffer.bindToProducer(updateScreen.getProducer());
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      updateScreen.setText("OTA: " + (String)(progress / (total / 100)) + "%");
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  fullRedraw = true;

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  */
}

SurfaceScrollerImproved::ScrollInstruction tmpinstr;

void loop() {
  tmpinstr.buffer = &SBB;
  tmpinstr.direction = false;
  tmpinstr.distance = 8;
  testScroller.addScrollInstrction(tmpinstr);
  tmpinstr.buffer = &test1;
  tmpinstr.direction = false;
  tmpinstr.distance = 8;
  testScroller.addScrollInstrction(tmpinstr);
    for (int i =0; i < 100; i ++) {
    oled.display();
    delay(10);
  }

  tmpinstr.buffer = &test2;
  tmpinstr.direction = true;
  tmpinstr.distance = 8;
  testScroller.addScrollInstrction(tmpinstr);

  
  for (int i =0; i < 100; i ++) {
    oled.display();
    delay(10);
  }

}

/*
void loop() {
  if (!digitalRead(INPUT_UP)) {
    display.handleInput(UP_SINGLE);
  }
  if (!digitalRead(INPUT_DOWN)) {
    display.handleInput(DOWN_SINGLE);
  }
  if (!digitalRead(INPUT_LEFT)) {
    display.handleInput(LEFT_SINGLE);
  }
  if (!digitalRead(INPUT_RIGHT)) {
    display.handleInput(RIGHT_SINGLE);
  }
  if (!digitalRead(INPUT_CENTER)) {
    display.handleInput(CENTER_SINGLE);
  }

  for (int i =0; i < 10; i ++) {
    oled.display();
    delay(12);
  }
  ArduinoOTA.handle();
}
*/