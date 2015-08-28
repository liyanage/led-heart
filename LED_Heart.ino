

#include "FastLED.h"
#include <EEPROM.h>

#define LED_COUNT 31
#define LED_PIN 19

#define BUTTON_PIN 3
#define BUTTON_INTERRUPT_NUMBER 1
#define BUTTON_LONG_PRESS_INTERVAL_MILLISECONDS 2000
#define BUTTON_DOUBLE_PRESS_INTERVAL_MILLISECONDS 250

#define BRIGHTNESS_MIN 10
#define BRIGHTNESS_MAX 100
#define BRIGHTNESS_DEFAULT 20
#define BRIGHTNESS_INCREMENT 10
#define BRIGHTNESS_FLASHLIGHT 200
#define BRIGHTNESS_EEPROM_ADDRESS 0

#define EEPROM_UNDEFINED_VALUE 255


enum AnimationState {
  AnimationStateUndefined,
  AnimationStateInitialized,
  AnimationStateRunning,
};

struct AnimationContext;
typedef void (*AnimationStepFunction)(uint8_t, AnimationContext *);
typedef struct AnimationContext {
  AnimationState state;
  unsigned int ledCount;
  CRGB *leds;
  AnimationStepFunction stepFunction;
  unsigned long frameDelay;
  byte stepFunctionPrivate[50];
} AnimationContext;


// Toplevel animation management functions
void advanceAnimation(AnimationContext *animationContext);
void transitionToNextAnimation(AnimationContext *animationContext);
void runAnimation(AnimationContext *animationContext);

// Animation step functions
void animationStepFunctionRainbow(uint8_t frameIndex, AnimationContext *animationContext);
void animationStepFunctionRainbowCycle(uint8_t frameIndex, AnimationContext *animationContext);
void animationStepFunctionInsideOut(uint8_t frameIndex, AnimationContext *animationContext);
void animationStepFunctionFire2012(uint8_t frameIndex, AnimationContext *animationContext);
void animationStepFunctionTheaterChaseRainbow(uint8_t frameIndex, AnimationContext *animationContext);
//void animationStepFunctionInnerToOuter(uint8_t frameIndex, AnimationContext *animationContext);

AnimationStepFunction kAllAnimationStepFunctions[] = {
  animationStepFunctionRainbow,
  animationStepFunctionRainbowCycle,
  animationStepFunctionInsideOut,
  animationStepFunctionFire2012,
  animationStepFunctionTheaterChaseRainbow,
//  animationStepFunctionInnerToOuter,
  NULL
};

unsigned long kAnimationStepFunctionFrameDelays[] = {
  25,
  25,
  30,
  30,
  60,
  //25,
};
  
enum ButtonEventType {
  ButtonEventTypeNone,
  ButtonEventTypePressed,
  ButtonEventTypeLongPressed,
  ButtonEventTypeReleased,
};

typedef struct ButtonEvent {
  ButtonEventType type;
  unsigned int releaseCount;
} ButtonEvent;

ButtonEvent determineButtonEvent();
  
enum State {
  StateEnteringAnimationFromLongPress,
  StateRunningAnimation,
  StateEnteringBrightnessAdjustment,
  StateRunningBrightnessAdjustment,
  StateRunningFlashlight,
};

unsigned int axisTopToBottom1[] = {   0,  1,      2,  3,     -1};
unsigned int axisTopToBottom2[] = { 4,  5,  6,  7,  8,  9,   -1};
unsigned int axisTopToBottom3[] = {10, 11, 12, 13, 14, 15,   -1};
unsigned int axisTopToBottom4[] = {  16, 17, 18, 19, 20,     -1};
unsigned int axisTopToBottom5[] = {    21, 22, 23, 24,       -1};
unsigned int axisTopToBottom6[] = {      25, 26, 27,         -1};
unsigned int axisTopToBottom7[] = {        28, 29,           -1};
unsigned int axisTopToBottom8[] = {          30,             -1};

