#include <Arduino.h>
#include <WiFi.h>
#include <SmartLEDs.h> // Corrected to SmartLEDs.h
#include "pin_definitions.h"
#include "pd_control.h"
#include "netman.h"

// LED Configuration
#define LED_PIN 38
#define NUM_LEDS 3
#define LED_TYPE LED_WS2812
#define BRIGHTNESS 50

SmartLed leds(LED_TYPE, NUM_LEDS, LED_PIN); // Assuming SmartLEDs class constructor

// PD Control instance - adjust chip type and pins according to your hardware
// For CH224K:
//PDControl pdControl(CH224K, PIN_I2C_SCL_PRIMARY, PIN_I2C_SDA_PRIMARY, PIN_PD_SPL);
// For CH224Q (uncomment if using CH224Q):
// PDControl pdControl(CH224Q, PIN_I2C_SDA_PRIMARY, PIN_I2C_SCL_PRIMARY);
PDControl pdControl(FIVE_V_ONLY); // Using 5V only mode as emergency fallback

// Network Manager instance
NetMan netManager("PocKETlab", "admin123");

void fadeIn() {
  for (int i = 0; i <= BRIGHTNESS; i++) {
    for (int j = 0; j < NUM_LEDS; j++) {
      // Assuming the SmartLEDs library uses a setPixelColor method or similar
      // and CRGB or an equivalent structure for color.
      // This part might need adjustment based on the actual SmartLEDs API.
      leds[j] = Rgb{(uint8_t)i, (uint8_t)i, (uint8_t)i}; // Example: (index, R, G, B)
    }
    leds.show();
    delay(10);
  }
}

void fadeOut() {
  for (int i = BRIGHTNESS; i >= 0; i--) {
    for (int j = 0; j < NUM_LEDS; j++) {
      leds[j] = Rgb{(uint8_t)i, (uint8_t)i, (uint8_t)i};
    }
    leds.show();
    delay(10);
  }
  for (int j = 0; j < NUM_LEDS; j++) {
    leds[j] = Rgb{0, 0, 0}; // Turn off LEDs
  }
  leds.show();
}

void setup() {
  delay(1000); // Wait for USB CDC to initialize
  Serial.begin(115200);
  // Initialize PD Control
  Serial.println("Initializing PD Control...");
  pdControl.begin();
  Serial.print("Max tested source voltage: ");
  Serial.print(pdControl.getMaxTestedSourceVoltage());
  Serial.println("V");
  // Initialize Network Manager
  Serial.println("Initializing Network Manager...");
  if (!netManager.begin()) {
    Serial.println("Network Manager initialization failed!");
  }

  netManager.enableOTA(true); // Enable OTA updates
  Serial.println("OTA updates enabled");
  //netManager.setMode(MODE_AP_BASIC); // Set to full AP mode with web interface

  // LEDs are typically initialized in their constructor or a begin() method
  // If SmartLEDs has a begin() method, it should be called here.
  // leds.begin(); // Uncomment if applicable

  pinMode(21, OUTPUT);

  Serial.println("Fading in LEDs...");
  fadeIn();

  Serial.println("Testing fan...");
  digitalWrite(21, HIGH);
  delay(1000);
  digitalWrite(21, LOW);
  Serial.println("Fan test done.");
  Serial.println("Fading out LEDs...");
  fadeOut();

  // Test PD voltage settings
  Serial.println("Testing PD voltage settings...");
  float testVoltages[] = {9.0, 12.0, 15.0, 20.0, 5.0};
  for (int i = 0; i < 5; i++) {
    Serial.print("Setting PD voltage to ");
    Serial.print(testVoltages[i]);
    Serial.println("V");
    pdControl.setPDVoltage(testVoltages[i]);
    delay(1000);
    Serial.print("Current voltage: ");
    Serial.print(pdControl.readPDVoltage());    Serial.println("V");
  }

  // Don't disconnect WiFi since netManager handles it
  Serial.println("Setup done");
}

void loop() {
  // Handle network manager operations
  netManager.loop();
  
  // Print status info every 10 seconds
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 10000) {
    lastStatusPrint = millis();
    
    Serial.println("=== Status Report ===");
    if (netManager.isConnected()) {
      Serial.println("WiFi: Connected to " + netManager.getConnectedSSID());
      Serial.println("IP: " + netManager.getIPAddress());
      if (netManager.isMDNSEnabled()) {
        Serial.println("mDNS: " + netManager.getMDNSName() + ".local");
      }
    } else {
      Serial.println("WiFi: Not connected");
      if (netManager.isConfigPortalActive()) {
        Serial.println("AP Mode: " + netManager.getIPAddress());
      }
    }
    Serial.println("==================");
  }
  
  // Original WiFi scanning code (now commented out since netManager handles WiFi)
  /*
  Serial.println("Scanning WiFi networks...");
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");

  if (n == 0) {
    Serial.println("No networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found:");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  */

  // Wait a bit before next iteration
  delay(100);
}