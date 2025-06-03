# PocKETlab I/O Library

A comprehensive hardware abstraction library for the PocKETlab board's ADC and DAC functionality.

## Overview

The PocKETlab I/O library provides a high-level interface for controlling the board's analog I/O capabilities:

- **MCP3202 ADC (U8)**: 2-channel 12-bit signal input measurement
- **MCP4822 DAC (U5)**: 2-channel 12-bit signal generation  
- **MCP4822 DAC (U6)**: 2-channel 12-bit power control
- **Feedback monitoring**: Voltage, current, and temperature sensing
- **Synchronized updates**: Simultaneous DAC output updates using LDAC

## Hardware Configuration

### Pin Assignments
- **SPI Bus**: MOSI (GPIO12), MISO (GPIO13), SCK (GPIO14)
- **Chip Selects**: 
  - Signal ADC: GPIO16
  - Signal DAC: GPIO18  
  - Power DAC: GPIO17
- **LDAC Control**: GPIO11 (for synchronized DAC updates)
- **Feedback Pins**: 
  - Power voltage: GPIO5 (FB_VOUT)
  - Power current: GPIO4 (FB_IOUT)
  - Ground voltage: GPIO3 (FB_GOUT)
  - Signal A feedback: GPIO1 (FB_AO)
  - Signal B feedback: GPIO2 (FB_A1)
  - Temperature: GPIO10 (TEMP_PROBE)

### Voltage Ranges
- **Signal ADC Input**: 0 - 3.3V (12-bit resolution)
- **Signal DAC Output**: 0 - 2.048V (12-bit resolution, 1x gain for safety)
- **Power Voltage Control**: 0 - 20V (via external scaling)
- **Power Current Control**: 0 - 3A (via external scaling)

## API Reference

### Initialization

```cpp
#include "pocketlab_io.h"

PocKETlabIO pocketlab;

void setup() {
    if (!pocketlab.begin()) {
        // Handle initialization error
    }
}
```

### Power Control

```cpp
// Set power output voltage (0-20V)
bool setPowerVoltage(float voltage);

// Set power current limit (0-3A)  
bool setPowerCurrent(float current);

// Read power feedback
float readPowerVoltage();    // Actual output voltage
float readPowerCurrent();    // Actual output current
float readGroundVoltage();   // Floating ground voltage
```

### Signal Generation and Measurement

```cpp
// Generate signal outputs - voltage is FINAL OUTPUT after 6.7x amplification
// Range: 0V to ~13.7V (limited by 2.048V DAC × 6.7 gain)
bool setSignalVoltage(SignalChannel channel, float voltage);

// Measure signal inputs (0-3.3V)
float readSignalVoltage(SignalChannel channel);

// Read DAC output feedback (BEFORE amplifier, 0-2.048V)
float readSignalFeedback(SignalChannel channel);

// Get expected amplified output voltage based on current DAC setting
float getExpectedSignalOutput(SignalChannel channel);

// Channel definitions
SIGNAL_CHANNEL_A  // Channel 0
SIGNAL_CHANNEL_B  // Channel 1
```

**Note**: The signal outputs pass through 6.7x operational amplifiers. When you call 
`setSignalVoltage()`, you specify the desired final output voltage. The library 
automatically compensates by setting the DAC to `voltage / 6.7`. The feedback 
readings (`readSignalFeedback()`) show the actual DAC voltage before amplification.

### Advanced Control

```cpp
// Synchronized DAC updates
void updateAllDACs();

// Raw ADC/DAC access
uint16_t readRawADC(uint8_t channel);
bool writeRawDAC(uint8_t dac, uint8_t channel, uint16_t value);

// Temperature monitoring
float readTemperature();

// Configuration
void setADCReference(float voltage);
void setDACReference(float voltage);
```

### Status and Diagnostics

```cpp
// Check initialization status
bool isInitialized();

// Print comprehensive status
void printStatus();

// Get configuration
float getADCReference();
float getDACReference(); 
float getPowerVoltageRange();
float getPowerCurrentRange();
```

## Usage Examples

### Basic Power Control

```cpp
#include "pocketlab_io.h"

PocKETlabIO pocketlab;

void setup() {
    Serial.begin(115200);
    pocketlab.begin();
    
    // Set 5V output with 1A current limit
    pocketlab.setPowerVoltage(5.0);
    pocketlab.setPowerCurrent(1.0);
    pocketlab.updateAllDACs();
}

void loop() {
    // Monitor actual output
    float voltage = pocketlab.readPowerVoltage();
    float current = pocketlab.readPowerCurrent();
    
    Serial.printf("Output: %.2fV, %.3fA\n", voltage, current);
    delay(1000);
}
```

### Signal Generation