unsigned int *axisTopToBottom[] = {
  axisTopToBottom1,
  axisTopToBottom2,
  axisTopToBottom3,
  axisTopToBottom4,
  axisTopToBottom5,
  axisTopToBottom6,
  axisTopToBottom7,
  axisTopToBottom8,
  NULL  
};

unsigned int *axisBottomToTop[] = {
  axisTopToBottom8,
  axisTopToBottom7,
  axisTopToBottom6,
  axisTopToBottom5,
  axisTopToBottom4,
  axisTopToBottom3,
  axisTopToBottom2,
  axisTopToBottom1,
  NULL  
};


unsigned int axisInsideToOutside1[] = {5, 8, 11, 14, 17, 19, 22, 23, 26, -1};
unsigned int axisInsideToOutside2[] = {0, 1, 2, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 18, 20, 21, 24, 25, 27, 28, 29, 30, -1};

unsigned int *axisInsideToOutside[] = {
  axisInsideToOutside1,
  axisInsideToOutside2,
  NULL  
};

unsigned int *axisOutsideToInside[] = {
  axisInsideToOutside2,
  axisInsideToOutside1,
  NULL  
};



CRGB leds[LED_COUNT];
AnimationContext gAnimationContext;

volatile int gButtonDidChange = 0;
volatile unsigned long gButtonEventTime = 0;
volatile int gBrightness = BRIGHTNESS_DEFAULT;


void setup() {
  int persistedBrightness = EEPROM.read(BRIGHTNESS_EEPROM_ADDRESS);
  if (persistedBrightness != EEPROM_UNDEFINED_VALUE) {
    gBrightness = persistedBrightness;
  }
  
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(gBrightness);
  clearToColor(CRGB::Black);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_INTERRUPT_NUMBER , signalButtonStateChange, CHANGE);
  gAnimationContext.state = AnimationStateUndefined;
}


void loop() {
  static State sCurrentState = StateRunningAnimation;

  ButtonEvent buttonEvent = determineButtonEvent();

  switch (sCurrentState) {

    case StateRunningAnimation:
      if (buttonEvent.type == ButtonEventTypeLongPressed) {
        sCurrentState = StateEnteringBrightnessAdjustment;
        enterBrightnessAdjustment();
      } else if (buttonEvent.type == ButtonEventTypeReleased) {
        if (buttonEvent.releaseCount > 1) {
          sCurrentState = StateRunningFlashlight;
          enterFlashlight();
        } else {
          transitionToNextAnimation(&gAnimationContext);
        }
      } else {
        runAnimation(&gAnimationContext);
      }
      break;

    case StateEnteringBrightnessAdjustment:
      if (buttonEvent.type == ButtonEventTypeReleased) {
        sCurrentState = StateRunningBrightnessAdjustment;
      }
      break;

    case StateRunningBrightnessAdjustment:
      if (buttonEvent.type == ButtonEventTypeReleased) {
        int delta = buttonEvent.releaseCount == 1 ? BRIGHTNESS_INCREMENT : -BRIGHTNESS_INCREMENT;
        gBrightness = gBrightness + delta;
        if (gBrightness > BRIGHTNESS_MAX) {
          gBrightness = BRIGHTNESS_MIN;
        } else if (gBrightness < BRIGHTNESS_MIN ) {
          gBrightness = BRIGHTNESS_MAX;
        }
        FastLED.setBrightness(gBrightness);
        
        showCurrentBrightnessAdjustmentValue();
      } else if (buttonEvent.type == ButtonEventTypeLongPressed) {
        sCurrentState = StateEnteringAnimationFromLongPress;
        leaveBrightnessAdjustment();
      }
      break;

    case StateEnteringAnimationFromLongPress:
      if (buttonEvent.type == ButtonEventTypeReleased) {
        sCurrentState = StateRunningAnimation;      
      }
      break;
      
    case StateRunningFlashlight:
      if (buttonEvent.type == ButtonEventTypeReleased) {
        sCurrentState = StateRunningAnimation;
        leaveFlashlight();
      }
      break;

  }

}


