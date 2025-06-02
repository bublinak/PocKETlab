# mDNS Support in NetMan Library

## Overview

The NetMan library now includes mDNS (Multicast DNS) support for STA mode, allowing the device to be discoverable on the local network with a friendly hostname.

## Features

- **Automatic mDNS startup**: mDNS is automatically started when the device connects to a WiFi network in STA mode
- **Service advertising**: Advertises HTTP service on port 80 and optional Arduino OTA service on port 3232
- **Custom hostnames**: Support for custom service names beyond the device name
- **Automatic cleanup**: mDNS is properly stopped when switching modes or disconnecting

## Usage

### Basic Usage

```cpp
#include "netman.h"

NetMan netManager("PocKETlab", "admin123");

void setup() {
    // Initialize NetMan
    netManager.begin();
    
    // Enable mDNS (enabled by default)
    netManager.enableMDNS(true);
    
    // Device will be accessible at "PocKETlab.local"
}
```

### Custom Service Name

```cpp
void setup() {
    netManager.begin();
    
    // Enable mDNS with custom service name
    netManager.enableMDNS(true, "pocketlab");
    
    // Device will be accessible at "pocketlab.local"
    Serial.println("Device accessible at: " + netManager.getMDNSName());
}
```

### Disabling mDNS

```cpp
void setup() {
    netManager.begin();
    
    // Disable mDNS
    netManager.enableMDNS(false);
}
```

## API Reference

### Public Methods

#### `void enableMDNS(bool enable = true, const String& serviceName = "")`
- **enable**: Enable or disable mDNS
- **serviceName**: Optional custom service name (if empty, uses device name)

#### `bool isMDNSEnabled()`
- Returns true if mDNS is enabled

#### `String getMDNSName()`
- Returns the full mDNS name (e.g., "pocketlab.local")

## Services Advertised

When mDNS is enabled, the following services are advertised:

1. **HTTP Service** (`_http._tcp`)
   - Port: 80
   - TXT Records:
     - `device`: Device name
     - `version`: "1.0"

2. **Arduino OTA Service** (`_arduino._tcp`) - Only if OTA is enabled
   - Port: 3232
   - TXT Records:
     - `board`: "esp32s3"
     - `version`: "1.0"

## Network Discovery

Once mDNS is active, you can discover the device using:

### Command Line Tools
```bash
# Linux/macOS
ping pocketlab.local

# Windows (if Bonjour is installed)
ping pocketlab.local
```

### Web Browser
```
http://pocketlab.local
```

### Network Scanner Apps
- **iOS**: Network Analyzer, Discovery
- **Android**: Network Discovery, Fing
- **Desktop**: Bonjour Browser, Advanced IP Scanner

## Automatic Behavior

- **STA Mode**: mDNS starts automatically when connected to WiFi
- **AP Mode**: mDNS is stopped (not needed in AP mode)
- **Connection Loss**: mDNS is stopped when WiFi disconnects
- **Mode Switching**: mDNS is properly cleaned up when switching between modes

## Example Output

When mDNS starts successfully, you'll see:
```
NetMan: mDNS started as pocketlab.local
mDNS enabled - device accessible at: pocketlab.local
```

## Troubleshooting

1. **Device not discoverable**: Ensure both device and client are on the same network
2. **Name conflicts**: If another device uses the same name, try a different service name
3. **Windows issues**: Install Bonjour Print Services for Windows to support .local domains
4. **Router limitations**: Some routers block multicast traffic; check router settings

## Implementation Notes

- Uses ESP32's built-in ESPmDNS library
- Hostname is automatically converted to lowercase
- Service name defaults to device name if not specified
- mDNS is enabled by default but can be disabled if not needed
- No periodic updates required (handled automatically by ESP32)
