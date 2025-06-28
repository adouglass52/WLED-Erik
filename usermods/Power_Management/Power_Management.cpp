#include "wled.h"
#include "GPIO_Control.h"

// Global instance
GPIOControlUsermod* gpioControlUsermod = nullptr;

/*
 * Power Management Usermod
 * 
 * This usermod provides comprehensive power management for battery-powered WLED systems:
 * - GPIO33 output control based on system state
 * - GPIO35 input monitoring with 5-second debounce for manual shutdown
 * - Battery voltage monitoring with automatic shutdown on low battery
 * - Keep alive functionality to prevent system shutdown during activity
 * - Automatic shutdown trigger when GPIO35 is held LOW for 5+ seconds
 */

class PowerManagementUsermod : public Usermod {

  private:
    // Private class members
    bool enabled = true;
    bool initDone = false;
    
    // GPIO pins
    const int OUTPUT_PIN = 33;  // GPIO33 - output pin
    const int INPUT_PIN = 35;   // GPIO35 - input pin for shutdown trigger
    const int VBAT_PIN = 7;     // GPIO7 - battery voltage pin
    const int KEEP_ALIVE_PIN = 1; // GPIO1 - Keep alive pin
    
    // Battery monitoring constants
    const int ADC_MAX_VALUE = 4095;     // Maximum value for 12-bit ADC
    const float ADC_REF_VOLTAGE = 3.3;  // Reference voltage for the ADC
    const float ACTUAL_R1 = 20000.0;    // Actual measured value of R1
    const float ACTUAL_R2 = 10000.0;    // Actual measured value of R2
    const float VOLTAGE_DIVIDER_RATIO = (ACTUAL_R1 / (ACTUAL_R1 + ACTUAL_R2));
    const float CALIBRATION_SLOPE = 0.878;
    const float CALIBRATION_INTERCEPT = -0.010;
    const float LOW_BATTERY_THRESHOLD = 3.2; // Voltage threshold for low battery shutdown
    
    // Keep alive configuration
    const unsigned long KEEP_ALIVE_TIMEOUT = 360000; // 1 minute in milliseconds
    unsigned long lastActivityTime = 0;  // Timer for keep alive functionality
    
    // Timing variables
    unsigned long lastInputCheck = 0;
    unsigned long inputCheckInterval = 100;  // Check every 100ms
    unsigned long shutdownDelay = 5000;      // 5 seconds in milliseconds
    unsigned long lastBatteryCheck = 0;
    unsigned long batteryCheckInterval = 10000; // Check battery every 10 seconds
    
    // State tracking
    bool lastInputState = HIGH;
    unsigned long inputLowStartTime = 0;
    bool shutdownTriggered = false;
    bool lowBatteryShutdown = false;
    
    // Battery monitoring
    float currentBatteryVoltage = 0.0;
    float lastBatteryVoltage = 0.0;
    
    // Configuration variables
    bool configEnabled = true;
    int configOutputPin = 33;
    int configInputPin = 35;
    int configVbatPin = 7;
    int configKeepAlivePin = 1;
    unsigned long configShutdownDelay = 5000;
    float configLowBatteryThreshold = 3.2;
    unsigned long configKeepAliveTimeout = 360000;
    
    // String constants
    static const char _name[];
    static const char _enabled[];
    static const char _outputPin[];
    static const char _inputPin[];
    static const char _vbatPin[];
    static const char _keepAlivePin[];
    static const char _shutdownDelay[];
    static const char _lowBatteryThreshold[];
    static const char _keepAliveTimeout[];