```cpp
void generateSineWave() {
    float amplitude = 3.35;  // 3.35V amplitude (final output after 6.7x amplifier)
    float offset = 6.85;     // 6.85V offset (mid-scale of amplified range)
    
    for (int i = 0; i < 360; i += 10) {
        float radians = i * PI / 180.0;
        float voltage = offset + amplitude * sin(radians);
        
        // Voltage range: 3.5V to 10.2V (final amplified output)
        pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, voltage);
        pocketlab.updateAllDACs();
        delay(10);
        
        // Check actual DAC voltage (before amplifier)
        float dacVoltage = pocketlab.readSignalFeedback(SIGNAL_CHANNEL_A);
        Serial.printf("Set: %.2fV, DAC: %.3fV\n", voltage, dacVoltage);
    }
}
```

### Signal Measurement

```cpp
void measureInputs() {
    float signalA = pocketlab.readSignalVoltage(SIGNAL_CHANNEL_A);
    float signalB = pocketlab.readSignalVoltage(SIGNAL_CHANNEL_B);
    
    // Also read raw ADC values for maximum precision
    uint16_t rawA = pocketlab.readRawADC(SIGNAL_CHANNEL_A);
    uint16_t rawB = pocketlab.readRawADC(SIGNAL_CHANNEL_B);
    
    Serial.printf("A: %.3fV (%d), B: %.3fV (%d)\n", 
                  signalA, rawA, signalB, rawB);
}
```

### Temperature Monitoring

```cpp
void monitorTemperature() {
    float temp = pocketlab.readTemperature();
    
    if (temp > 60.0) {
        Serial.println("WARNING: High temperature!");
        // Reduce power or enable cooling
        pocketlab.setPowerVoltage(0.0);
    }
}
```

## Error Handling

The library performs range checking and validation:

```cpp
// This will return false and print an error
if (!pocketlab.setPowerVoltage(25.0)) {
    Serial.println("Voltage out of range!");
}

// Check if library is properly initialized
if (!pocketlab.isInitialized()) {
    Serial.println("I/O system not ready!");
}
```

## Dependencies

- **Arduino Framework**: Core ESP32 support
- **MCP_ADC Library**: RobTillaart's MCP_ADC library (v0.5.1+)
- **MCP_DAC Library**: RobTillaart's MCP_DAC library (v0.5.2+)
- **SPI Library**: ESP32 SPI support

## Installation

1. Ensure the required libraries are installed via PlatformIO:
   ```ini
   lib_deps = 
       robtillaart/MCP_DAC@^0.5.2
       robtillaart/MCP_ADC@^0.5.1
   ```

2. Copy the `pocketlab_io` folder to your project's `lib/` directory.

3. Include the header in your main code:
   ```cpp
   #include "pocketlab_io.h"
   ```

## Calibration Notes

### ADC Calibration
- The ADC reference is set to 3.3V by default
- For precise measurements, calibrate against a known voltage reference
- Use `setADCReference()` to adjust for actual ESP32 ADC reference

### DAC Calibration  
- DACs use internal 2.048V reference with 1x gain for safety
- Higher gains (2x for 4.096V) can be enabled if needed
- Output scaling depends on external circuitry
- Power control scaling needs characterization of the power stage

### Temperature Calibration
- Default temperature calculation is a placeholder
- Requires proper NTC thermistor characterization
- Implement `_calculateTemperature()` with actual NTC formula

## Thread Safety

The library is **not** thread-safe. If using with RTOS tasks:
- Use mutexes around I/O operations
- Avoid concurrent access to the same DAC/ADC
- Consider using separate instances for different tasks if needed

## Performance

- **SPI Speed**: 1 MHz (configurable)
- **ADC Conversion**: ~10μs per channel
- **DAC Update**: ~5μs per channel  
- **Synchronized Update**: All DACs updated in ~1μs with LDAC

## Troubleshooting

### Common Issues

1. **Initialization Failure**
   - Check SPI connections and pin definitions
   - Verify MCP_ADC/MCP_DAC library versions
   - Ensure proper power supply to ADC/DAC chips

2. **Inaccurate Readings**
   - Calibrate reference voltages
   - Check for noise on power supplies
   - Verify external scaling circuitry

3. **Power Control Issues**
   - Characterize power stage transfer function
   - Implement proper feedback calibration
   - Check current sensing circuit

### Debug Output

Enable verbose output for troubleshooting:

```cpp
void setup() {
    Serial.begin(115200);
    pocketlab.begin();
    pocketlab.printStatus();  // Shows complete system status
}
```

## License

This library is part of the PocKETlab project and follows the same licensing terms.

## Contributing

Contributions are welcome! Please:
- Follow the existing code style
- Add appropriate documentation
- Test on actual hardware
- Update examples as needed
