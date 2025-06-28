#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32
#include <driver/i2s.h>
#endif

/*
 * LED Controller Usermod for WLED
 * 
 * This usermod provides a comprehensive LED control system with:
 * - Color selection (White, Red, Orange, Yellow, Green, Blue, Pink, Purple, Cycle, Rainbow)
 * - Pattern modes (Uniform Blink, Chaser, Multiple Chaser, Random Blink, Sound Reactive patterns)
 * - Sound reactive functionality using I2S microphone
 * - Button control for mode switching
 * - Brightness control
 * - Sleep mode functionality
 * 
 * Button behavior:
 * - Quick press: Change color/pattern
 * - Long press: Switch between color/pattern/brightness modes
 * - Very long press: Enter sleep mode
 * - Long press when off: Enter brightness selection mode
 */

//=============================================================================
// USER CONFIGURABLE SETTINGS
//=============================================================================

// TIMING SETTINGS
#define QUICK_PRESS_TIME   500    // Maximum time in ms for a quick press
#define LONG_PRESS_TIME    1000   // Time in ms to trigger mode change
#define SLEEP_PRESS_TIME   3000   // Time in ms to enter sleep mode

// PATTERN TIMING
#define PATTERN_INTERVAL   100    // Time in ms between pattern updates
#define BLINK_INTERVAL     200    // Time in ms for blink pattern
#define CHASE_INTERVAL     250    // Time in ms for chase patterns
#define COLOR_CYCLE_INTERVAL 2000 // Time in ms between color changes

// SOUND REACTIVE SETTINGS
#define GAIN_FACTOR        4.0    // Amplification for sound input
#define BASE_BRIGHTNESS    0      // Minimum brightness for sound reactive modes
#define SOUND_THRESHOLD    2000000 // Threshold for sound detection
#define SMOOTHING_FACTOR   0.1    // How smooth the sound response is
#define PULSING_DIVISOR    25000000.0  // Divisor for pulsing mode
#define CLOCKWISE_SENSITIVITY 50000000  // Sensitivity for clockwise mode

// BRIGHTNESS LEVELS (0-255)
#define BRIGHTNESS_25      64     // 25% brightness level
#define BRIGHTNESS_50      128    // 50% brightness level
#define BRIGHTNESS_75      192    // 75% brightness level
#define BRIGHTNESS_100     255    // 100% brightness level

// I2S Configuration - now configurable through web interface
#define SAMPLE_RATE     44100
#define SAMPLE_BITS     32
#define SAMPLE_BUFFER   128

class LEDControllerUsermod : public Usermod {
private:
  // Enums for modes, colors, and patterns
  enum Mode {
    COLOR_SELECT,               // Mode for selecting colors
    PATTERN_SELECT,             // Mode for selecting patterns
    BRIGHTNESS_SELECT           // Mode for selecting brightness
  };

  enum Colors {
    WHITE, RED, ORANGE, YELLOW, GREEN, BLUE, PINK, PURPLE, CYCLE, RAINBOW, COLOR_COUNT, WLED_MODE
  };

  enum Patterns {
    UNIFORM_BLINK, CHASER, MULTIPLE_CHASER, RANDOM_BLINK, SOUND_REACTIVE_PULSING, SOUND_REACTIVE_CLOCKWISE, SOUND_REACTIVE_RANDOM, PATTERN_COUNT
  };

  // Configuration variables
  bool enabled = true;
  bool i2sInitialized = false;
  
  // I2S pin configuration - configurable through web interface
  int8_t i2sSdPin = 5;    // I2S SD (Data) pin
  int8_t i2sWsPin = 4;    // I2S WS (Word Select) pin  
  int8_t i2sSckPin = 6;   // I2S SCK (Clock) pin
  int8_t i2sMclkPin = -1; // I2S MCLK (Master Clock) pin, -1 for not used
  
  // State variables
  Mode currentMode = COLOR_SELECT;
  Colors currentColor = WHITE;
  Patterns currentPattern = UNIFORM_BLINK;
  bool isActive = false;
  bool isFlashingOrange = false;
  bool inBrightnessSelection = false;
  bool wledControlEnabled = false;  // Track if WLED control is currently enabled
  
  // Variables to store the last known state
  Mode lastMode = COLOR_SELECT;
  Colors lastColor = WHITE;
  Patterns lastPattern = UNIFORM_BLINK;
  
  // Timing variables
  unsigned long buttonPressStart = 0;
  unsigned long lastPatternUpdate = 0;
  unsigned long lastColorChange = 0;
  
  // Position for chaser pattern
  static int position;
  
  // I2S audio buffer
  int32_t samples[SAMPLE_BUFFER];
  
  // Smoothing variables
  float smoothedBrightness = 0;
  
