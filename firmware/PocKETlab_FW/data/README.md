# SPIFFS Data Directory

This directory contains files that will be uploaded to the SPIFFS filesystem on the ESP32-S3.

## Usage

- Place any files you want to be available in the SPIFFS filesystem here
- Web UI files (HTML, CSS, JS) should be placed here if you want to serve them from SPIFFS
- Configuration files, certificates, or other data files can be stored here

## Building and Uploading SPIFFS

Use the provided utility scripts:

### Windows:
```bash
spiffs_util.bat build    # Build SPIFFS filesystem
spiffs_util.bat upload   # Upload to device
spiffs_util.bat full     # Complete deployment
```

### Linux/Mac:
```bash
./spiffs_util.sh build   # Build SPIFFS filesystem  
./spiffs_util.sh upload  # Upload to device
./spiffs_util.sh full    # Complete deployment
```

## Partition Layout

The custom partition table provides:
- **SPIFFS Size**: 896KB (0xE0000 bytes)
- **App Partitions**: 2x 1.5MB (OTA support)
- **NVS**: 20KB for WiFi credentials and settings
