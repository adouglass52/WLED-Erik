# GPIO Control Usermod

This usermod provides automatic GPIO control functionality for WLED. It automatically sets GPIO33 to HIGH when the system is on and monitors GPIO35 for a shutdown trigger.

## Features

- **Automatic Output Control**: GPIO33 is automatically set to HIGH when the system starts
- **Shutdown Monitoring**: Monitors GPIO35 for a shutdown trigger
- **Debounced Input**: Requires GPIO35 to be held LOW for 5 seconds to trigger shutdown
- **Configurable**: Pin assignments and timing can be configured through the WLED interface
- **State Persistence**: Settings are saved to configuration and persist across reboots

## Hardware Setup

### Required Connections

1. **GPIO33 (Output)**: Connect to your external circuit that needs to know when the system is on
2. **GPIO35 (Input)**: Connect to a button or switch that will trigger shutdown when held

### Wiring Example

```
ESP32 Board:
├── GPIO33 ──→ External Circuit (HIGH when system on)
└── GPIO35 ←── Button/Switch (pull to GND to trigger shutdown)
```

## Configuration

The usermod can be configured through the WLED web interface:

1. Go to **Config** → **Sync Interfaces** → **Usermods**
2. Find "GPIO Control" in the list
3. Configure the following settings:
   - **Enabled**: Enable/disable the usermod
   - **Output Pin**: GPIO pin for output (default: 33)
   - **Input Pin**: GPIO pin for input (default: 35)
   - **Shutdown Delay**: Time in milliseconds to hold input LOW before triggering shutdown (default: 5000ms)

## Operation

### Normal Operation
- When WLED starts, GPIO33 is automatically set to HIGH
- GPIO35 is monitored continuously for input changes
- The system remains active as long as GPIO35 is HIGH or not held LOW for the configured duration

### Shutdown Trigger
- When GPIO35 is pulled LOW, a timer starts
- If GPIO35 remains LOW for the configured delay (default 5 seconds), shutdown is triggered
- When shutdown is triggered, GPIO33 is set to LOW
- If GPIO35 is released before the delay expires, the timer resets

### Recovery
- When GPIO35 is released (goes HIGH), the shutdown state is reset
- GPIO33 is set back to HIGH, indicating the system is active again

## API Integration

The usermod exposes its state through the WLED JSON API:

```json
{
  "GPIO_Control": {
    "enabled": true,
    "outputPin": 33,
    "inputPin": 35,
    "shutdownDelay": 5000,
    "shutdownTriggered": false
  }
}
```

## Debug Information

When debug mode is enabled, the usermod will output status messages to the serial console:

- `GPIO Control Usermod initialized` - When the usermod starts
- `GPIO35 pulled LOW - starting shutdown timer` - When shutdown sequence begins
- `GPIO35 held LOW for 5 seconds - triggering shutdown` - When shutdown is triggered
- `GPIO35 released - resetting shutdown timer` - When shutdown sequence is cancelled

## Installation

1. Copy the `GPIO_Control` folder to your WLED `usermods` directory
2. Add the following to your `platformio_override.ini`:

```ini
[env:your_board]
build_flags = ${common.build_flags} ${esp32.build_flags}
  -D USERMOD_GPIO_CONTROL
```

3. Rebuild and flash WLED to your device
4. Configure the usermod through the web interface

## Troubleshooting

### GPIO33 not changing state
- Check that the usermod is enabled in the configuration
- Verify the GPIO pin is not being used by another function
- Check the serial console for debug messages

### Shutdown not triggering
- Ensure GPIO35 is properly connected and pulled to GND when pressed
- Check that the shutdown delay is not set too high
- Verify the input pin configuration matches your hardware

### System not recovering after shutdown
- Check that GPIO35 is properly released (not stuck LOW)
- Verify the input pin has proper pull-up resistance
- Check the serial console for any error messages

## Customization

You can modify the usermod to add additional functionality:

- **MQTT Integration**: Add MQTT messages when shutdown is triggered
- **Additional Outputs**: Control multiple GPIO pins based on system state
- **Different Triggers**: Add other conditions for shutdown (e.g., temperature, time-based)
- **State Persistence**: Save the shutdown state to flash memory

## License

This usermod is provided as-is for educational and personal use. Modify as needed for your specific requirements. 