/*
 * PocKETlab I/O Library Example
 * 
 * This example demonstrates how to use the PocKETlab I/O library
 * to control the ADC and DAC functionality of the PocKETlab board.
 * 
 * Features demonstrated:
 * - Power voltage and current control
 * - Signal generation and measurement
 * - Temperature monitoring
 * - Feedback reading
 */

#include <Arduino.h>
#include "pocketlab_io.h"

// Create PocKETlab I/O instance
PocKETlabIO pocketlab;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("PocKETlab I/O Library Example");
    Serial.println("==============================");
    
    // Initialize the I/O system
    if (!pocketlab.begin()) {
        Serial.println("FATAL: Failed to initialize PocKETlab I/O!");
        while (1) {
            delay(1000);
        }
    }
    
    Serial.println("PocKETlab I/O initialized successfully!");
    delay(2000);
}

void loop() {
    // Demonstrate different functionalities
    demonstratePowerControl();
    delay(3000);
    
    demonstrateSignalGeneration();
    delay(3000);
    
    demonstrateSignalMeasurement();
    delay(3000);
    
    demonstrateTemperatureReading();
    delay(3000);
    
    // Print complete status
    pocketlab.printStatus();
    delay(5000);
}

void demonstratePowerControl() {
    Serial.println("\n=== Power Control Demo ===");
    
    // Set power voltage to 5V
    Serial.println("Setting power voltage to 5.0V...");
    if (pocketlab.setPowerVoltage(5.0)) {
        Serial.println("✓ Power voltage set successfully");
    } else {
        Serial.println("✗ Failed to set power voltage");
    }
    
    // Set power current limit to 1A
    Serial.println("Setting power current limit to 1.0A...");
    if (pocketlab.setPowerCurrent(1.0)) {
        Serial.println("✓ Power current limit set successfully");
    } else {
        Serial.println("✗ Failed to set power current limit");
    }
    
    // Update all DACs simultaneously
    pocketlab.updateAllDACs();
    delay(100);  // Allow time for settling
    
    // Read back power feedback
    float powerVoltage = pocketlab.readPowerVoltage();
    float powerCurrent = pocketlab.readPowerCurrent();
    float groundVoltage = pocketlab.readGroundVoltage();
    
    Serial.printf("Power Feedback - Voltage: %.3fV, Current: %.3fA, Ground: %.3fV\n", 
                  powerVoltage, powerCurrent, groundVoltage);
}

void demonstrateSignalGeneration() {
    Serial.println("\n=== Signal Generation Demo ===");
      // Generate 1V on channel A
    Serial.println("Setting signal channel A to 1.0V...");
    if (pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, 1.0)) {
        Serial.println("✓ Signal A set successfully");
    } else {
        Serial.println("✗ Failed to set signal A");
    }
    
    // Generate 1.5V on channel B (within 2.048V range)
    Serial.println("Setting signal channel B to 1.5V...");
    if (pocketlab.setSignalVoltage(SIGNAL_CHANNEL_B, 1.5)) {
        Serial.println("✓ Signal B set successfully");
    } else {
        Serial.println("✗ Failed to set signal B");
    }
    
    // Update all DACs
    pocketlab.updateAllDACs();
    delay(100);
    
    // Read back feedback
    float feedbackA = pocketlab.readSignalFeedback(SIGNAL_CHANNEL_A);
    float feedbackB = pocketlab.readSignalFeedback(SIGNAL_CHANNEL_B);
    
    Serial.printf("Signal Feedback - A: %.3fV, B: %.3fV\n", feedbackA, feedbackB);
}

void demonstrateSignalMeasurement() {
    Serial.println("\n=== Signal Measurement Demo ===");
    
    // Read both signal input channels
    float signalA = pocketlab.readSignalVoltage(SIGNAL_CHANNEL_A);
    float signalB = pocketlab.readSignalVoltage(SIGNAL_CHANNEL_B);
    
    Serial.printf("Signal Inputs - A: %.3fV, B: %.3fV\n", signalA, signalB);
    
    // Demonstrate raw ADC reading
    uint16_t rawA = pocketlab.readRawADC(SIGNAL_CHANNEL_A);
    uint16_t rawB = pocketlab.readRawADC(SIGNAL_CHANNEL_B);
    
    Serial.printf("Raw ADC Values - A: %d, B: %d\n", rawA, rawB);
}

void demonstrateTemperatureReading() {
    Serial.println("\n=== Temperature Monitoring Demo ===");
    
    float temperature = pocketlab.readTemperature();
    Serial.printf("Board Temperature: %.1f°C\n", temperature);
    
    if (temperature > 50.0) {
        Serial.println("⚠️  WARNING: High temperature detected!");
    } else if (temperature < 0.0) {
        Serial.println("⚠️  WARNING: Low temperature detected!");
    } else {
        Serial.println("✓ Temperature within normal range");
    }
}

void demonstrateAdvancedFeatures() {
    Serial.println("\n=== Advanced Features Demo ===");
    
    // Raw DAC control example
    Serial.println("Setting raw DAC values...");
    
    // Set signal DAC channel A to mid-scale (2048/4095)
    if (pocketlab.writeRawDAC(0, 0, 2048)) {
        Serial.println("✓ Raw signal DAC A set to mid-scale");
    }
    
    // Set power DAC channel B to quarter-scale (1024/4095)
    if (pocketlab.writeRawDAC(1, 1, 1024)) {
        Serial.println("✓ Raw power DAC B set to quarter-scale");
    }
    
    // Update all DACs
    pocketlab.updateAllDACs();
    
    // Display current configuration
    Serial.printf("ADC Reference: %.3fV\n", pocketlab.getADCReference());
    Serial.printf("DAC Reference: %.3fV\n", pocketlab.getDACReference());
    Serial.printf("Power Voltage Range: 0-%.1fV\n", pocketlab.getPowerVoltageRange());
    Serial.printf("Power Current Range: 0-%.1fA\n", pocketlab.getPowerCurrentRange());
}

// Example of error handling
void demonstrateErrorHandling() {
    Serial.println("\n=== Error Handling Demo ===");
    
    // Try to set voltage out of range
    Serial.println("Testing out-of-range voltage (should fail)...");
    if (!pocketlab.setPowerVoltage(25.0)) {
        Serial.println("✓ Correctly rejected out-of-range power voltage");
    }
    
    if (!pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, 10.0)) {
        Serial.println("✓ Correctly rejected out-of-range signal voltage");
    }
    
    // Try to set current out of range
    if (!pocketlab.setPowerCurrent(5.0)) {
        Serial.println("✓ Correctly rejected out-of-range power current");
    }
}
