#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "Font4x5Fixed.h"

#include <duktape.h>

#include <string>
#include <vector>
#include <map>
#include <stdio.h>

#define ANIMATION_SCALE 1

#define INPUT_UP 32
#define INPUT_DOWN 27
#define INPUT_LEFT 25
#define INPUT_RIGHT 33
#define INPUT_CENTER 26

class FrameBuffer {

public:
  uint8_t buffer[40] = {false};

  void setPixel(uint8_t x, uint8_t y, bool val) {
    if (x < 40 && y < 7) {
      if (val) {
        buffer[x] |= ((uint8_t)1 << y);
      } else {
        buffer[x] &= ~((uint8_t)1 << y);
      }
    }
  }

  bool getPixel(uint8_t x, uint8_t y) {
    return (buffer[x] & (uint8_t)1 << y) >> y;
  }
};

namespace window {

  struct RenderParameters {
    uint16_t time_since_last_render;
  };

  struct AttributeValue {
    std::string value;
    bool update = true;
  };

  enum InputEventType {
    UP_SINGLE
  , DOWN_SINGLE
  , LEFT_SINGLE
  , RIGHT_SINGLE
  , CENTER_SINGLE
  };

  class Element {

  public:

    std::vector<Element*> children;
    std::map<std::string, AttributeValue> attributes;

    virtual ~Element() {
      for (auto& i : children) { 
        delete i;
      } 
    }

    virtual bool getPixel(uint8_t x, uint8_t y) = 0;

    virtual void render(RenderParameters params) = 0;

    virtual void childrenUpdate() {}

    virtual bool handleInput(InputEventType inputEventType) = 0;

  };

  class TextElement: public Element {

    GFXcanvas1 canvas;

  public: 


    TextElement()
    : canvas(40, 8)
    {
      AttributeValue val;
      val.value = "test";
      attributes["value"] = val;
    }

    void render(RenderParameters params) { 
      canvas.fillScreen(false);
      canvas.setCursor(0, 4);
      canvas.setFont(&Font4x5Fixed);
      canvas.print(attributes["value"].value.c_str());
    }

    bool getPixel(uint8_t x, uint8_t y) {
      return canvas.getPixel(x, y);
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }

  };

  class Container: public Element {

    FrameBuffer frameBuffer;

  public: 

    Container() {

    }

    bool getPixel(uint8_t x, uint8_t y) {
      return frameBuffer.getPixel(x, y);
    }

    void render(RenderParameters params) {

      for (auto& element : children) {
        element->render(params);
        uint8_t size_x = 40;
        uint8_t size_y = 8;
        uint8_t pos_x = 0;
        uint8_t pos_y = 0;

        if (element->attributes.count("height") > 0) {
          size_y = (uint8_t)std::stoi(element->attributes["height"].value);
        }
        if (element->attributes.count("width") > 0) {
          size_x = (uint8_t)std::stoi(element->attributes["width"].value);
        }

        if (element->attributes.count("y") > 0) {
          pos_y = (uint8_t)std::stoi(element->attributes["y"].value);
        }
        if (element->attributes.count("x") > 0) {
          pos_x = (uint8_t)std::stoi(element->attributes["x"].value);
        }

        for (uint8_t x = 0; x < size_x; x++) {
          for (uint8_t y = 0; y < size_y; y++) {
            frameBuffer.setPixel(x + pos_x, y + pos_y, element->getPixel(x, y));
          }
        }
      }
    }

    bool handleInput(InputEventType inputEventType) {
      return children[0]->handleInput(inputEventType);
    }

  };

  class InstructionScroller: public Element {
  public:

    struct ScrollInstruction {
      Element* element;
      int distance;
      bool direction;
    };

    bool vertical;

  private:

    std::vector<ScrollInstruction> instructionBuffer;
    int8_t offset = 0;
    int16_t leftoverAnitmationTime = 0;

    Element* activeElement = nullptr;
    Element* inactiveElement = nullptr;

    uint8_t active_pos_x = 0;
    uint8_t active_pos_y = 0;
    uint8_t active_size_x = 40;
    uint8_t active_size_y = 8;
    