void transitionToNextAnimation(AnimationContext *animationContext) {
  int i = 0;
  while (1) {
    if (animationContext->stepFunction == kAllAnimationStepFunctions[i]) {
      i = kAllAnimationStepFunctions[i + 1] ? i + 1 : 0;
      animationContext->stepFunction = kAllAnimationStepFunctions[i];
      animationContext->frameDelay = kAnimationStepFunctionFrameDelays[i];
      animationContext->state = AnimationStateInitialized;
      break;
    }
    i++;
  }
}


void runAnimation(AnimationContext *animationContext) {
  if (animationContext->state == AnimationStateUndefined) {
    memset(&gAnimationContext, 0, sizeof(gAnimationContext));
    animationContext->state = AnimationStateInitialized;
    animationContext->ledCount = LED_COUNT;
    animationContext->leds = leds;
    animationContext->frameDelay = 25;
    animationContext->stepFunction = kAllAnimationStepFunctions[0];
  }
  advanceAnimation(animationContext);  
}


void clearToColor(CRGB color) {
  clearToColorNoShow(color);
  FastLED.show();
}


void clearToColorNoShow(CRGB color) {
  for (int i = 0; i< LED_COUNT; i++) {
    leds[i] = color;
  }
}


void enterBrightnessAdjustment() {
  showCurrentBrightnessAdjustmentValue();
}


void leaveBrightnessAdjustment() {
  EEPROM.update(BRIGHTNESS_EEPROM_ADDRESS, gBrightness);
  clearToColor(CRGB::Black);
}


void enterFlashlight() {
  FastLED.setBrightness(BRIGHTNESS_FLASHLIGHT);
  clearToColor(CRGB::White);
}


void leaveFlashlight() {
  FastLED.setBrightness(gBrightness);
  clearToColor(CRGB::Black);
}


void showCurrentBrightnessAdjustmentValue() {
  clearToColorNoShow(CRGB::White);
  int level = gBrightness * (BRIGHTNESS_MAX / BRIGHTNESS_INCREMENT) / BRIGHTNESS_MAX - 1;
  leds[level] = CRGB::Red;
  FastLED.show();  
}


void signalButtonStateChange() {
  gButtonDidChange = 1;
  gButtonEventTime = millis();
}


ButtonEvent determineButtonEvent() {
  static int sButtonIsPressed = 0;
  static unsigned long sButtonPressedTime = 0;
  static unsigned long sButtonReleasedTime = 0;
  static int sPendingReleaseCount = 0;

  unsigned long now = millis();

  ButtonEvent buttonEvent;
  buttonEvent.type = ButtonEventTypeNone;
  buttonEvent.releaseCount = 0;
  
  if (gButtonDidChange) {
    gButtonDidChange = 0;
    int oldButtonIsPressed = sButtonIsPressed;

    // Debounce by polling for three consecutive matching values
    int matchingReadCount = 0;
    sButtonIsPressed = digitalRead(BUTTON_PIN) == LOW;
    while (matchingReadCount < 3) {
      delay(1);
      int value = digitalRead(BUTTON_PIN) == LOW;
      if (value == sButtonIsPressed) {
        matchingReadCount++;
      } else {
        matchingReadCount = 0;
        sButtonIsPressed = value;
      }
    }

    if (oldButtonIsPressed && !sButtonIsPressed) {
      unsigned long intervalSinceLastButtonRelease = now - sButtonReleasedTime;
      if (intervalSinceLastButtonRelease < BUTTON_DOUBLE_PRESS_INTERVAL_MILLISECONDS) {
        sPendingReleaseCount++;
      } else {
        sPendingReleaseCount = 1;
      }
      sButtonReleasedTime = gButtonEventTime;
      sButtonPressedTime = 0;
    } else if (!oldButtonIsPressed && sButtonIsPressed) {
      buttonEvent.type = ButtonEventTypePressed;
      sButtonPressedTime = gButtonEventTime;
    }
  } else if (sButtonPressedTime) {
    unsigned long intervalSinceButtonPress = now - sButtonPressedTime;
    if (intervalSinceButtonPress > BUTTON_LONG_PRESS_INTERVAL_MILLISECONDS) {
      buttonEvent.type = ButtonEventTypeLongPressed;
      sButtonPressedTime = 0;
    }
  } else if (sPendingReleaseCount) {
    unsigned long intervalSinceLastButtonRelease = now - sButtonReleasedTime;
    if (intervalSinceLastButtonRelease > BUTTON_DOUBLE_PRESS_INTERVAL_MILLISECONDS) {
      buttonEvent.type = ButtonEventTypeReleased;
      buttonEvent.releaseCount = sPendingReleaseCount;
      sPendingReleaseCount = 0;
    }
  }

  return buttonEvent;
}


