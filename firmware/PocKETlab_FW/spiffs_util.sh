#!/bin/bash

# SPIFFS Utility Script for PocKETlab
# This script helps manage SPIFFS operations

ENVIRONMENT="PocKETlab"
PLATFORMIO_CMD="$HOME/.platformio/penv/Scripts/platformio.exe"

case "$1" in
    "build")
        echo "Building SPIFFS filesystem..."
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t buildfs
        ;;
    "upload")
        echo "Uploading SPIFFS filesystem..."
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t uploadfs
        ;;
    "erase")
        echo "Erasing flash..."
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t erase
        ;;
    "monitor")
        echo "Starting serial monitor..."
        $PLATFORMIO_CMD device monitor -e $ENVIRONMENT
        ;;
    "flash")
        echo "Building and uploading firmware..."
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t upload
        ;;
    "full")
        echo "Full deployment: erase -> build -> upload firmware -> build SPIFFS -> upload SPIFFS"
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t erase
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t upload
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t buildfs
        $PLATFORMIO_CMD run -e $ENVIRONMENT -t uploadfs
        ;;
    *)
        echo "PocKETlab SPIFFS Utility"
        echo "Usage: $0 {build|upload|erase|monitor|flash|full}"
        echo ""
        echo "Commands:"
        echo "  build   - Build SPIFFS filesystem from data/ folder"
        echo "  upload  - Upload SPIFFS filesystem to device"
        echo "  erase   - Erase entire flash memory"
        echo "  monitor - Start serial monitor"
        echo "  flash   - Build and upload firmware only"
        echo "  full    - Complete deployment (erase + firmware + SPIFFS)"
        ;;
esac