    uint8_t inactive_pos_x = 0;
    uint8_t inactive_pos_y = 0;
    uint8_t inactive_size_x = 40;
    uint8_t inactive_size_y = 8;

  public:

    uint16_t getRemainingDistance() {
      uint16_t remainingDistance = 0;
      bool scrollDirection = true;
      uint8_t searchIndex = 0;
      while(searchIndex < instructionBuffer.size()) {
        scrollDirection = instructionBuffer[0].direction;
        remainingDistance += instructionBuffer[searchIndex].distance;
        searchIndex ++;
        if (instructionBuffer[searchIndex].direction != scrollDirection) {
          break;
        }
      }
      return remainingDistance;
    }

    InstructionScroller(bool isVertical = true) 
    : vertical(isVertical)
    {
    }

    virtual bool getPixel(uint8_t x, uint8_t y) {
      if(instructionBuffer.size() == 0) {
        if (activeElement) {
          return activeElement->getPixel(x - active_pos_x, y - active_pos_y);
        } else {
          return false;
        }
      }

      uint8_t* positionOffset;
      if (vertical) {
        positionOffset = &y;
      }
      else {
        positionOffset = &x;
      }

      if ((offset + *positionOffset) >= 0 && (offset + *positionOffset) < instructionBuffer[0].distance) {
        if (activeElement) {
          if (vertical) {
            return activeElement->getPixel(x - active_pos_x, (offset + y - active_pos_y));
          }
          else {
            return activeElement->getPixel((offset + x - active_pos_x), y - active_pos_y);
          }
        }
      }
      else if (inactiveElement){
        if (vertical) {
          if (offset >= 0) {
            return inactiveElement->getPixel(x - inactive_pos_x, (offset + y - inactive_pos_y)-instructionBuffer[0].distance);
          }
          else {
            return inactiveElement->getPixel(x - inactive_pos_x, (offset + y - inactive_pos_y)+instructionBuffer[0].distance);
          }
        }
        else {
          if (offset >= 0) {
            return inactiveElement->getPixel((offset + x - inactive_pos_x)-instructionBuffer[0].distance, y - inactive_pos_y);
          }
          else {
            return inactiveElement->getPixel((offset + x - inactive_pos_x)+instructionBuffer[0].distance, y - inactive_pos_y);
          }
        }
      }
      return false;
    }

    void addScrollInstrction(ScrollInstruction nextScrollInstruction) {
      instructionBuffer.push_back(nextScrollInstruction);
    }

    virtual void render(RenderParameters params) {
      leftoverAnitmationTime += params.time_since_last_render;
      uint16_t remainingDistance = getRemainingDistance() - abs(offset);

      if (remainingDistance > 0) {
        while(
          constrain((100.0 * ANIMATION_SCALE)/pow(remainingDistance, 1), 10, 200 * ANIMATION_SCALE) < leftoverAnitmationTime
        && instructionBuffer.size() > 0 
        ) {
          leftoverAnitmationTime -= constrain((100.0 * ANIMATION_SCALE)/pow(remainingDistance, 1), 10, 200 * ANIMATION_SCALE);
          if (offset == 0) {
            instructionBegin();
            inactiveElement = instructionBuffer[0].element;
          }
          if(instructionBuffer[0].direction) {
            offset --;
          } else {
            offset ++;
          }
          remainingDistance --;
          if (instructionBuffer[0].distance == abs(offset)) {
            activeElement = instructionBuffer[0].element;
            inactiveElement = nullptr;
            offset = 0;
            instructionBuffer.erase(instructionBuffer.begin());
            instructionComplete();
          } else {
            if (inactiveElement) {
              inactiveElement->render(params);

              if (inactiveElement->attributes.count("height") > 0) {
                inactive_size_y = (uint8_t)std::stoi(inactiveElement->attributes["height"].value);
              }
              if (inactiveElement->attributes.count("width") > 0) {
                inactive_size_x = (uint8_t)std::stoi(inactiveElement->attributes["width"].value);
              }

              if (inactiveElement->attributes.count("y") > 0) {
                inactive_pos_y = (uint8_t)std::stoi(inactiveElement->attributes["y"].value);
              }
              if (inactiveElement->attributes.count("x") > 0) {
                inactive_pos_x = (uint8_t)std::stoi(inactiveElement->attributes["x"].value);
              }

            }
          }
        } 
      } else {
        leftoverAnitmationTime = 0;
      }
      if (activeElement) {
        activeElement->render(params);

        if (activeElement->attributes.count("height") > 0) {
          active_size_y = (uint8_t)std::stoi(activeElement->attributes["height"].value);
        }
        if (activeElement->attributes.count("width") > 0) {
          active_size_x = (uint8_t)std::stoi(activeElement->attributes["width"].value);
        }

        if (activeElement->attributes.count("y") > 0) {
          active_pos_y = (uint8_t)std::stoi(activeElement->attributes["y"].value);
        }
        if (activeElement->attributes.count("x") > 0) {
          active_pos_x = (uint8_t)std::stoi(activeElement->attributes["x"].value);
        }
        
      }

    }

