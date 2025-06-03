#include <Arduino.h>
#include "pocketlab_io.h"

// Test program to verify amplifier compensation functionality
// This program validates that the 6.7x amplifier gain compensation
// is working correctly in the PocKETlab I/O library

PocKETlabIO pocketlab;

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial monitor
    
    Serial.println("=== PocKETlab Amplifier Compensation Test ===");
    Serial.println("Testing 6.7x operational amplifier gain compensation");
    Serial.println();
    
    // Initialize the PocKETlab I/O system
    if (!pocketlab.begin()) {
        Serial.println("ERROR: Failed to initialize PocKETlab I/O system!");
        return;
    }
    Serial.println("✓ PocKETlab I/O system initialized successfully");
      // Test amplifier compensation constants
    Serial.println("\n--- Testing Amplifier Constants ---");
    Serial.print("Signal amplifier gain: ");
    Serial.println(SIGNAL_AMPLIFIER_GAIN);
    Serial.print("Signal feedback gain: ");
    Serial.println(SIGNAL_FEEDBACK_GAIN);
    Serial.print("ADC input gain: ");
    Serial.println(ADC_INPUT_GAIN);
    
    float maxOutputRange = pocketlab.getSignalVoltageRange();
    Serial.print("Maximum amplified output voltage: ");
    Serial.print(maxOutputRange);
    Serial.println("V");
    
    float maxInputRange = pocketlab.getSignalInputRange();
    Serial.print("Maximum compensated input voltage: ");
    Serial.print(maxInputRange);
    Serial.println("V");
    
    // Expected: ~13.74V (2.048V * 6.7)
    if (maxOutputRange > 13.0f && maxOutputRange < 14.0f) {
        Serial.println("✓ Output voltage range calculation correct");
    } else {
        Serial.println("✗ ERROR: Output voltage range calculation incorrect");
    }
    
    // Expected: ~20.5V (3.3V / 0.161)
    if (maxInputRange > 20.0f && maxInputRange < 22.0f) {
        Serial.println("✓ Input voltage range calculation correct");
    } else {
        Serial.println("✗ ERROR: Input voltage range calculation incorrect");
    }
    
    // Test voltage compensation calculations
    Serial.println("\n--- Testing Voltage Compensation ---");
    
    struct TestCase {
        float desiredOutput;    // What user wants to output (after amplifier)
        float expectedDAC;      // What DAC should be set to (before amplifier)
        const char* description;
    };
    
    TestCase testCases[] = {
        {0.0f, 0.0f, "Zero output"},
        {1.0f, 0.149f, "1V output (low range)"},
        {3.3f, 0.493f, "3.3V output (logic level)"},
        {5.0f, 0.746f, "5V output (common level)"},
        {10.0f, 1.493f, "10V output (high range)"},
        {13.7f, 2.045f, "13.7V output (near maximum)"}
    };
    
    for (int i = 0; i < 6; i++) {
        TestCase& test = testCases[i];
        float calculatedDAC = test.desiredOutput / SIGNAL_AMPLIFIER_GAIN;
        
        Serial.print("Test: ");
        Serial.print(test.description);
        Serial.print(" -> Desired: ");
        Serial.print(test.desiredOutput);
        Serial.print("V, Expected DAC: ");
        Serial.print(test.expectedDAC);
        Serial.print("V, Calculated: ");
        Serial.print(calculatedDAC);
        Serial.print("V ");
        
        if (abs(calculatedDAC - test.expectedDAC) < 0.01f) {
            Serial.println("✓");
        } else {
            Serial.println("✗");
        }
    }
    
    // Test actual signal setting and feedback reading
    Serial.println("\n--- Testing Signal Generation ---");
    
    float testVoltages[] = {0.0f, 1.0f, 3.3f, 5.0f, 10.0f};
    
    for (int i = 0; i < 5; i++) {
        float targetVoltage = testVoltages[i];
        
        Serial.print("Setting Signal A to ");
        Serial.print(targetVoltage);
        Serial.print("V... ");
        
        if (pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, targetVoltage)) {
            Serial.println("✓");
            
            delay(100); // Allow settling time
            
            // Read back the DAC feedback (before amplifier)
            float dacFeedback = pocketlab.readSignalFeedback(SIGNAL_CHANNEL_A);
            float expectedOutput = pocketlab.getExpectedSignalOutput(SIGNAL_CHANNEL_A);
            
            Serial.print("  DAC feedback: ");
            Serial.print(dacFeedback);
            Serial.print("V, Expected amplified output: ");
            Serial.print(expectedOutput);
            Serial.println("V");
            
            // Verify the DAC voltage is approximately correct
            float expectedDAC = targetVoltage / SIGNAL_AMPLIFIER_GAIN;
            if (abs(dacFeedback - expectedDAC) < 0.1f) {
                Serial.println("  ✓ DAC voltage correct");
            } else {
                Serial.println("  ✗ DAC voltage incorrect");
            }
            
            // Verify the expected output matches target
            if (abs(expectedOutput - targetVoltage) < 0.1f) {
                Serial.println("  ✓ Expected output correct");
            } else {
                Serial.println("  ✗ Expected output incorrect");
            }
        } else {
            Serial.println("✗ Failed to set voltage");
        }        
        Serial.println();
        delay(500);
    }
    
    // Test ADC input compensation
    Serial.println("--- Testing ADC Input Compensation ---");
    
    // Test ADC gain calculation
    float calculatedGain = ADC_INPUT_GAIN;
    float expectedGain = (10.0f/78.0f) * (1.0f + 10.0f/68.0f);
    
    Serial.print("Calculated ADC gain: ");
    Serial.print(calculatedGain);
    Serial.print(", Expected: ");
    Serial.print(expectedGain);
    Serial.print(" ");
    
    if (abs(calculatedGain - expectedGain) < 0.001f) {
        Serial.println("✓");
    } else {
        Serial.println("✗");
    }
    
    // Test that ADC compensation is working
    Serial.println("Reading current ADC inputs...");
    
    float rawA = pocketlab.readSignalVoltageRaw(SIGNAL_CHANNEL_A);
    float compensatedA = pocketlab.readSignalVoltage(SIGNAL_CHANNEL_A);
    float rawB = pocketlab.readSignalVoltageRaw(SIGNAL_CHANNEL_B);
    float compensatedB = pocketlab.readSignalVoltage(SIGNAL_CHANNEL_B);
    
    Serial.print("Channel A: Raw ADC: ");
    Serial.print(rawA);
    Serial.print("V, Compensated: ");
    Serial.print(compensatedA);
    Serial.println("V");
    
    Serial.print("Channel B: Raw ADC: ");
    Serial.print(rawB);
    Serial.print("V, Compensated: ");
    Serial.print(compensatedB);
    Serial.println("V");
    
    // Verify compensation calculation
    float expectedCompensatedA = rawA / ADC_INPUT_GAIN;
    float expectedCompensatedB = rawB / ADC_INPUT_GAIN;
    
    if (abs(compensatedA - expectedCompensatedA) < 0.01f) {
        Serial.println("✓ Channel A compensation calculation correct");
    } else {
        Serial.println("✗ Channel A compensation calculation incorrect");
    }
    
    if (abs(compensatedB - expectedCompensatedB) < 0.01f) {
        Serial.println("✓ Channel B compensation calculation correct");
    } else {
        Serial.println("✗ Channel B compensation calculation incorrect");
    }
    
    Serial.println();
    
    // Test range validation
    Serial.println("--- Testing Range Validation ---");
    
    // Test valid voltages
    if (pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, 5.0f)) {
        Serial.println("✓ Valid voltage (5V) accepted");
    } else {
        Serial.println("✗ Valid voltage (5V) rejected");
    }
    
    // Test out-of-range voltage (should be rejected)
    if (!pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, 15.0f)) {
        Serial.println("✓ Out-of-range voltage (15V) correctly rejected");
    } else {
        Serial.println("✗ Out-of-range voltage (15V) incorrectly accepted");
    }
    
    // Test negative voltage (should be rejected)
    if (!pocketlab.setSignalVoltage(SIGNAL_CHANNEL_A, -1.0f)) {
        Serial.println("✓ Negative voltage (-1V) correctly rejected");
    } else {
        Serial.println("✗ Negative voltage (-1V) incorrectly accepted");
    }
    
    Serial.println("\n--- Test Complete ---");
    Serial.println("Amplifier compensation testing finished.");
    Serial.println("If all tests show ✓, the amplifier compensation is working correctly.");
}

void loop() {
    // Print periodic status to verify ongoing operation
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        
        Serial.println("\n--- Periodic Status ---");
        pocketlab.printStatus();
        Serial.println();
    }
    
    delay(100);
}