  // Brightness control
  uint8_t maxBrightness = 128;
  uint8_t currentBrightnessLevel = 1;
  
  // Button handling
  static bool buttonWasPressed;
  static unsigned long lastDebounceTime;
  static bool lastButtonState;
  static bool sleepTriggered;
  
  // Configuration names for JSON
  static const char _name[];
  static const char _enabled[];
  static const char _i2sPins[];
  static const char _i2sSdPin[];
  static const char _i2sWsPin[];
  static const char _i2sSckPin[];
  static const char _i2sMclkPin[];

public:
  LEDControllerUsermod() {
    // Initialize static variables
    position = 0;
    buttonWasPressed = false;
    lastDebounceTime = 0;
    lastButtonState = true;
    sleepTriggered = false;
  }

  void setup() override {
    // Initialize I2S for sound reactive features
    i2sInitialized = initializeI2S();
    
    // Start with LEDs off
    isActive = false;
    strip.fill(0, 0, strip.getLengthTotal());
    strip.show();
    
    // Mark initialization as complete
    initDone = true;
  }

  void loop() override {
    if (!enabled || strip.isUpdating()) return;

    // Handle button input
    handleButton();
    
    // Only update if usermod is active and we're not in brightness selection
    if (isActive && !isFlashingOrange && !inBrightnessSelection) {
      // Check if we're in WLED mode - if so, let WLED handle everything
      if (currentColor == WLED_MODE) {
        // Properly enable WLED control by setting a static effect
        if (!wledControlEnabled) {
          enableWLEDControl();
          wledControlEnabled = true;
        }
        return;
      } else {
        // Ensure WLED control is disabled for all custom modes
        if (wledControlEnabled) {
          disableWLEDControl();
          wledControlEnabled = false;
        }
      }
      
      // Otherwise, handle our custom modes
      if (currentMode == COLOR_SELECT) {
        updateColorMode();
      } else if (currentMode == PATTERN_SELECT) {
        updatePatternMode();
      }
    }
  }

  void connected() override {
    // Nothing needed for network connection
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;
    
    JsonObject i2sPins = top.createNestedObject(FPSTR(_i2sPins));
    i2sPins[FPSTR(_i2sSdPin)] = i2sSdPin;
    i2sPins[FPSTR(_i2sWsPin)] = i2sWsPin;
    i2sPins[FPSTR(_i2sSckPin)] = i2sSckPin;
    i2sPins[FPSTR(_i2sMclkPin)] = i2sMclkPin;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[FPSTR(_name)];
    bool configComplete = !top.isNull();

    configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled);
    
    JsonObject i2sPins = top[FPSTR(_i2sPins)];
    if (!i2sPins.isNull()) {
      configComplete &= getJsonValue(i2sPins[FPSTR(_i2sSdPin)], i2sSdPin);
      configComplete &= getJsonValue(i2sPins[FPSTR(_i2sWsPin)], i2sWsPin);
      configComplete &= getJsonValue(i2sPins[FPSTR(_i2sSckPin)], i2sSckPin);
      configComplete &= getJsonValue(i2sPins[FPSTR(_i2sMclkPin)], i2sMclkPin);
    }