    bool handleInput(InputEventType inputEventType) {
      if (activeElement) {
        return activeElement->handleInput(inputEventType);
      }
      return false;
    }

    void setElement(Element* element) {
      instructionBuffer.empty();
      activeElement = element;
      offset = 0;
    }

    virtual void instructionComplete() {}

    virtual void instructionBegin() {}

    virtual void childrenUpdate() {}

  };

  class ElementMenu: public InstructionScroller {
  public:

    uint8_t pos = 0;

    ElementMenu(bool isVertical = true)
    : InstructionScroller(isVertical)
    {
      AttributeValue tmp;
      tmp.value = "0";
      attributes["index"] = tmp;
    }

    void render(RenderParameters params) {

      int8_t newIndex = constrain(std::stoi(attributes["index"].value), 0, children.size() - 1);
      attributes["index"].value = std::to_string(newIndex);
      while (newIndex != pos) {
        ScrollInstruction scroll;
        if (pos  > newIndex) {
          pos --;
          scroll.direction = true;
        } else {
          pos ++;
          scroll.direction = false;
        }
        scroll.element = children[pos];
        if (vertical) {
          if (scroll.element->attributes.count("height") > 0) {
            scroll.distance = std::stoi(scroll.element->attributes["height"].value);
          } else {
            scroll.distance = 8;
          }
        } else {
          if (scroll.element->attributes.count("width") > 0) {
            scroll.distance = std::stoi(scroll.element->attributes["width"].value);
          } else {
            scroll.distance = 41;
          }
        }
        scroll.distance = 8;
        addScrollInstrction(scroll);
      }

      InstructionScroller::render(params);
    }

    bool getPixel(uint8_t x, uint8_t y) {
      return InstructionScroller::getPixel(x, y);
    }
    
    bool handleInput(InputEventType inputEventType) {
      switch (inputEventType) {
        case UP_SINGLE:
          if (attributes.count("index") > 0) {
            attributes["index"].value = std::to_string(std::stoi(attributes["index"].value) - 1);
          }
          return true;
          break;
        case DOWN_SINGLE:
          if (attributes.count("index") > 0) {
            attributes["index"].value = std::to_string(std::stoi(attributes["index"].value) + 1);
          }
          return true;
          break;
        default:
          return InstructionScroller::handleInput(inputEventType);
      }
      return false;
    }

    void childrenUpdate() {
      AttributeValue tmp;
      tmp.value = "0";
      attributes["index"] = tmp;
      pos = 0;
    }

  };

  static duk_ret_t createElement(duk_context* ctx) {
    Element* newElement = nullptr;
    Element* parent = (Element*)duk_require_pointer(ctx, 0);
    const char* type = duk_require_string(ctx, 1);

    if (strcmp(type, "text") == 0) {
      newElement = new window::TextElement;
    } else if (strcmp(type, "container") == 0) {
      newElement = new window::Container;
    } else if (strcmp(type, "inscroll") == 0) {
      newElement = new window::ElementMenu;
    } else {
      return -1;
    }

    if (newElement) {
      parent->children.push_back(newElement);
      parent->childrenUpdate();
      duk_push_pointer(ctx,(void*)newElement);
      return 1;
    }
    return -1;
  }

