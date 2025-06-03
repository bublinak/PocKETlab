/**
 * @file advanced_demo.cpp
 * @brief Advanced demonstration of PocKETlab capabilities
 * 
 * This example demonstrates:
 * - Complete I/O system initialization
 * - Power delivery control
 * - Signal generation and measurement
 * - Temperature monitoring
 * - Network management with web interface
 * - Safety features and error handling
 * - Real-time data logging
 * 
 * Hardware Requirements:
 * - PocKETlab board with ESP32-S3
 * - MCP4822 DACs for power and signal control
 * - MCP3202 ADC for measurements
 * - NTC thermistor for temperature sensing
 * - Optional: CH224K/CH224Q for USB-C PD
 * 
 * @author PocKETlab Team
 * @date 2025-06-02
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SmartLEDs.h>
#include "pin_definitions.h"
#include "pd_control.h"
#include "netman.h"
#include "pocketlab_io.h"

// =========================
// Configuration
// =========================
#define DEVICE_NAME "PocKETlab-Demo"
#define ADMIN_PASSWORD "demo123"
#define LED_PIN 38
#define NUM_LEDS 3
#define BRIGHTNESS 50
#define STATUS_INTERVAL 5000  // 5 seconds

// =========================
// Global Objects
// =========================
SmartLed leds(LED_WS2812, NUM_LEDS, LED_PIN);
PDControl pdControl(FIVE_V_ONLY);  // Safe fallback mode
NetMan netManager(DEVICE_NAME, ADMIN_PASSWORD);
PocKETlabIO io;

// =========================
// Demo State Machine
// =========================
enum DemoState {
    DEMO_INIT,
    DEMO_POWER_TEST,
    DEMO_SIGNAL_GEN,
    DEMO_MEASUREMENT,
    DEMO_SWEEP,
    DEMO_IDLE
};

DemoState currentState = DEMO_INIT;
unsigned long stateStartTime = 0;
unsigned long lastStatusReport = 0;
float sweepVoltage = 0.0;
bool sweepDirection = true;  // true = up, false = down

// =========================
// LED Status Functions
// =========================
void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = Rgb{r, g, b};
    }
    leds.show();
}

void setLEDStatus(DemoState state) {
    switch (state) {
        case DEMO_INIT:
            setLEDColor(255, 255, 0);  // Yellow - initializing
            break;
        case DEMO_POWER_TEST:
            setLEDColor(255, 0, 0);    // Red - power test
            break;
        case DEMO_SIGNAL_GEN:
            setLEDColor(0, 255, 0);    // Green - signal generation
            break;
        case DEMO_MEASUREMENT:
            setLEDColor(0, 0, 255);    // Blue - measurement
            break;
        case DEMO_SWEEP:
            setLEDColor(255, 0, 255);  // Magenta - sweep mode
            break;
        case DEMO_IDLE:
            setLEDColor(255, 255, 255); // White - idle
            break;
    }
}

// =========================
// Demo Functions
// =========================
void runPowerTest() {
    static int testStep = 0;
    static unsigned long stepTime = 0;
    const float testVoltages[] = {5.0, 9.0, 12.0, 15.0, 20.0};
    const float testCurrents[] = {0.5, 1.0, 1.5, 2.0, 3.0};
    const int numSteps = sizeof(testVoltages) / sizeof(testVoltages[0]);
    
    if (millis() - stepTime > 2000) {  // 2 seconds per step
        if (testStep < numSteps) {
            Serial.printf("Power Test Step %d: Setting %.1fV, %.1fA limit\n", 
                         testStep + 1, testVoltages[testStep], testCurrents[testStep]);
            
            io.setPowerVoltage(testVoltages[testStep]);
            io.setPowerCurrent(testCurrents[testStep]);
            io.updateAllDACs();
            
            testStep++;
            stepTime = millis();
        } else {
            // Test complete, move to next state
            currentState = DEMO_SIGNAL_GEN;
            stateStartTime = millis();
            testStep = 0;
            Serial.println("Power test complete, moving to signal generation...");
        }
    }
}

void runSignalGeneration() {
    static float phaseA = 0.0;
    static float phaseB = 0.0;
    
    // Generate sine waves on both channels (voltages are FINAL OUTPUT after 6.7x amplifier)
    float voltageA = 6.7 + 3.35 * sin(phaseA);  // 3.35V to 10.05V sine wave (centered at 6.7V)
    float voltageB = 6.7 + 2.0 * cos(phaseB);   // 4.7V to 8.7V cosine wave (centered at 6.7V)
    
    io.setSignalVoltage(SIGNAL_CHANNEL_A, voltageA);
    io.setSignalVoltage(SIGNAL_CHANNEL_B, voltageB);
    io.updateAllDACs();
    
    phaseA += 0.1;  // Increment phase
    phaseB += 0.15; // Different frequency
    
    if (phaseA > 2 * PI) phaseA -= 2 * PI;
    if (phaseB > 2 * PI) phaseB -= 2 * PI;
    
    // Run for 10 seconds
    if (millis() - stateStartTime > 10000) {
        currentState = DEMO_MEASUREMENT;
        stateStartTime = millis();
        Serial.println("Signal generation complete, moving to measurement demo...");
    }
}

void runMeasurementDemo() {
    // Take multiple readings and calculate statistics
    const int numSamples = 10;
    float powerVoltages[numSamples];
    float powerCurrents[numSamples];
    float signalAs[numSamples];
    float signalBs[numSamples];
    
    // Collect samples
    for (int i = 0; i < numSamples; i++) {
        powerVoltages[i] = io.readPowerVoltage();
        powerCurrents[i] = io.readPowerCurrent();
        signalAs[i] = io.readSignalVoltage(SIGNAL_CHANNEL_A);
        signalBs[i] = io.readSignalVoltage(SIGNAL_CHANNEL_B);
        delay(50);  // 50ms between samples
    }
    
    // Calculate averages
    float avgPowerV = 0, avgPowerI = 0, avgSigA = 0, avgSigB = 0;
    for (int i = 0; i < numSamples; i++) {
        avgPowerV += powerVoltages[i];
        avgPowerI += powerCurrents[i];
        avgSigA += signalAs[i];
        avgSigB += signalBs[i];
    }
    avgPowerV /= numSamples;
    avgPowerI /= numSamples;
    avgSigA /= numSamples;
    avgSigB /= numSamples;
    
    Serial.println("=== Measurement Statistics ===");
    Serial.printf("Power: %.3fV ± %.3fV, %.3fA ± %.3fA\n", 
                  avgPowerV, 
                  abs(powerVoltages[0] - powerVoltages[numSamples-1]),
                  avgPowerI,
                  abs(powerCurrents[0] - powerCurrents[numSamples-1]));
    Serial.printf("Signals: A=%.3fV ± %.3fV, B=%.3fV ± %.3fV\n",
                  avgSigA,
                  abs(signalAs[0] - signalAs[numSamples-1]),
                  avgSigB,
                  abs(signalBs[0] - signalBs[numSamples-1]));
    Serial.printf("Temperature: %.1f°C\n", io.readTemperature());
    
    // Move to sweep demo
    currentState = DEMO_SWEEP;
    stateStartTime = millis();
    sweepVoltage = 0.0;
    sweepDirection = true;
    Serial.println("Measurement demo complete, starting voltage sweep...");
}

void runVoltageSweep() {
    // Sweep voltage from 0V to 13.7V and back (full amplified range)
    const float sweepStep = 0.1;  // 100mV steps (faster for demo)
    const float maxVoltage = 13.7;  // Near maximum amplified output
    
    if (sweepDirection) {
        sweepVoltage += sweepStep;
        if (sweepVoltage >= maxVoltage) {
            sweepVoltage = maxVoltage;
            sweepDirection = false;
        }
    } else {
        sweepVoltage -= sweepStep;
        if (sweepVoltage <= 0.0) {
            sweepVoltage = 0.0;
            sweepDirection = true;
        }
    }
    
    // Set same voltage on both channels (final amplified output)
    io.setSignalVoltage(SIGNAL_CHANNEL_A, sweepVoltage);
    io.setSignalVoltage(SIGNAL_CHANNEL_B, sweepVoltage);
    io.updateAllDACs();
    
    // Run sweep for 20 seconds
    if (millis() - stateStartTime > 20000) {
        currentState = DEMO_IDLE;
        stateStartTime = millis();
        Serial.println("Voltage sweep complete, entering idle mode...");
    }
}

// =========================
// Main Functions
// =========================
void setup() {
    Serial.begin(115200);
    delay(1000);  // Wait for serial
    
    Serial.println("========================================");
    Serial.println("    PocKETlab Advanced Demo");
    Serial.println("========================================");
    
    // Initialize hardware systems
    Serial.println("Initializing hardware...");
    
    // Initialize I/O system
    if (!io.begin()) {
        Serial.println("ERROR: Failed to initialize I/O system!");
        setLEDColor(255, 0, 0);  // Red error indication
        while (1) delay(1000);   // Halt on critical error
    }
    Serial.println("✓ I/O system initialized");
    
    // Initialize power delivery
    pdControl.begin();
    Serial.printf("✓ PD control initialized (max %.1fV)\n", 
                  pdControl.getMaxTestedSourceVoltage());
    
    // Initialize network
    if (!netManager.begin()) {
        Serial.println("WARNING: Network initialization failed!");
    } else {
        netManager.enableOTA(true);
        netManager.enableMDNS(true);
        Serial.println("✓ Network manager initialized");
        Serial.printf("  Device available at: %s.local\n", DEVICE_NAME);
    }
    
    // Initialize LEDs
    setLEDStatus(DEMO_INIT);
    Serial.println("✓ LED system initialized");
    
    // Set initial safe state
    io.setPowerVoltage(5.0);
    io.setPowerCurrent(1.0);
    io.setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
    io.setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
    io.updateAllDACs();
    
    Serial.println("========================================");
    Serial.println("Demo sequence starting...");
    Serial.println("1. Power output test (various voltages)");
    Serial.println("2. Signal generation (sine waves)");
    Serial.println("3. Measurement statistics");
    Serial.println("4. Voltage sweep");
    Serial.println("5. Idle monitoring");
    Serial.println("========================================");
    
    currentState = DEMO_POWER_TEST;
    stateStartTime = millis();
}

void loop() {
    // Handle network operations
    netManager.loop();
    
    // Update LED status
    setLEDStatus(currentState);
    
    // Run current demo state
    switch (currentState) {
        case DEMO_INIT:
            // Should not reach here
            currentState = DEMO_POWER_TEST;
            stateStartTime = millis();
            break;
            
        case DEMO_POWER_TEST:
            runPowerTest();
            break;
            
        case DEMO_SIGNAL_GEN:
            runSignalGeneration();
            break;
            
        case DEMO_MEASUREMENT:
            runMeasurementDemo();
            break;
            
        case DEMO_SWEEP:
            runVoltageSweep();
            break;
            
        case DEMO_IDLE:
            // Continuous monitoring mode
            break;
    }
    
    // Periodic status reporting
    if (millis() - lastStatusReport > STATUS_INTERVAL) {
        lastStatusReport = millis();
        
        Serial.println("\n=== Status Report ===");
        Serial.printf("Demo State: %d, Runtime: %.1fs\n", 
                      currentState, (millis() - stateStartTime) / 1000.0);
        
        if (netManager.isConnected()) {
            Serial.printf("Network: Connected to %s (%s)\n", 
                         netManager.getConnectedSSID().c_str(),
                         netManager.getIPAddress().c_str());
        } else {
            Serial.println("Network: Disconnected");
        }
        
        // Print I/O status
        io.printStatus();
        
        Serial.println("====================\n");
    }
    
    // Small delay to prevent overwhelming the system
    delay(50);
}
