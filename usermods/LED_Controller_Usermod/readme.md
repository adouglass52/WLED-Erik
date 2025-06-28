# LED Controller Usermod

A comprehensive LED control system for WLED that provides advanced color selection, pattern modes, and sound reactive functionality.

## Features

### Color Selection
- **Solid Colors**: White, Red, Orange, Yellow, Green, Blue, Pink, Purple
- **Dynamic Colors**: Rainbow (each LED different color), Cycle (smooth color transitions)

### Pattern Modes
- **Uniform Blink**: All LEDs blink together
- **Chaser**: Single light moving around the strip
- **Multiple Chaser**: Alternating odd/even LEDs
- **Random Blink**: Random LED lights up
- **Sound Reactive Pulsing**: Brightness responds to audio
- **Sound Reactive Clockwise**: LEDs light up clockwise based on sound
- **Sound Reactive Random**: Random LEDs respond to audio

### Button Control
- **Quick Press**: Change color or pattern
- **Long Press**: Switch between Color/Pattern/Brightness modes
- **Very Long Press**: Enter sleep mode
- **Long Press when off**: Enter brightness selection mode

### Brightness Control
- 4 brightness levels: 25%, 50%, 75%, 100%
- Accessible through button or web interface

### Sound Reactive Features
- Uses I2S microphone input
- Configurable sensitivity and smoothing
- Works with digital microphones (INMP441, etc.)

## Hardware Requirements

### Required Pins
- **I2S Microphone**: Configurable through WLED web interface
  - SD (Data): Configurable (default: GPIO 5)
  - SCK (Clock): Configurable (default: GPIO 6)  
  - WS (Word Select): Configurable (default: GPIO 4)
  - MCLK (Master Clock): Optional, configurable (default: -1, disabled)
- **Button**: Configured through WLED button settings

### Supported Hardware
- ESP32 (recommended for sound reactive features)
- Digital I2S microphones (INMP441, ICS-43434, etc.)
- WS2812B LED strips

## Installation

1. Copy the `LED_Controller_Usermod` folder to your WLED `usermods` directory
2. Add `LED_Controller_Usermod` to your `custom_usermods` in `platformio_override.ini`:
   ```ini
   custom_usermods = LED_Controller_Usermod
   ```
3. Compile and upload WLED

## Configuration

### Web Interface
The LED Controller usermod can be configured through the WLED web interface:

1. Go to **Config** → **Sync Interfaces** → **Usermods**
2. Find the **LED Controller** section
3. Configure the following settings:
   - **Enable LED Controller**: Enable/disable the usermod
   - **I2S Pin Configuration**:
     - **I2S SD Pin**: Data pin for I2S microphone (default: 5)
     - **I2S WS Pin**: Word Select pin for I2S microphone (default: 4)
     - **I2S SCK Pin**: Clock pin for I2S microphone (default: 6)
     - **I2S MCLK Pin**: Master Clock pin (optional, default: -1)

### Button Configuration
Configure your button through WLED's button settings:
1. Go to **Config** → **Sync Interfaces** → **Hardware Setup**
2. Configure button pin and behavior
3. The LED Controller will automatically use the configured button

### API Control
The usermod can be controlled via WLED's JSON API:

```json
{
  "LED_Controller": {
    "enabled": true,
    "mode": 0,
    "color": 0,
    "pattern": 0,
    "active": true,
    "brightness": 128
  }
}
```

## Usage

### Button Operation
- **Quick Press**: Cycles through colors (in color mode) or patterns (in pattern mode)
- **Long Press**: Switches between color selection and pattern selection modes
- **Very Long Press**: Enters sleep mode (turns off LEDs)
- **Long Press when off**: Enters brightness selection mode

### Web Interface Control
- Use the WLED web interface to control colors, patterns, and brightness
- Sound reactive patterns will automatically respond to audio input
- All settings are saved and restored on power-up

### Sound Reactive Patterns
The usermod includes three sound reactive patterns:
1. **Pulsing**: Brightness of all LEDs responds to audio level
2. **Clockwise**: LEDs light up clockwise from positions 1 and 6 based on sound
3. **Random**: Random LEDs light up in response to audio peaks

## Troubleshooting

### I2S Not Working
- Check that I2S pins are correctly configured in the web interface
- Ensure microphone is properly connected
- Verify microphone type is compatible (INMP441, ICS-43434, etc.)
- Check that pins are not conflicting with other hardware

### Button Not Responding
- Verify button is configured in WLED's hardware setup
- Check button wiring and pull-up/pull-down configuration
- Ensure button pin is not conflicting with other hardware

### Sound Reactive Patterns Not Working
- Verify I2S pins are correctly configured
- Check microphone connections
- Ensure microphone is powered correctly
- Try adjusting sensitivity settings in the code if needed

## Advanced Configuration

### Compile-Time Options
You can modify the following constants in the source code for advanced customization:

```cpp
// Timing settings
#define QUICK_PRESS_TIME   500    // Quick press threshold
#define LONG_PRESS_TIME    1000   // Long press threshold
#define SLEEP_PRESS_TIME   3000   // Sleep mode threshold

// Sound reactive settings
#define GAIN_FACTOR        4.0    // Audio amplification
#define SOUND_THRESHOLD    2000000 // Sound detection threshold
#define PULSING_DIVISOR    25000000.0  // Pulsing sensitivity
#define CLOCKWISE_SENSITIVITY 50000000  // Clockwise sensitivity
```

### Custom Patterns
You can add custom patterns by:
1. Adding new pattern types to the `Patterns` enum
2. Implementing pattern functions
3. Adding pattern handling in `updatePatternMode()`

## License
This usermod is provided as-is for educational and personal use. 