#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
//#include <SmartLEDs.h> // Corrected to SmartLEDs.h
#include "pin_definitions.h"
#include "pd_control.h"
#include "netman.h"
#include "pocketlab_io.h"
#include "postman_mqtt.h"
#include "driver_control.h"

// LED Configuration
//#define LED_PIN 38
//#define NUM_LEDS 3
//#define LED_TYPE LED_WS2812B
//#define BRIGHTNESS 50

//SmartLed leds(LED_TYPE, NUM_LEDS, LED_PIN); // Assuming SmartLEDs class constructor

// PD Control instance - adjust chip type and pins according to your hardware
// For CH224K:
PDControl pdControl(CH224K, PIN_I2C_SCL_PRIMARY, PIN_I2C_SDA_PRIMARY, PIN_PD_SPL);
// For CH224Q (uncomment if using CH224Q):
// PDControl pdControl(CH224Q, PIN_I2C_SDA_PRIMARY, PIN_I2C_SCL_PRIMARY);
// PDControl pdControl(FIVE_V_ONLY); // Using 5V only mode as emergency fallback

// Network Manager instance
NetMan netManager("PocKETlab", "admin123");

// PocKETlab I/O system instance
PocKETlabIO pocketlabIO;

// MQTT and Driver Control
const char* mqtt_server = "10.0.0.42"; // <<< CHANGE TO YOUR MQTT BROKER
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Get MAC address for unique board ID
// String mac = WiFi.macAddress();
PostmanMQTT postman(mqttClient, "pocketlab_01");

DriverControl driver(postman, pocketlabIO);

void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    driver.handleCommand(doc);
}
/*
void fadeIn()
{
	for (int i = 0; i <= BRIGHTNESS; i++)
	{
		for (int j = 0; j < NUM_LEDS; j++)
		{
			// Assuming the SmartLEDs library uses a setPixelColor method or similar
			// and CRGB or an equivalent structure for color.
			// This part might need adjustment based on the actual SmartLEDs API.
			leds[j] = Rgb{(uint8_t)i, (uint8_t)i, (uint8_t)i}; // Example: (index, R, G, B)
		}
		leds.show();
		delay(10);
	}
}

void fadeOut()
{
	for (int i = BRIGHTNESS; i >= 0; i--)
	{
		for (int j = 0; j < NUM_LEDS; j++)
		{
			leds[j] = Rgb{(uint8_t)i, (uint8_t)i, (uint8_t)i};
		}
		leds.show();
		delay(10);
	}
	for (int j = 0; j < NUM_LEDS; j++)
	{
		leds[j] = Rgb{0, 0, 0}; // Turn off LEDs
	}
	leds.show();
}
*/
void setup()
{
	delay(1000); // Wait for USB CDC to initialize
	Serial.begin(115200);
	
	// Check reset reason to help debug reset issues
	esp_reset_reason_t reset_reason = esp_reset_reason();
	Serial.print("Reset reason: ");
	switch (reset_reason) {
		case ESP_RST_POWERON: Serial.println("Power-on reset"); break;
		case ESP_RST_EXT: Serial.println("External reset"); break;
		case ESP_RST_SW: Serial.println("Software reset"); break;
		case ESP_RST_PANIC: Serial.println("Exception/panic reset"); break;
		case ESP_RST_INT_WDT: Serial.println("Interrupt watchdog reset"); break;
		case ESP_RST_TASK_WDT: Serial.println("Task watchdog reset"); break;
		case ESP_RST_WDT: Serial.println("Other watchdog reset"); break;
		case ESP_RST_DEEPSLEEP: Serial.println("Deep sleep reset"); break;
		case ESP_RST_BROWNOUT: Serial.println("Brownout reset"); break;
		case ESP_RST_SDIO: Serial.println("SDIO reset"); break;
		default: Serial.printf("Unknown reset (%d)\n", reset_reason); break;
	}
	
	// Print memory info
	Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
	Serial.printf("Min free heap: %d bytes\n", ESP.getMinFreeHeap());
	Serial.printf("Heap size: %d bytes\n", ESP.getHeapSize());

	// Initialize PocKETlab I/O system
	Serial.println("Initializing PocKETlab I/O...");
	if (!pocketlabIO.begin())
	{
		Serial.println("ERROR: Failed to initialize PocKETlab I/O system!");
		Serial.println("Some features may not work properly.");
	}
	else
	{
		Serial.println("PocKETlab I/O system initialized successfully");
	}

	// Initialize PD Control
	Serial.println("Initializing PD Control...");
	pdControl.begin();
	Serial.print("Max tested source voltage: ");
	Serial.print(pdControl.getMaxTestedSourceVoltage());
	Serial.println("V");
	// Initialize Network Manager
	Serial.println("Initializing Network Manager...");
	if (!netManager.begin())
	{
		Serial.println("Network Manager initialization failed!");
	}

	netManager.enableOTA(true); // Enable OTA updates
	Serial.println("OTA updates enabled");
	// netManager.setMode(MODE_AP_BASIC); // Set to full AP mode with web interface

	// LEDs are typically initialized in their constructor or a begin() method
	// If SmartLEDs has a begin() method, it should be called here.
	// leds.begin(); // Uncomment if applicable

	pinMode(21, OUTPUT);

	Serial.println("Fading in LEDs...");
	//fadeIn();

	Serial.println("Testing fan...");
	digitalWrite(21, HIGH);
	delay(1000);
	digitalWrite(21, LOW);
	Serial.println("Fan test done.");
	Serial.println("Fading out LEDs...");
	//fadeOut();
	// Test PD voltage settings
	Serial.println("Testing PD voltage settings...");
	float testVoltages[] = {9.0, 12.0, 15.0, 20.0, 5.0};
	for (int i = 0; i < 5; i++)
	{
		Serial.print("Setting PD voltage to ");
		Serial.print(testVoltages[i]);
		Serial.println("V");
		pdControl.setPDVoltage(testVoltages[i]);
		delay(1000);
		Serial.print("Current voltage: ");
		Serial.print(pdControl.readPDVoltage());
		Serial.println("V");
	}

	// Test PocKETlab I/O functionality
	if (pocketlabIO.isInitialized())
	{
		Serial.println("Testing PocKETlab I/O functionality...");

		// Test power control
		Serial.println("Setting power output to 3.3V, 0.1A limit...");
		pocketlabIO.setPowerVoltage(3.3);
		pocketlabIO.setPowerCurrent(0.1);
		// Test signal generation with amplifier compensation
		Serial.println("Generating test signals (values are FINAL OUTPUT after 6.7x amplifier):");
		Serial.println("Setting Channel A to 6.7V, Channel B to 13.4V...");
		pocketlabIO.setSignalVoltage(SIGNAL_CHANNEL_A, 1.5); // 6.7V final output (1V DAC)
		pocketlabIO.setSignalVoltage(SIGNAL_CHANNEL_B, 2.1); // 13.4V final output (2V DAC)

		// Update all DACs simultaneously
		pocketlabIO.updateAllDACs();

		delay(500); // Allow settling time

		// Read back and verify values
		Serial.println("Verification of amplifier compensation:");
		Serial.printf("Channel A - DAC: %.3fV, Expected Output: %.2fV\n",
					  pocketlabIO.readSignalFeedback(SIGNAL_CHANNEL_A),
					  pocketlabIO.getExpectedSignalOutput(SIGNAL_CHANNEL_A));
		Serial.printf("Channel B - DAC: %.3fV, Expected Output: %.2fV\n",
					  pocketlabIO.readSignalFeedback(SIGNAL_CHANNEL_B),
					  pocketlabIO.getExpectedSignalOutput(SIGNAL_CHANNEL_B));

		// Read back initial values
		Serial.println("\nComplete I/O readings:");
		pocketlabIO.printStatus();
	}

	// Don't disconnect WiFi since netManager handles it
	Serial.println("Setup done");

    // Setup MQTT after WiFi is connected
    if (netManager.isConnected()) {
        Serial.println("Setting up MQTT...");
        postman.setup(mqtt_server, 1883, callback, 8192);  // Increased buffer for large control system JSON
		postman.subscribe("command");
    }
}