    return configComplete;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    
    JsonObject info = user.createNestedObject(FPSTR(_name));
    info["enabled"] = enabled;
    info["i2s_initialized"] = i2sInitialized;
    info["current_mode"] = currentMode;
    info["current_color"] = currentColor;
    info["current_pattern"] = currentPattern;
    info["is_active"] = isActive;
  }

  void addToJsonState(JsonObject& root) override {
    JsonObject state = root[FPSTR(_name)];
    state["enabled"] = enabled;
    state["mode"] = currentMode;
    state["color"] = currentColor;
    state["pattern"] = currentPattern;
    state["active"] = isActive;
    state["brightness"] = maxBrightness;
  }

  bool readFromJsonState(JsonObject& root) override {
    JsonObject state = root[FPSTR(_name)];
    if (state.isNull()) return false;

    bool stateChanged = false;
    
    if (getJsonValue(state["enabled"], enabled)) stateChanged = true;
    if (getJsonValue(state["mode"], currentMode)) stateChanged = true;
    if (getJsonValue(state["color"], currentColor)) stateChanged = true;
    if (getJsonValue(state["pattern"], currentPattern)) stateChanged = true;
    if (getJsonValue(state["active"], isActive)) stateChanged = true;
    if (getJsonValue(state["brightness"], maxBrightness)) stateChanged = true;
    
    return stateChanged;
  }

  uint16_t getId() override {
    return USERMOD_ID_LED_CONTROLLER;
  }

  void addToConfigSchema(JsonObject& root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top["type"] = "object";
    top["title"] = "LED Controller";
    top["description"] = "Advanced LED control system with color selection, patterns, and sound reactivity";
    
    JsonObject enabled = top.createNestedObject("properties");
    enabled["type"] = "boolean";
    enabled["title"] = "Enabled";
    enabled["description"] = "Enable the LED Controller usermod";
    enabled["default"] = true;
    
    JsonObject i2sPins = top.createNestedObject("i2s_pins");
    i2sPins["type"] = "object";
    i2sPins["title"] = "I2S Pin Configuration";
    i2sPins["description"] = "Configure I2S pins for sound reactive features";
    
    JsonObject i2sProps = i2sPins.createNestedObject("properties");
    
    JsonObject sdPin = i2sProps.createNestedObject("i2s_sd_pin");
    sdPin["type"] = "integer";
    sdPin["title"] = "I2S SD Pin";
    sdPin["description"] = "I2S Data pin (SD/DOUT)";
    sdPin["minimum"] = -1;
    sdPin["maximum"] = 48;
    sdPin["default"] = 5;
    
    JsonObject wsPin = i2sProps.createNestedObject("i2s_ws_pin");
    wsPin["type"] = "integer";
    wsPin["title"] = "I2S WS Pin";
    wsPin["description"] = "I2S Word Select pin (WS/LRCK)";
    wsPin["minimum"] = -1;
    wsPin["maximum"] = 48;
    wsPin["default"] = 4;
    
    JsonObject sckPin = i2sProps.createNestedObject("i2s_sck_pin");
    sckPin["type"] = "integer";
    sckPin["title"] = "I2S SCK Pin";
    sckPin["description"] = "I2S Clock pin (SCK/BCLK)";
    sckPin["minimum"] = -1;
    sckPin["maximum"] = 48;
    sckPin["default"] = 6;
    
    JsonObject mclkPin = i2sProps.createNestedObject("i2s_mclk_pin");
    mclkPin["type"] = "integer";
    mclkPin["title"] = "I2S MCLK Pin";
    mclkPin["description"] = "I2S Master Clock pin (optional, use -1 to disable)";
    mclkPin["minimum"] = -1;
    mclkPin["maximum"] = 48;
    mclkPin["default"] = -1;
  }

  void addToConfigUI(JsonObject& root) override {
    JsonObject ui = root.createNestedObject("ui");
    ui["title"] = "LED Controller";
    ui["description"] = "Advanced LED control system with color selection, patterns, and sound reactivity";
    
    JsonObject enabled = ui.createNestedObject("enabled");
    enabled["type"] = "checkbox";
    enabled["title"] = "Enable LED Controller";
    enabled["description"] = "Enable the LED Controller usermod";
    
    JsonObject i2sPins = ui.createNestedObject("i2s_pins");
    i2sPins["type"] = "object";
    i2sPins["title"] = "I2S Pin Configuration";
    i2sPins["description"] = "Configure I2S pins for sound reactive features";
    
    JsonObject sdPin = i2sPins.createNestedObject("i2s_sd_pin");
    sdPin["type"] = "number";
    sdPin["title"] = "I2S SD Pin";
    sdPin["description"] = "I2S Data pin (SD/DOUT)";
    sdPin["minimum"] = -1;
    sdPin["maximum"] = 48;
    sdPin["default"] = 5;
    
    JsonObject wsPin = i2sPins.createNestedObject("i2s_ws_pin");
    wsPin["type"] = "number";
    wsPin["title"] = "I2S WS Pin";
    wsPin["description"] = "I2S Word Select pin (WS/LRCK)";
    wsPin["minimum"] = -1;
    wsPin["maximum"] = 48;
    wsPin["default"] = 4;
    
    JsonObject sckPin = i2sPins.createNestedObject("i2s_sck_pin");
    sckPin["type"] = "number";
    sckPin["title"] = "I2S SCK Pin";
    sckPin["description"] = "I2S Clock pin (SCK/BCLK)";
    sckPin["minimum"] = -1;
    sckPin["maximum"] = 48;
    sckPin["default"] = 6;
    
    JsonObject mclkPin = i2sPins.createNestedObject("i2s_mclk_pin");
    mclkPin["type"] = "number";
    mclkPin["title"] = "I2S MCLK Pin";
    mclkPin["description"] = "I2S Master Clock pin (optional, use -1 to disable)";
    mclkPin["minimum"] = -1;
    mclkPin["maximum"] = 48;
    mclkPin["default"] = -1;
  }

