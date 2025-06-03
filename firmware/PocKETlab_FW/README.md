# PocKETlab Firmware

Advanced firmware for the PocKETlab hardware platform, providing comprehensive network management, analog I/O control, and web-based configuration.

## Features

### 🌐 Network Management (NetMan Library)
- **WiFi Management**: Auto-connect, fallback AP mode, credential storage
- **mDNS Discovery**: Automatic service advertising (`pocketlab.local`)
- **OTA Updates**: Over-the-air firmware updates via web interface
- **Web Interface**: Modern responsive UI for device configuration
- **Dual Upload System**: Support for both firmware and WebUI uploads
- **Authentication**: Secure access with configurable credentials

### ⚡ Power Delivery Control
- **USB-C PD**: Support for CH224K/CH224Q chips
- **Voltage Control**: 5V, 9V, 12V, 15V, 20V output
- **Source Detection**: Automatic maximum voltage testing
- **Fallback Mode**: Safe 5V-only operation

### 📊 Analog I/O System (PocKETlab I/O Library)
- **Power Control**: 0-20V output, 0-3A current limiting
- **Signal Generation**: Dual-channel DAC (0-2.048V)
- **Signal Measurement**: High-precision ADC sampling
- **Temperature Monitoring**: NTC probe support
- **Safety Features**: Range validation, error handling

### 🎨 Visual Feedback
- **RGB LEDs**: WS2812 status indication
- **Fan Control**: PWM-controlled cooling system

## Hardware Requirements

- **MCU**: ESP32-S3 (Adafruit QT Py ESP32-S3 compatible)
- **DACs**: Dual MCP4822 (12-bit, 2-channel each)
- **ADC**: MCP3202 (12-bit, 2-channel)
- **PD Controller**: CH224K or CH224Q (optional)
- **NTC Thermistor**: Temperature sensing
- **RGB LEDs**: WS2812/NeoPixel chain

## Pin Configuration

See `include/pin_definitions.h` for complete pin mapping:

```cpp
// Power & Signal DACs
PIN_CS_DAC_POWER   = 17  // MCP4822 for power control
PIN_CS_DAC_SIGNAL  = 18  // MCP4822 for signal generation

// ADC
PIN_CS_ADC_SIGNAL  = 16  // MCP3202 for measurements

// SPI Bus
PIN_SPI_MOSI       = 12
PIN_SPI_MISO       = 13
PIN_SPI_SCK        = 14
PIN_DAC_LDAC       = 11  // Synchronized DAC updates

// Other
PIN_FAN_PWM        = 21  // Fan control
PIN_TEMP_PROBE     = 10  // NTC temperature sensor
```

## Quick Start

### 1. Build and Upload Firmware

```bash
# Using PlatformIO CLI
platformio run -e PocKETlab --target upload

# Or using the provided VS Code task
# Ctrl+Shift+P -> "Tasks: Run Task" -> "PlatformIO Build (PocKETlab)"
```

### 2. Initial Setup

1. **First Boot**: Device creates WiFi hotspot `PocKETlab-XXXX`
2. **Connect**: Join the hotspot (password: `admin123`)
3. **Configure**: Browse to `192.168.4.1` and configure WiFi
4. **Access**: Device available at `pocketlab.local` on your network

### 3. Web Interface Features

- **Dashboard**: Real-time device status and I/O readings
- **WiFi Settings**: Network configuration and credentials
- **Power Control**: Adjust output voltage and current limits
- **Signal Generator**: Configure dual-channel output voltages
- **Monitoring**: Temperature and system health metrics
- **Updates**: Upload new firmware or WebUI packages
- **Factory Reset**: Complete device reset option

## API Usage

### Power Control

```cpp
#include "pocketlab_io.h"

PocKETlabIO io;

void setup() {
    if (io.begin()) {
        // Set 12V output with 2A current limit
        io.setPowerVoltage(12.0);
        io.setPowerCurrent(2.0);
        
        // Generate test signals
        io.setSignalVoltage(SIGNAL_CHANNEL_A, 1.5);  // 1.5V on A
        io.setSignalVoltage(SIGNAL_CHANNEL_B, 0.8);  // 0.8V on B
        
        // Apply all changes simultaneously
        io.updateAllDACs();
    }
}

void loop() {
    // Read current measurements
    float voltage = io.readPowerVoltage();
    float current = io.readPowerCurrent();
    float temp = io.readTemperature();
    
    // Read signal inputs
    float signalA = io.readSignalVoltage(SIGNAL_CHANNEL_A);
    float signalB = io.readSignalVoltage(SIGNAL_CHANNEL_B);
    
    io.printStatus();  // Print all readings
    delay(1000);
}
```

### Network Management

```cpp
#include "netman.h"

NetMan netManager("MyDevice", "password123");

void setup() {
    if (netManager.begin()) {
        netManager.enableOTA(true);
        netManager.enableMDNS(true);
        // Device accessible at mydevice.local
    }
}

void loop() {
    netManager.loop();  // Handle network operations
}
```

## Configuration Files

### `platformio.ini`
- **Build Environment**: ESP32-S3 configuration
- **Libraries**: All required dependencies
- **Partitions**: OTA + SPIFFS support
- **Debug Settings**: Serial monitoring setup

### `partitions.csv`
- **OTA Support**: Dual app partitions for safe updates
- **SPIFFS**: File system for web assets and configuration
- **Core Dump**: Crash analysis support

## Development

### Build System
- **PlatformIO**: Modern embedded development platform
- **Libraries**: Modular architecture with custom libraries
- **Dependencies**: Automatic library management

### Library Structure
```
lib/
├── netman/           # Network & WiFi management
├── pd_control/       # USB-C Power Delivery control
└── pocketlab_io/     # Analog I/O hardware abstraction
```

### Web UI Development
```
webui/
├── index.html        # Main interface
├── app.js           # Application logic
├── style.css        # Responsive styling
└── setup.html       # Initial setup page
```

## Debugging

### Serial Monitor
- **Baud Rate**: 115200
- **Status Reports**: Every 10 seconds
- **Error Messages**: Detailed error reporting
- **I/O Readings**: Real-time sensor data

### Common Issues

1. **WiFi Connection**: Check credentials, signal strength
2. **I/O Errors**: Verify SPI connections, chip selects
3. **Power Issues**: Check PD source compatibility
4. **Web Access**: Confirm mDNS/firewall settings

## Safety Features

- **Voltage Limits**: Automatic range validation
- **Current Protection**: Configurable current limiting
- **Temperature Monitoring**: Thermal shutdown protection
- **Error Recovery**: Graceful degradation on component failure

## Updates

The firmware supports multiple update methods:

1. **OTA Web Interface**: Upload .bin files via web UI
2. **USB Serial**: Direct programming via PlatformIO
3. **WebUI Updates**: Upload new web interface packages

## License

This project is open source. See individual library files for specific licensing terms.

## Contributing

1. Follow the existing code style
2. Test all changes thoroughly
3. Update documentation as needed
4. Ensure backward compatibility

## Support

For issues and questions:
- Check the debugging section above
- Review the library documentation
- Examine the example code in each library

---

**Built with ❤️ for the maker community**