void loop()
{
	// Feed the watchdog to prevent resets
	//esp_task_wdt_reset();
	
	// Handle network manager operations
	netManager.loop();

    // Handle MQTT client loop
    if (netManager.isConnected()) {
        postman.loop();
        driver.loop(); // Handle driver tasks
    }

	// Print status info every 10 seconds (reduced frequency to avoid I/O overload)
	static unsigned long lastStatusPrint = 0;
	if (millis() - lastStatusPrint > 10000 && true) // Temporarily disabled for debugging
	{
		lastStatusPrint = millis();

		Serial.println("=== Status Report ===");
		if (netManager.isConnected())
		{
			Serial.println("WiFi: Connected to " + netManager.getConnectedSSID());
			Serial.println("IP: " + netManager.getIPAddress());
			if (netManager.isMDNSEnabled())
			{
				Serial.println("mDNS: " + netManager.getMDNSName() + ".local");
			}
			postman.sendStatus("ready", driver.getCurrentMode());
		}
		else
		{
			Serial.println("WiFi: Not connected");
			if (netManager.isConfigPortalActive())
			{
				Serial.println("AP Mode: " + netManager.getIPAddress());
			}
		}
		// Add I/O system status to periodic report
		if (pocketlabIO.isInitialized())
		{
			Serial.println("--- I/O Status ---");
			Serial.printf("Power: %.2fV, %.3fA\n",
						  pocketlabIO.readPowerVoltage(),
						  pocketlabIO.readPowerCurrent());
			Serial.printf("Signal Inputs: A=%.3fV, B=%.3fV\n",
						  pocketlabIO.readSignalVoltage(SIGNAL_CHANNEL_A),
						  pocketlabIO.readSignalVoltage(SIGNAL_CHANNEL_B));
			Serial.printf("Signal Outputs: A=%.2fV, B=%.2fV (amplified)\n",
						  pocketlabIO.getExpectedSignalOutput(SIGNAL_CHANNEL_A),
						  pocketlabIO.getExpectedSignalOutput(SIGNAL_CHANNEL_B));
			Serial.printf("Temperature: %.1f°C\n", pocketlabIO.readTemperature());
		}
		Serial.println("==================");
	}
	// Wait a bit before next iteration
	delay(100);
	
	// Monitor memory usage every minute to detect leaks
	static unsigned long lastMemoryCheck = 0;
	if (millis() - lastMemoryCheck > 60000) {
		lastMemoryCheck = millis();
		Serial.printf("Memory check - Free heap: %d bytes, Min free: %d bytes\n", 
					  ESP.getFreeHeap(), ESP.getMinFreeHeap());
		
		if (ESP.getFreeHeap() < 50000) {
			Serial.println("WARNING: Low memory detected!");
		}
	}
}