private:
  // Initialize I2S with configurable pins
  bool initializeI2S() {
#ifdef ARDUINO_ARCH_ESP32
    // Check if pins are valid
    if (i2sSdPin < 0 || i2sWsPin < 0 || i2sSckPin < 0) {
      return false;
    }
    
    // Allocate pins through WLED's pin manager
    if (!PinManager::allocatePin(i2sSdPin, false, PinOwner::UM_LED_Controller) ||
        !PinManager::allocatePin(i2sWsPin, true, PinOwner::UM_LED_Controller) ||
        !PinManager::allocatePin(i2sSckPin, true, PinOwner::UM_LED_Controller)) {
      return false;
    }
    
    // Allocate MCLK pin if specified
    if (i2sMclkPin >= 0) {
      if (!PinManager::allocatePin(i2sMclkPin, true, PinOwner::UM_LED_Controller)) {
        return false;
      }
    }
    
    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = SAMPLE_BUFFER,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
      .bck_io_num = i2sSckPin,
      .ws_io_num = i2sWsPin,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = i2sSdPin
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
      return false;
    }
    
    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
      return false;
    }
    
    return true;
#else
    return false;
#endif
  }

  void readAudioSamples() {
#ifdef ARDUINO_ARCH_ESP32
    if (!i2sInitialized) return;
    
    size_t bytes_read = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes_read, portMAX_DELAY);
    if (result != ESP_OK) {
      return;
    }
