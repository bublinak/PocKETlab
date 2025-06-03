#ifndef CONFIG_H
#define CONFIG_H

// ================================
// PocKETlab Configuration Template
// ================================
// Copy this file to config.h and customize for your setup
// Add config.h to .gitignore to keep your settings private

// --- Network Configuration ---
#define DEVICE_NAME "PocKETlab"           // mDNS name (device.local)
#define DEFAULT_ADMIN_PASSWORD "admin123" // Default web interface password
#define AP_FALLBACK_PASSWORD "admin123"   // Access point password

// --- WiFi Behavior ---
#define WIFI_CONNECT_TIMEOUT_MS 20000     // How long to try connecting to saved WiFi
#define AP_MODE_TIMEOUT_MS 300000         // How long to stay in AP mode (5 minutes)
#define WIFI_RETRY_INTERVAL_MS 30000      // How often to retry WiFi connection

// --- Hardware Configuration ---
#define ENABLE_PD_CONTROL true            // Enable USB-C PD control
#define PD_CHIP_TYPE CH224K               // CH224K, CH224Q, or FIVE_V_ONLY
#define ENABLE_TEMPERATURE_MONITORING true// Enable NTC temperature sensor
#define ENABLE_FAN_CONTROL true           // Enable PWM fan control

// --- Safety Limits ---
#define MAX_POWER_VOLTAGE 20.0f           // Maximum power output voltage (V)
#define MAX_POWER_CURRENT 3.0f            // Maximum power output current (A)
#define MAX_SIGNAL_VOLTAGE 2.048f         // Maximum signal voltage (V)
#define THERMAL_SHUTDOWN_TEMP 80.0f       // Temperature shutdown threshold (°C)

// --- I/O Configuration ---
#define ADC_SAMPLE_RATE 1000              // ADC sampling rate (Hz)
#define DAC_UPDATE_RATE 100               // DAC update rate (Hz)
#define ENABLE_SIMULTANEOUS_DAC_UPDATE true // Use LDAC for synchronized updates

// --- LED Configuration ---
#define LED_BRIGHTNESS 50                 // RGB LED brightness (0-255)
#define LED_STATUS_UPDATES true           // Enable status indication via LEDs
#define LED_STARTUP_ANIMATION true        // Show fade-in animation on boot

// --- Debug Configuration ---
#define DEBUG_SERIAL_ENABLED true         // Enable serial debug output
#define DEBUG_VERBOSE_NETWORK false       // Verbose network debugging
#define DEBUG_VERBOSE_IO false            // Verbose I/O debugging
#define STATUS_REPORT_INTERVAL_MS 10000   // How often to print status (milliseconds)

// --- Web Interface ---
#define ENABLE_WEB_INTERFACE true         // Enable web-based configuration
#define WEB_SERVER_PORT 80                // HTTP server port
#define ENABLE_WEBSOCKET_UPDATES false    // Real-time WebSocket updates (experimental)

// --- OTA Updates ---
#define ENABLE_OTA_UPDATES true           // Enable over-the-air updates
#define OTA_PASSWORD ""                   // OTA password (empty = use admin password)
#define AUTO_REBOOT_AFTER_OTA true        // Automatically reboot after successful OTA

// --- Advanced Settings ---
#define SPIFFS_AUTO_FORMAT true           // Auto-format SPIFFS if mount fails
#define FACTORY_RESET_HOLD_TIME_MS 10000  // Hold time for factory reset (10 seconds)
#define WATCHDOG_TIMEOUT_MS 30000         // Watchdog timer (0 = disabled)

// --- Calibration Values ---
// Adjust these based on your hardware characteristics
#define VOLTAGE_DIVIDER_RATIO 11.0f       // Voltage measurement scaling
#define CURRENT_SENSE_GAIN 20.0f          // Current sensor amplification
#define NTC_BETA_COEFFICIENT 3950.0f      // NTC thermistor beta value
#define NTC_SERIES_RESISTOR 10000.0f      // NTC series resistor (ohms)

// --- Pin Mapping Override ---
// Uncomment and modify if using custom pin assignments
// #define CUSTOM_PIN_MAPPING
// #ifdef CUSTOM_PIN_MAPPING
// #define PIN_CS_DAC_POWER 17
// #define PIN_CS_DAC_SIGNAL 18
// #define PIN_CS_ADC_SIGNAL 16
// #define PIN_SPI_MOSI 12
// #define PIN_SPI_MISO 13
// #define PIN_SPI_SCK 14
// #define PIN_DAC_LDAC 11
// #define PIN_FAN_PWM 21
// #define PIN_TEMP_PROBE 10
// #endif

#endif // CONFIG_H