  public:
    
    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool enable) { enabled = enable; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() { return enabled; }

    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     */
    void setup() override {
      if (!enabled) return;
      
      // Configure GPIO pins
      pinMode(OUTPUT_PIN, OUTPUT);
      pinMode(INPUT_PIN, INPUT_PULLUP);
      pinMode(VBAT_PIN, INPUT);
      pinMode(KEEP_ALIVE_PIN, OUTPUT);
      
      // Initialize output pin states
      digitalWrite(OUTPUT_PIN, HIGH);  // Set to HIGH when system is on
      digitalWrite(KEEP_ALIVE_PIN, HIGH); // Set keep alive high on startup
      
      // Initialize keep alive timer
      lastActivityTime = millis();
      
      initDone = true;
      DEBUG_PRINTLN(F("Power Management Usermod initialized with battery monitoring"));
    }

    /*
     * connected() is called every time the WiFi is (re)connected
     */
    void connected() override {
      // Nothing needed here
    }

    /*
     * loop() is called continuously. Here we monitor the input pin.
     */
    void loop() override {
      if (!enabled || !initDone || strip.isUpdating()) return;

      // Check input pin periodically
      if (millis() - lastInputCheck > inputCheckInterval) {
        checkInputPin();
        lastInputCheck = millis();
      }
      
      // Check battery voltage periodically
      if (millis() - lastBatteryCheck > batteryCheckInterval) {
        checkBatteryVoltage();
        lastBatteryCheck = millis();
      }
      
      // Check keep alive timer
      if (millis() - lastActivityTime >= KEEP_ALIVE_TIMEOUT) {
        // Turn off keep alive pin to shut down the device
        digitalWrite(KEEP_ALIVE_PIN, LOW);
        // Set output pin to LOW
        digitalWrite(OUTPUT_PIN, LOW);
        DEBUG_PRINTLN(F("Keep alive timeout - shutting down system"));
        shutdownTriggered = true;
      }
    }

    /*
     * Check the input pin state and handle shutdown logic
     */
    void checkInputPin() {
      bool currentInputState = digitalRead(INPUT_PIN);
      
      // If input just went LOW, start timing
      if (currentInputState == LOW && lastInputState == HIGH) {
        inputLowStartTime = millis();
        DEBUG_PRINTLN(F("GPIO35 pulled LOW - starting shutdown timer"));
      }
      
      // If input is LOW and we haven't triggered shutdown yet
      if (currentInputState == LOW && !shutdownTriggered) {
        if (millis() - inputLowStartTime >= shutdownDelay) {
          // Trigger shutdown
          shutdownTriggered = true;
          digitalWrite(OUTPUT_PIN, LOW);  // Set output to LOW
          digitalWrite(KEEP_ALIVE_PIN, LOW); // Set keep alive to LOW
          DEBUG_PRINTLN(F("GPIO35 held LOW for 5 seconds - triggering shutdown"));
        }
      }
      
      // If input went HIGH, reset the timer
      if (currentInputState == HIGH && lastInputState == LOW) {
        inputLowStartTime = 0;
        shutdownTriggered = false;
        digitalWrite(OUTPUT_PIN, HIGH);  // Set output back to HIGH
        digitalWrite(KEEP_ALIVE_PIN, HIGH); // Set keep alive back to HIGH
        DEBUG_PRINTLN(F("GPIO35 released - resetting shutdown timer"));
      }
      
      lastInputState = currentInputState;
    }

    /*
     * Check battery voltage and handle low voltage shutdown
     */
    void checkBatteryVoltage() {
      int adcValue = analogRead(VBAT_PIN);
      float voltageAtPin = (adcValue / (float)ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
      float batteryVoltage = voltageAtPin / VOLTAGE_DIVIDER_RATIO;
      float calibratedVoltage = CALIBRATION_SLOPE * batteryVoltage + CALIBRATION_INTERCEPT;
      
      // Update current battery voltage
      currentBatteryVoltage = calibratedVoltage;
      
      // Check for low battery condition
      if (calibratedVoltage <= LOW_BATTERY_THRESHOLD && !lowBatteryShutdown) {
        lowBatteryShutdown = true;
        digitalWrite(OUTPUT_PIN, LOW);  // Set output to LOW
        digitalWrite(KEEP_ALIVE_PIN, LOW); // Set keep alive to LOW
        DEBUG_PRINTLN(F("Low battery detected - shutting down system"));
        DEBUG_PRINT(F("Battery voltage: "));
        DEBUG_PRINTLN(calibratedVoltage);
      }
      
      // Debug output every 10 seconds
      DEBUG_PRINT(F("ADC Value: "));
      DEBUG_PRINT(adcValue);
      DEBUG_PRINT(F(" Voltage at Pin: "));
      DEBUG_PRINT(voltageAtPin);
      DEBUG_PRINT(F(" Calibrated Battery Voltage: "));
      DEBUG_PRINTLN(calibratedVoltage);
    }

    /*
     * Update keep alive timer (call this when there's activity)
     */
    void updateKeepAliveTimer() {
      lastActivityTime = millis();
    }

    /*
     * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
     */
    void addToJsonInfo(JsonObject& root) override
    {
      if (!enabled) return;
      
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");

      JsonArray gpioInfo = user.createNestedArray(FPSTR(_name));
      gpioInfo.add(F("Power Management Active"));
      gpioInfo.add(F("Battery Monitoring Active"));
      gpioInfo.add(F("Keep Alive Active"));
    }

    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API.
     */
    void addToJsonState(JsonObject& root) override
    {
      if (!initDone || !enabled) return;

      JsonObject usermod = root[FPSTR(_name)];
      if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));

      usermod["enabled"] = configEnabled;
      usermod["outputPin"] = configOutputPin;
      usermod["inputPin"] = configInputPin;
      usermod["vbatPin"] = configVbatPin;
      usermod["keepAlivePin"] = configKeepAlivePin;
      usermod["shutdownDelay"] = configShutdownDelay;
      usermod["lowBatteryThreshold"] = configLowBatteryThreshold;
      usermod["keepAliveTimeout"] = configKeepAliveTimeout;
      usermod["shutdownTriggered"] = shutdownTriggered;
      usermod["lowBatteryShutdown"] = lowBatteryShutdown;
      usermod["batteryVoltage"] = currentBatteryVoltage;
      usermod["lastActivityTime"] = lastActivityTime;
    }

    /*
     * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API.
     */
    void readFromJsonState(JsonObject& root) override
    {
      if (!initDone) return;

      JsonObject usermod = root[FPSTR(_name)];
      if (!usermod.isNull()) {
        configEnabled = usermod["enabled"] | configEnabled;
        configOutputPin = usermod["outputPin"] | configOutputPin;
        configInputPin = usermod["inputPin"] | configInputPin;
        configVbatPin = usermod["vbatPin"] | configVbatPin;
        configKeepAlivePin = usermod["keepAlivePin"] | configKeepAlivePin;
        configShutdownDelay = usermod["shutdownDelay"] | configShutdownDelay;
        configLowBatteryThreshold = usermod["lowBatteryThreshold"] | configLowBatteryThreshold;
        configKeepAliveTimeout = usermod["keepAliveTimeout"] | configKeepAliveTimeout;
      }
    }

    /*
     * addToConfig() can be used to add custom persistent settings to the cfg.json file.
     */
    void addToConfig(JsonObject& root) override
    {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) top = root.createNestedObject(FPSTR(_name));

      top[FPSTR(_enabled)] = configEnabled;
      top[FPSTR(_outputPin)] = configOutputPin;
      top[FPSTR(_inputPin)] = configInputPin;
      top[FPSTR(_vbatPin)] = configVbatPin;
      top[FPSTR(_keepAlivePin)] = configKeepAlivePin;
      top[FPSTR(_shutdownDelay)] = configShutdownDelay;
      top[FPSTR(_lowBatteryThreshold)] = configLowBatteryThreshold;
      top[FPSTR(_keepAliveTimeout)] = configKeepAliveTimeout;

      DEBUG_PRINTLN(F("Power Management config saved."));
    }

    /*
     * readFromConfig() can be used to read back the custom settings you added with addToConfig().
     */
    bool readFromConfig(JsonObject& root) override
    {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) {
        DEBUG_PRINTLN(F("Power Management config not found. (Using defaults.)"));
        return false;
      }

      configEnabled = top[FPSTR(_enabled)] | configEnabled;
      configOutputPin = top[FPSTR(_outputPin)] | configOutputPin;
      configInputPin = top[FPSTR(_inputPin)] | configInputPin;
      configVbatPin = top[FPSTR(_vbatPin)] | configVbatPin;
      configKeepAlivePin = top[FPSTR(_keepAlivePin)] | configKeepAlivePin;
      configShutdownDelay = top[FPSTR(_shutdownDelay)] | configShutdownDelay;
      configLowBatteryThreshold = top[FPSTR(_lowBatteryThreshold)] | configLowBatteryThreshold;
      configKeepAliveTimeout = top[FPSTR(_keepAliveTimeout)] | configKeepAliveTimeout;

      DEBUG_PRINTLN(F("Power Management config loaded."));
      return true;
    }

    /*
     * getId() should always return the same value for your usermod.
     */
    uint16_t getId() override
    {
      return USERMOD_ID_POWER_MANAGEMENT;
    }
};

// Define the static strings
const char PowerManagementUsermod::_name[] PROGMEM = "Power_Management";
const char PowerManagementUsermod::_enabled[] PROGMEM = "enabled";
const char PowerManagementUsermod::_outputPin[] PROGMEM = "outputPin";
const char PowerManagementUsermod::_inputPin[] PROGMEM = "inputPin";
const char PowerManagementUsermod::_vbatPin[] PROGMEM = "vbatPin";
const char PowerManagementUsermod::_keepAlivePin[] PROGMEM = "keepAlivePin";
const char PowerManagementUsermod::_shutdownDelay[] PROGMEM = "shutdownDelay";
const char PowerManagementUsermod::_lowBatteryThreshold[] PROGMEM = "lowBatteryThreshold";
const char PowerManagementUsermod::_keepAliveTimeout[] PROGMEM = "keepAliveTimeout";

// Create global instance
PowerManagementUsermod power_management;

// Register the usermod
REGISTER_USERMOD(power_management); 