// Animation toplevel function

void advanceAnimation(AnimationContext *animationContext) {
    static uint8_t frameIndex = 0;
    static unsigned long lastCallTime = millis();
    
    if (animationContext->state != AnimationStateRunning) {
        animationContext->state = AnimationStateRunning;
        frameIndex = 0;
        lastCallTime = millis();
        memset(animationContext->stepFunctionPrivate, 0, sizeof(animationContext->stepFunctionPrivate));
    }

    unsigned long currentTime = millis();
    unsigned long timeDiff = currentTime - lastCallTime;
    if (timeDiff < animationContext->frameDelay) {
        return;
    }
    /* Turned off frame skipping because it looks better without it
    else if (timeDiff > animationContext->frameDelay)
    {
        // Skip frames
        int numFrames = (timeDiff - animationContext->frameDelay) / frameDelay;
        frameIndex += numFrames;
        skippedFrames = numFrames;
    }*/

    // Call the animation frame
    animationContext->stepFunction(frameIndex, animationContext);
    FastLED.show();
    frameIndex++;

    lastCallTime = currentTime;
}


// Animation step functions

// -------------------------
void animationStepFunctionRainbow(uint8_t frameIndex, AnimationContext *animationContext) {
    for (int i = 0; i < animationContext->ledCount; i++) {
        animationContext->leds[i] = colorWheel((i + frameIndex) & 255);
    }
}

// -------------------------
// Slightly different, this makes the rainbow equally distributed throughout
void animationStepFunctionRainbowCycle(uint8_t frameIndex, AnimationContext *animationContext) {
    for (int i = 0; i < animationContext->ledCount; i++)  {
        animationContext->leds[i] = colorWheel(((i * 256 / animationContext->ledCount) + frameIndex) & 255);
    }
}

// -------------------------
// Theatre-style crawling lights with rainbow effect
void animationStepFunctionTheaterChaseRainbow(uint8_t frameIndex, AnimationContext *animationContext) {
    const uint8_t offset = frameIndex % 3;
    const boolean on = frameIndex % 2;

    for (int i = 0; i < animationContext->ledCount; i = i + 3)  {
        // Turn every third pixel on or off
        CRGB color = on ? colorWheel((i + frameIndex) % 255) : CRGB::Black;
        animationContext->leds[i + offset] = color;    
    }
}