#endif
  }

  // Handle button press and debounce logic
  void handleButton() {
    static bool buttonWasPressed = false;
    static unsigned long lastDebounceTime = 0;
    static bool lastButtonState = true;
    static bool sleepTriggered = false;
    
    // Read the current button state (assuming button is connected to a defined pin)
    bool currentButtonState = !digitalRead(BTN_PIN);  // Active LOW
    
    // If the button state changed, reset debounce timer
    if (currentButtonState != lastButtonState) {
      lastDebounceTime = millis();
    }
    
    // Only act on button state if it's been stable for debounce period
    if ((millis() - lastDebounceTime) > DEBOUNCE_TIME) {
      // Button press start
      if (currentButtonState && !buttonWasPressed) {
        buttonPressStart = millis();
        buttonWasPressed = true;
        sleepTriggered = false;
        isFlashingOrange = false;
      }
      
      // While button is pressed, check duration
      if (currentButtonState && buttonWasPressed) {
        unsigned long pressDuration = millis() - buttonPressStart;
        if (pressDuration > SLEEP_PRESS_TIME && !sleepTriggered) {
          // Trigger sleep mode
          strip.setBrightness(0);
          strip.show();
          isActive = false;
          lastMode = currentMode;
          lastColor = currentColor;
          lastPattern = currentPattern;
          strip.fill(0, 0, strip.getLengthTotal());
          strip.show();
          sleepTriggered = true;
        }
        else if (pressDuration > LONG_PRESS_TIME && pressDuration <= SLEEP_PRESS_TIME) {
          flashOrange();
        }
      }
      
      // Button release
      if (!currentButtonState && buttonWasPressed) {
        unsigned long pressDuration = millis() - buttonPressStart;
        if (pressDuration >= DEBOUNCE_TIME) {
          isFlashingOrange = false;
          if (!sleepTriggered) {
            handleButtonPress(pressDuration);
          }
        }
        buttonWasPressed = false;
      }
    }
    
    lastButtonState = currentButtonState;
  }

  // Handle different button press durations
  void handleButtonPress(unsigned long duration) {
    if (!isActive) {
      // Device is waking up
      if (duration < QUICK_PRESS_TIME) {
        // Quick press - go to last known state
        isActive = true;
        currentMode = lastMode;
        currentColor = lastColor;
        currentPattern = lastPattern;
        strip.setBrightness(255);
        updateLEDs();
      } else if (duration >= LONG_PRESS_TIME) {
        // Long press - enter brightness selection mode
        enterBrightnessSelection();
      }
    } else {
      // Device is active
      if (duration < QUICK_PRESS_TIME) {
        // Quick press - change color or pattern
        if (currentMode == COLOR_SELECT) {
          currentColor = static_cast<Colors>((currentColor + 1) % COLOR_COUNT);
          
          // If we just entered WLED_MODE, enable WLED control
          if (currentColor == WLED_MODE && !wledControlEnabled) {
            enableWLEDControl();
            wledControlEnabled = true;
            return;
          }
          
          // If we just left WLED_MODE or entered any custom color, disable WLED control
          if ((lastColor == WLED_MODE && wledControlEnabled) || 
              (currentColor != WLED_MODE && wledControlEnabled)) {
            disableWLEDControl();
            wledControlEnabled = false;
          }
          
          updateColorMode();
        } else if (currentMode == PATTERN_SELECT) {
          currentPattern = static_cast<Patterns>((currentPattern + 1) % PATTERN_COUNT);
          position = 0;
          lastPatternUpdate = 0;
          
          // Disable WLED control when entering pattern mode
          if (wledControlEnabled) {
            disableWLEDControl();
            wledControlEnabled = false;
          }
          
          updatePatternMode();
        }
      } else if (duration >= LONG_PRESS_TIME && duration < SLEEP_PRESS_TIME) {
        // Long press - cycle through modes
        if (currentMode == COLOR_SELECT) {
          currentMode = PATTERN_SELECT;
          currentPattern = UNIFORM_BLINK;
          position = 0;
          lastPatternUpdate = 0;
          
          // Disable WLED control when switching to pattern mode
          if (wledControlEnabled) {
            disableWLEDControl();
            wledControlEnabled = false;
          }
          
          updatePatternMode();
        } else if (currentMode == PATTERN_SELECT) {
          currentMode = COLOR_SELECT;
          
          // Disable WLED control when switching to color mode (unless we're in WLED_MODE)
          if (wledControlEnabled && currentColor != WLED_MODE) {
            disableWLEDControl();
            wledControlEnabled = false;
          }
          
          updateColorMode();
        }
      } else if (duration >= SLEEP_PRESS_TIME) {
        // Enter sleep mode
        strip.setBrightness(0);
        strip.show();
        isActive = false;
        lastMode = currentMode;
        lastColor = currentColor;
        lastPattern = currentPattern;
        strip.fill(0, 0, strip.getLengthTotal());
        strip.show();
      }
    }
    buttonPressStart = 0;
  }

  // Enter brightness selection mode
  void enterBrightnessSelection() {
    inBrightnessSelection = true;
    bool isExitFlashing = false;
    
    // Set LEDs to pure white at current brightness
    strip.setBrightness(255);
    setAllLEDs(strip.Color(maxBrightness, maxBrightness, maxBrightness));
    
    while (inBrightnessSelection) {
      bool currentButtonState = !digitalRead(BTN_PIN);
      static bool lastButtonState = HIGH;
      static unsigned long lastDebounceTime = 0;
      static bool buttonWasPressed = false;

      if (currentButtonState != lastButtonState) {
        lastDebounceTime = millis();
      }

      if ((millis() - lastDebounceTime) > 50) {
        if (currentButtonState && !buttonWasPressed) {
          buttonPressStart = millis();
          buttonWasPressed = true;
          isExitFlashing = false;
        }

        if (currentButtonState && buttonWasPressed) {
          unsigned long pressDuration = millis() - buttonPressStart;
          if (pressDuration > LONG_PRESS_TIME && !isExitFlashing) {
            setAllLEDs(strip.Color(maxBrightness, maxBrightness, 0));
            isExitFlashing = true;
          }
        }

        if (!currentButtonState && buttonWasPressed) {
          unsigned long pressDuration = millis() - buttonPressStart;
          if (pressDuration >= 50) {
            if (pressDuration < QUICK_PRESS_TIME) {
              cycleBrightness();
              setAllLEDs(strip.Color(maxBrightness, maxBrightness, maxBrightness));
            } else if (pressDuration >= LONG_PRESS_TIME) {
              inBrightnessSelection = false;
              isActive = true;
              currentMode = lastMode;
              currentColor = lastColor;
              currentPattern = lastPattern;
              updateLEDs();
            }
          }
          buttonWasPressed = false;
        }
      }
      lastButtonState = currentButtonState;
    }
  }

  // Update LEDs based on the current mode
  void updateLEDs() {
    if (!isActive || isFlashingOrange) return;

    if (currentMode == COLOR_SELECT) {
      updateColorMode();
    } else {
      updatePatternMode();
    }
  }

  // Update LEDs in color mode - modify WLED's color instead of direct control
  void updateColorMode() {
    if (currentColor == WLED_MODE) {
      // Let WLED take control - don't interfere with WLED's normal operation
      return;
    } else if (currentColor == RAINBOW) {
      // For rainbow, we need to use direct control since WLED doesn't have a simple rainbow mode
      setRainbowColors();
    } else if (currentColor == CYCLE) {
      // For cycle, we need to use direct control for smooth transitions
      setCycleColors();
    } else {
      // For solid colors, we can use direct control or modify WLED's state
      setAllLEDs(getCurrentColor());
    }
  }

  // Set each LED to a different color for cycle mode
  void setCycleColors() {
    static const uint32_t cycleColors[] = {
      strip.Color(0, maxBrightness, 0),    // Red
      strip.Color(maxBrightness*100/255, maxBrightness, 0),  // Orange
      strip.Color(maxBrightness*165/255, maxBrightness, 0),  // Yellow
      strip.Color(maxBrightness, maxBrightness/2, 0),  // Lime
      strip.Color(maxBrightness, 0, 0),    // Green
      strip.Color(maxBrightness/2, 0, maxBrightness),  // Spring Green
      strip.Color(maxBrightness, 0, maxBrightness),  // Cyan
      strip.Color(0, maxBrightness/2, maxBrightness),  // Azure
      strip.Color(0, 0, maxBrightness),    // Blue
      strip.Color(0, maxBrightness, maxBrightness/2),  // Violet
      strip.Color(0, maxBrightness, maxBrightness),  // Magenta
      strip.Color(0, maxBrightness, maxBrightness/2)   // Pink
    };

    int numColors = sizeof(cycleColors) / sizeof(cycleColors[0]);
    unsigned long currentTime = millis();
    int cycleIndex = (currentTime / COLOR_CYCLE_INTERVAL) % numColors;
    float fraction = (currentTime % COLOR_CYCLE_INTERVAL) / (float)COLOR_CYCLE_INTERVAL;

    uint32_t currentColor = interpolateColor(cycleColors[cycleIndex], cycleColors[(cycleIndex + 1) % numColors], fraction);

    for (int i = 0; i < strip.getLengthTotal(); i++) {
      strip.setPixelColor(i, currentColor);
    }
    strip.show();
  }

  // Set each LED to a different color for rainbow mode
  void setRainbowColors() {
    static const uint32_t rainbowColors[] = {
      strip.Color(0, maxBrightness, 0),    // Red
      strip.Color(maxBrightness*100/255, maxBrightness, 0),  // Orange
      strip.Color(maxBrightness*165/255, maxBrightness, 0),  // Yellow
      strip.Color(maxBrightness, maxBrightness/2, 0),  // Lime
      strip.Color(maxBrightness, 0, 0),    // Green
      strip.Color(maxBrightness/2, 0, maxBrightness),  // Spring Green
      strip.Color(maxBrightness, 0, maxBrightness),  // Cyan
      strip.Color(0, 0, maxBrightness),    // Blue
      strip.Color(0, maxBrightness/2, maxBrightness),  // Violet
      strip.Color(0, maxBrightness, maxBrightness/2)   // Pink
    };

    for (int i = 0; i < strip.getLengthTotal(); i++) {
      strip.setPixelColor(i, rainbowColors[i % 10]);
    }
    strip.show();
  }

  // Update LEDs in pattern mode - use direct control for custom patterns
  void updatePatternMode() {
    if (millis() - lastPatternUpdate < PATTERN_INTERVAL) return;
    lastPatternUpdate = millis();
    
    switch (currentPattern) {
      case UNIFORM_BLINK:
        updateUniformBlink();
        break;
      case CHASER:
        updateChaser();
        break;
      case MULTIPLE_CHASER:
        updateMultipleChaser();
        break;
      case RANDOM_BLINK:
        updateRandomBlink();
        break;
      case SOUND_REACTIVE_PULSING:
        updateSoundReactivePulsing();
        break;
      case SOUND_REACTIVE_CLOCKWISE:
        updateSoundReactiveClockwise();
        break;
      case SOUND_REACTIVE_RANDOM:
        updateSoundReactiveRandom();
        break;
    }
  }

  // Pattern implementation functions
  void updateUniformBlink() {
    static bool blinkState = false;
    static unsigned long lastBlinkTime = 0;

    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      blinkState = !blinkState;
      if (blinkState) {
        for (int i = 0; i < strip.getLengthTotal(); i++) {
          strip.setPixelColor(i, getCurrentColor(i));
        }
      } else {
        strip.clear();
      }
      strip.show();
      lastBlinkTime = millis();
    }
  }

  void updateChaser() {
    strip.clear();
    
    for (int i = 0; i < 3; i++) {
      strip.setPixelColor((position + i) % strip.getLengthTotal(), getCurrentColor((position + i) % strip.getLengthTotal()));
    }
    
    strip.show();
    position = (position + 1) % strip.getLengthTotal();
  }

  void updateRandomBlink() {
    static unsigned long lastBlinkTime = 0;
    static int lastRandomLed = -1;

    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      strip.clear();
      int randomLed;
      do {
        randomLed = random(strip.getLengthTotal());
      } while (randomLed == lastRandomLed);

      strip.setPixelColor(randomLed, getCurrentColor(randomLed));
      strip.show();
      lastRandomLed = randomLed;
      lastBlinkTime = millis();
    }
  }

  void updateMultipleChaser() {
    static bool oddPhase = true;
    static unsigned long lastSwitchTime = 0;

    if (millis() - lastSwitchTime >= CHASE_INTERVAL) {
      strip.clear();
      
      for (int i = 0; i < strip.getLengthTotal(); i++) {
        if ((i % 2 == 0) != oddPhase) {
          strip.setPixelColor(i, getCurrentColor(i));
        }
      }
      
      strip.show();
      oddPhase = !oddPhase;
      lastSwitchTime = millis();
    }
  }

  // Sound reactive patterns still need direct control for audio responsiveness
  void updateSoundReactivePulsing() {
    readAudioSamples();
    
    float sum = 0;
    int samples_read = SAMPLE_BUFFER;
    
    for (int i = 0; i < samples_read; i++) {
      sum += abs(samples[i]);
    }
    
    float average = sum / samples_read;
    float normalized = constrain(average / PULSING_DIVISOR * GAIN_FACTOR, 0, 1);
    smoothedBrightness = (0.8 * normalized) + (0.2 * smoothedBrightness);
    
    uint8_t brightness = BASE_BRIGHTNESS + (smoothedBrightness * (maxBrightness - BASE_BRIGHTNESS));
    
    // For sound reactive, we need direct control for responsiveness
    for (int i = 0; i < strip.getLengthTotal(); i++) {
      uint32_t color = getCurrentColor(i);
      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;

      r = (r * brightness) / 255;
      g = (g * brightness) / 255;
      b = (b * brightness) / 255;

      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
  }

  void updateSoundReactiveClockwise() {
    readAudioSamples();
    
    float sum = 0;
    int samples_read = SAMPLE_BUFFER;
    
    for (int i = 0; i < samples_read; i++) {
      sum += abs(samples[i]);
    }
    
    float average = sum / samples_read;
    int numLedsToLight = map(average, 0, CLOCKWISE_SENSITIVITY, 0, strip.getLengthTotal());

    strip.clear();
    
    // Light up LEDs starting from positions 1 and 6
    strip.setPixelColor(1 % strip.getLengthTotal(), getCurrentColor(1));
    strip.setPixelColor(6 % strip.getLengthTotal(), getCurrentColor(6));

    for (int i = 0; i <= numLedsToLight; i++) {
      strip.setPixelColor((1 + i) % strip.getLengthTotal(), getCurrentColor((1 + i) % strip.getLengthTotal()));
      strip.setPixelColor((6 + i) % strip.getLengthTotal(), getCurrentColor((6 + i) % strip.getLengthTotal()));
    }
    strip.show();
  }

  void updateSoundReactiveRandom() {
    static int lastRandomLed = -1;
    readAudioSamples();
    
    float sum = 0;
    int samples_read = SAMPLE_BUFFER;
    
    for (int i = 0; i < samples_read; i++) {
      sum += abs(samples[i]);
    }
    
    float average = sum / samples_read;

    if (average > SOUND_THRESHOLD) {
      int randomLed;
      do {
        randomLed = random(strip.getLengthTotal());
      } while (randomLed == lastRandomLed);
      
      if (lastRandomLed != -1) {
        strip.setPixelColor(lastRandomLed, 0);
      }
      
      strip.setPixelColor(randomLed, getCurrentColor(randomLed));
      lastRandomLed = randomLed;
    }
    strip.show();
  }

  // Helper functions
  void cycleBrightness() {
    currentBrightnessLevel = (currentBrightnessLevel + 1) % 4;

    switch (currentBrightnessLevel) {
      case 0:
        maxBrightness = BRIGHTNESS_25;
        break;
      case 1:
        maxBrightness = BRIGHTNESS_50;
        break;
      case 2:
        maxBrightness = BRIGHTNESS_75;
        break;
      case 3:
        maxBrightness = BRIGHTNESS_100;
        break;
    }
    strip.setBrightness(255);
    strip.show();
  }

  void flashOrange() {
    isFlashingOrange = true;
    setAllLEDs(strip.Color(maxBrightness, maxBrightness, 0));
  }

  void setAllLEDs(uint32_t color) {
    strip.fill(color, 0, strip.getLengthTotal());
    strip.show();
  }

  uint32_t getCurrentColor(int ledIndex = 0) {
    if (currentColor == WLED_MODE) {
      // In WLED mode, return a neutral color (we shouldn't be calling this)
      return strip.Color(0, 0, 0);
    } else if (currentColor == RAINBOW) {
      static const uint32_t rainbowColors[] = {
        strip.Color(0, maxBrightness, 0),    // Red
        strip.Color(maxBrightness*100/255, maxBrightness, 0),  // Orange
        strip.Color(maxBrightness*165/255, maxBrightness, 0),  // Yellow
        strip.Color(maxBrightness, maxBrightness/2, 0),  // Lime
        strip.Color(maxBrightness, 0, 0),    // Green
        strip.Color(maxBrightness/2, 0, maxBrightness),  // Spring Green
        strip.Color(maxBrightness, 0, maxBrightness),  // Cyan
        strip.Color(0, 0, maxBrightness),    // Blue
        strip.Color(0, maxBrightness/2, maxBrightness),  // Violet
        strip.Color(0, maxBrightness, maxBrightness/2)   // Pink
      };
      return rainbowColors[ledIndex % 10];
    } else if (currentColor == CYCLE) {
      static const uint32_t cycleColors[] = {
        strip.Color(0, maxBrightness, 0),    // Red
        strip.Color(maxBrightness*100/255, maxBrightness, 0),  // Orange
        strip.Color(maxBrightness*165/255, maxBrightness, 0),  // Yellow
        strip.Color(maxBrightness, maxBrightness/2, 0),  // Lime
        strip.Color(maxBrightness, 0, 0),    // Green
        strip.Color(maxBrightness/2, 0, maxBrightness),  // Spring Green
        strip.Color(maxBrightness, 0, maxBrightness),  // Cyan
        strip.Color(0, maxBrightness/2, maxBrightness),  // Azure
        strip.Color(0, 0, maxBrightness),    // Blue
        strip.Color(0, maxBrightness, maxBrightness/2),  // Violet
        strip.Color(0, maxBrightness, maxBrightness),  // Magenta
        strip.Color(0, maxBrightness, maxBrightness/2)   // Pink
      };
      int numColors = sizeof(cycleColors) / sizeof(cycleColors[0]);
      unsigned long currentTime = millis();
      int cycleIndex = (currentTime / COLOR_CYCLE_INTERVAL) % numColors;
      float fraction = (currentTime % COLOR_CYCLE_INTERVAL) / (float)COLOR_CYCLE_INTERVAL;
      return interpolateColor(cycleColors[cycleIndex], cycleColors[(cycleIndex + 1) % numColors], fraction);
    } else {
      switch (currentColor) {
        case WHITE:   return strip.Color(maxBrightness, maxBrightness, maxBrightness);
        case RED:     return strip.Color(0, maxBrightness, 0);
        case ORANGE:  return strip.Color(maxBrightness*100/255, maxBrightness, 0);
        case YELLOW:  return strip.Color(maxBrightness*165/255, maxBrightness, 0);
        case GREEN:   return strip.Color(maxBrightness, 0, 0);
        case BLUE:    return strip.Color(0, 0, maxBrightness);
        case PINK:    return strip.Color(0, maxBrightness, maxBrightness/2);
        case PURPLE:  return strip.Color(0, maxBrightness/2, maxBrightness/2);
        default:      return strip.Color(maxBrightness, maxBrightness, maxBrightness);
      }
    }
  }

  uint32_t interpolateColor(uint32_t color1, uint32_t color2, float fraction) {
    uint8_t g1 = (color1 >> 16) & 0xFF;
    uint8_t r1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;

    uint8_t g2 = (color2 >> 16) & 0xFF;
    uint8_t r2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;

    uint8_t g = g1 + (g2 - g1) * fraction;
    uint8_t r = r1 + (r2 - r1) * fraction;
    uint8_t b = b1 + (b2 - b1) * fraction;

    return strip.Color(g, r, b);
  }

  // Enable WLED control by setting appropriate segment properties
  void enableWLEDControl() {
    // Get the main segment and set it to static mode so WLED can control it
    Segment& mainSeg = strip.getMainSegment();
    if (mainSeg.mode != FX_MODE_STATIC) {
      mainSeg.setMode(FX_MODE_STATIC);
      stateChanged = true;
    }
    
    // Don't interfere with WLED's normal operation
    // WLED will handle all color and effect changes through the web interface
  }

  // Disable WLED control by taking over the segment
  void disableWLEDControl() {
    // Set the main segment to a mode that we can easily override
    Segment& mainSeg = strip.getMainSegment();
    if (mainSeg.mode != FX_MODE_STATIC) {
      mainSeg.setMode(FX_MODE_STATIC);
      stateChanged = true;
    }
    
    // Clear any existing colors to start fresh
    mainSeg.clear();
    
    // Force an immediate update to ensure WLED's effects are cleared
    strip.trigger();
    
    // Now our custom modes will take full control through direct pixel manipulation
  }
};

// Static member definitions
const char LEDControllerUsermod::_name[] PROGMEM = "LED_Controller";
const char LEDControllerUsermod::_enabled[] PROGMEM = "enabled";
const char LEDControllerUsermod::_i2sPins[] PROGMEM = "i2s_pins";
const char LEDControllerUsermod::_i2sSdPin[] PROGMEM = "i2s_sd_pin";
const char LEDControllerUsermod::_i2sWsPin[] PROGMEM = "i2s_ws_pin";
const char LEDControllerUsermod::_i2sSckPin[] PROGMEM = "i2s_sck_pin";
const char LEDControllerUsermod::_i2sMclkPin[] PROGMEM = "i2s_mclk_pin";

int LEDControllerUsermod::position = 0;
bool LEDControllerUsermod::buttonWasPressed = false;
unsigned long LEDControllerUsermod::lastDebounceTime = 0;
bool LEDControllerUsermod::lastButtonState = true;  // HIGH = true
bool LEDControllerUsermod::sleepTriggered = false;

// Register the usermod
REGISTER_USERMOD(LEDControllerUsermod); 