  static duk_ret_t getAttribute(duk_context* ctx) {
    Element* element = (Element*)duk_require_pointer(ctx, 0);
    const char* key = duk_require_string(ctx, 1);
    
    if (element->attributes.count(key) > 0) {
      duk_push_string(ctx, element->attributes[key].value.c_str());
    } else {
      duk_push_undefined(ctx);
    }
    return 1;
  }

  static duk_ret_t setAttribute(duk_context* ctx) {
    Element* element = (Element*)duk_require_pointer(ctx, 0);
    std::string key = duk_require_string(ctx, 1);
    AttributeValue setValue;
    setValue.value = duk_require_string(ctx, 2);

    element->attributes[key] = setValue;

    return 0;

  }

  static duk_ret_t print(duk_context* ctx) {
    const char* val = duk_require_string(ctx, 0);
    Serial.print(val);
    return 0;
  }

}

class Activity: public window::Container {
  duk_context *ctx;
public:

  Activity(std::string name) {

    Serial.print("Starting activity ");


    ctx = duk_create_heap_default();
    if (!ctx) { 
      ///delete this; TODO: exit in bette way
      return;
    }

    duk_push_global_object(ctx);

    duk_push_pointer(ctx, (void*)this);
    duk_put_prop_string(ctx, 0, "objectPointer");

    duk_push_c_function(ctx, window::createElement, 2);
    duk_put_prop_string(ctx, 0, "createElement");

    duk_push_c_function(ctx, window::getAttribute, 2);
    duk_put_prop_string(ctx, 0, "getAttribute");

    duk_push_c_function(ctx, window::setAttribute, 3);
    duk_put_prop_string(ctx, 0, "setAttribute");

    duk_push_c_function(ctx, window::print, 1);
    duk_put_prop_string(ctx, 0, "print");
    Serial.print("Loading JS");

    Serial.print("Activity started");


  }
};

/*
class Scroller: public WindowElement {

  uint16_t stepRemaining = 0;
  
public:

  uint8_t scrollerViewSize;

  uint16_t scrollerSize;

  int16_t position = 0;
  int16_t targetPosition = 0;

  bool vertical;

  struct segment {
    uint8_t start;
    uint8_t size;
    WindowElement* source;
  };

  std::vector<segment> segments;

  uint16_t getScrollerSize() {
    uint16_t size = 0;
    for (auto& segment : segments) {
      size += segment.size;
    }
    return size;
  }

  void limitPosition() {
    while (position + scrollerViewSize  > scrollerSize) {
      position -= scrollerSize;
    } 
    return;
    while (position - scrollerViewSize  < -1*scrollerSize) {
      position += scrollerSize;
    }
    return;
  }

  void buildSegments(int16_t position) {
    int16_t elementLowerPosition = -1*scrollerSize;
    uint8_t elementIndex = 0;
    WindowElement* currentElement;

    scrollerSize = getScrollerSize();
    limitPosition();

    if (vertical) {
      elementLowerPosition += components[0]->size_y;
    } else {
      elementLowerPosition += components[0]->size_x;
    }

    while (elementLowerPosition < position) {
      elementIndex ++;
      elementIndex = elementIndex % components.size();
      currentElement = components[elementIndex];
      if (vertical) {
        elementLowerPosition += currentElement->size_y;
      } else {
        elementLowerPosition += currentElement->size_x;
      }
    }

    segments.clear();

    while (elementLowerPosition <= position + scrollerViewSize) {
      segment nextSegment;
      currentElement = components[elementIndex];
      nextSegment.source = currentElement;
      if (vertical) {
        nextSegment.start = elementLowerPosition - currentElement->size_y;
        nextSegment.size = currentElement->size_y;
      } else {
        nextSegment.start = elementLowerPosition - currentElement->size_x;
        nextSegment.size = currentElement->size_x;
      }
      segments.push_back(nextSegment); // right vector order?

      elementIndex ++;
      elementIndex = elementIndex % components.size();
      currentElement = components[elementIndex];

      if (vertical) {
        elementLowerPosition += currentElement->size_y;
      } else {
        elementLowerPosition += currentElement->size_x;
      }
    }
  }

  void updateScroller(int16_t position = 0) {
    buildSegments(position);
  }

  Scroller(bool vertical = true)
  : vertical(vertical)
  {
    if (vertical) {
      scrollerViewSize = size_y;
    } else {
      scrollerViewSize = size_x;
    }

    updateScroller();
  }

  void moveSegments(int16_t ds) {
    position += ds;
    limitPosition();
    for (auto& segment : segments) {
      segment.start += ds;
    }

    if (segments[0].start > 0) {
      buildSegments(position);
    }

    if ((segments.back().start + segments.back().size)  > scrollerViewSize) {
      buildSegments(position);
    }
  }

  segment* getSegment(uint8_t position) {
    for (auto& i : segments) { 
      if (position <= i.start + i.size && position >= i.start) {
        return &i;
      }
    } 
    return nullptr;
  }

  bool getPixel(uint8_t x, uint8_t y) {
    uint8_t* scrollingValue;
    if (vertical) {
      scrollingValue = &y; 
    }
    else {
      scrollingValue = &x;
    }

    segment* currentSegment = getSegment(*scrollingValue);
    if (currentSegment) {
      if (vertical) {
        return currentSegment->source->getPixel(x, *scrollingValue - currentSegment->start);
      } else {
        return currentSegment->source->getPixel(*scrollingValue - currentSegment->start, y);
      }
    }
    return false;
  }

  virtual int16_t animator(uint16_t &lastUpdate, int16_t position, int16_t targetPosition) {
    if (lastUpdate >= 1000) {
      lastUpdate -= 1000;
      return 1;
    }
    return 0;
  }

  void animate(uint16_t stepSize) {
    if (stepSize <= 0) {return;}
    stepRemaining += stepSize;
    uint16_t ds = animator(stepRemaining, position, targetPosition);
    moveSegments(ds);

    /// TODO: animate all subelements
  }

};
*/