// -------------------------
/*
typedef struct {
  int initialized; 
  CRGB color;
  boolean skipOuter;
} InnerToOuterParameters;

const uint8_t kInnerLeds[]  = { 24, 17, 12, 18, 23 };
const uint8_t kMiddleLeds[] = { 28, 25, 16, 13, 7, 4, 8, 11, 19, 22, 29 };
const uint8_t kOuterLeds[]  = { 27, 26, 15, 14, 6, 5, 1, 0, 2, 3, 9, 10, 20, 21, 30 };

const uint8_t kInnerLedsLength = sizeof(kInnerLeds) / sizeof(uint8_t);
const uint8_t kMiddleLedsLength = sizeof(kMiddleLeds) / sizeof(uint8_t);
const uint8_t kOuterLedsLength = sizeof(kOuterLeds) / sizeof(uint8_t);

// Fills the heart with color, ring by ring, starting from the inner ring
void animationStepFunctionInnerToOuter(uint8_t frameIndex, AnimationContext *animationContext) {

    InnerToOuterParameters *p = (InnerToOuterParameters *)animationContext->stepFunctionPrivate;
    if (!p->initialized) {
      p->initialized = 1;
      p->color = CRGB::Red;
      p->skipOuter = false;
    }
        
    const uint8_t numRings = p->skipOuter ? 2 : 3;
    const uint8_t ringIndex = frameIndex % numRings;

    for (uint8_t i = 0; i < kInnerLedsLength; i++)
    {
        const uint8_t pixel = kInnerLeds[i];
        leds[pixel] = ringIndex == 0 ? p->color : CRGB::Black;
    }

    for (uint8_t i = 0; i < kMiddleLedsLength; i++)
    {
        const uint8_t pixel = kMiddleLeds[i];
        leds[pixel] = ringIndex == 1 ? p->color : CRGB::Black;
    }

    for (uint8_t i = 0; i < kOuterLedsLength; i++)
    {
        const uint8_t pixel = kOuterLeds[i];
        leds[pixel] = ringIndex == 2 ? p->color : CRGB::Black;
    }
}
*/

// -------------------------
// from https://github.com/FastLED/FastLED/blob/master/examples/Fire2012/Fire2012.ino

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100 
#define COOLING  80

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 50

#define HEAT_CELL_COUNT 8

void animationStepFunctionFire2012(uint8_t frameIndex, AnimationContext *animationContext) {
  random16_add_entropy(random());

  int heatCellCount = HEAT_CELL_COUNT;
  
  // Array of temperature readings at each simulation cell
  static byte heat[HEAT_CELL_COUNT];

  // Step 1.  Cool down every cell a little
    for (int i = 0; i < heatCellCount; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / heatCellCount) + 2));
    }
  
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for (int k = heatCellCount - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    
    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if (random8() < SPARKING) {
      int y = random8(3);
      heat[y] = qadd8(heat[y], random8(160, 255));
    }

    // Step 4.  Map from heat cells to LED colors
//    for (int j = 0; j < LED_COUNT; j++) {
//        animationContext->leds[j] = HeatColor(heat[j]);
//    }

    for (int i = 0; i < heatCellCount; i++) {
      unsigned int *group = axisBottomToTop[i];
      unsigned int j = 0;
      CRGB groupColor = HeatColor(heat[i]);
      while (group[j] != -1) {
        animationContext->leds[group[j]] = groupColor;
        j++;
      }
    }
}


// -------------------------
void animationStepFunctionInsideOut(uint8_t frameIndex, AnimationContext *animationContext) {
  uint8_t saturation = 255 - scale8(cubicwave8(frameIndex), 175);
  CHSV color = CHSV(0, saturation, 254);
  unsigned int *group = axisInsideToOutside[0];
  int i = 0;
  while (group[i] != -1) {
    animationContext->leds[group[i]] = color;
    i++;
  }

  saturation = 255 - scale8(cubicwave8(frameIndex + 25), 175);
  color = CHSV(0, saturation, 254);
  group = axisInsideToOutside[1];
  i = 0;
  while (group[i] != -1) {
    animationContext->leds[group[i]] = color;
    i++;
  }
}




// Animation helper functions

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
CRGB colorWheel(byte wheelPos) {
    wheelPos = 255 - wheelPos;
    if (wheelPos < 85)  {
        return CRGB(255 - wheelPos * 3, 0, wheelPos * 3);
    }  else if (wheelPos < 170)  {
        wheelPos -= 85;
        return CRGB(0, wheelPos * 3, 255 - wheelPos * 3);
    } else  {
        wheelPos -= 170;
        return CRGB(wheelPos * 3, 255 - wheelPos * 3, 0);
    }
}

int ledGroupCountInAnimationAxis(unsigned int *animationAxis[]) {
  int count = 0;
  while (animationAxis[count]) {
    count++;
  }
  return count;
}