class FlipDisplay {

public:

  bool fullRedraw = false;

  window::Element* frameBuffer;

  FlipDisplay()
  {
    
  }

  void begin() {
    fullRedraw = true;
  }

  static void updateDisplay(void *arg) { 
    
    FlipDisplay* display = (FlipDisplay*)arg;

    window::RenderParameters params;
    params.time_since_last_render = 16;

    while(true) {
      display->frameBuffer->render(params);

      for (int module = 0; module < 8; module ++) {
        Serial2.write(0b10000000 | (module << 4));
        for (int x = 0; x < 5; x ++) {
          uint8_t colBuf = 0;
          for (int y = 0; y < 7; y ++) {
            colBuf |= display->frameBuffer->getPixel(x + module * 5, y) << y;
          }
          Serial2.write(colBuf);
        }
        if (display->fullRedraw) {
          Serial2.write(0b10000110 | (module << 4));
        }
        else {
          Serial2.write(0b10000101 | (module << 4));
        }
      }
      display->fullRedraw = false;
      vTaskDelay(16/portTICK_PERIOD_MS );
    }
  }
  
};

FlipDisplay display;

Activity* test;

bool tick = false;

static void inputSimulator(void* arg) {
  if (tick){ 
    display.frameBuffer->handleInput(window::DOWN_SINGLE);
  } else {
    display.frameBuffer->handleInput(window::UP_SINGLE);
  }
  tick = !tick;
}

static void input(int pin, void *arg) {
  switch(pin) {
    case INPUT_UP:
      display.frameBuffer->handleInput(window::UP_SINGLE);
      break;
    case INPUT_DOWN:
      display.frameBuffer->handleInput(window::DOWN_SINGLE);
      break;
    case INPUT_LEFT:
      display.frameBuffer->handleInput(window::LEFT_SINGLE);
      break;
    case INPUT_RIGHT:
      display.frameBuffer->handleInput(window::RIGHT_SINGLE);
      break;
    case INPUT_CENTER:
      display.frameBuffer->handleInput(window::CENTER_SINGLE);
      break;
  }
}

TaskHandle_t displayTask;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);

  test = new Activity("test.main");

  display.frameBuffer = test;

  display.begin(); 

  xTaskCreatePinnedToCore (
        display.updateDisplay,
        "Display",
        10000,
        &display,
        1,
        &displayTask,
        1
      );

}

void loop() {

}